#pragma once

#include <atomic>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

namespace pathguard::snapshot {

inline constexpr uint32_t kAppPathHazardSlots = 128;
inline constexpr uint32_t kProviderHazardSlots = 256;
inline constexpr uint32_t kMaxHazardSlots = kProviderHazardSlots;
inline constexpr uint32_t kMaxRetiredSnapshots = 8;
inline constexpr size_t kMaxRetiredSnapshotBytes = 8 * 1024 * 1024;

enum class Profile : uint8_t { kAppPath, kProvider };

struct Metrics {
    uint64_t hazard_slot_acquire_fail_total = 0;
    uint32_t hazard_slots_in_use_high_watermark = 0;
    uint64_t snapshot_reload_rejected_retire_limit_total = 0;
    uint32_t retired_snapshot_count_high_watermark = 0;
    size_t retired_snapshot_bytes_high_watermark = 0;
    uint64_t post_fork_registry_rebuild_total = 0;
};

namespace detail {

struct ThreadBinding {
    void* domain = nullptr;
    uint32_t slot = UINT32_MAX;
    void (*release)(void*, uint32_t, ThreadBinding*) = nullptr;

    ~ThreadBinding() {
        if (release != nullptr) release(domain, slot, this);
    }
};

inline thread_local ThreadBinding g_thread_binding;
inline volatile sig_atomic_t g_fork_generation = 0;
inline std::atomic<int> g_atfork_registration{0};

inline void AtForkChildHandler() {
    g_fork_generation = g_fork_generation == 1 ? 2 : 1;
}

inline bool RegisterAtFork() {
#if defined(_WIN32)
    return true;
#else
    int state = g_atfork_registration.load();
    if (state == 1) return true;
    if (state == 2) return false;
    int expected = 0;
    if (g_atfork_registration.compare_exchange_strong(expected, -1)) {
        const int result = pthread_atfork(nullptr, nullptr, AtForkChildHandler);
        g_atfork_registration.store(result == 0 ? 1 : 2);
        return result == 0;
    }
    do {
        state = g_atfork_registration.load();
    } while (state == -1);
    return state == 1;
#endif
}

inline void SaturatingIncrement(std::atomic<uint64_t>* value) {
    uint64_t current = value->load();
    while (current != UINT64_MAX
           && !value->compare_exchange_weak(current, current + 1)) {}
}

template <typename T>
inline void UpdateHighWater(std::atomic<T>* value, T candidate) {
    T current = value->load();
    while (candidate > current
           && !value->compare_exchange_weak(current, candidate)) {}
}

}  // namespace detail

template <typename Snapshot>
class Domain {
public:
    class Guard {
    public:
        Guard() = default;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& other) noexcept { MoveFrom(&other); }
        Guard& operator=(Guard&& other) noexcept {
            if (this != &other) {
                Reset();
                MoveFrom(&other);
            }
            return *this;
        }
        ~Guard() { Reset(); }

        const Snapshot* get() const { return snapshot_; }
        const Snapshot& operator*() const { return *snapshot_; }
        const Snapshot* operator->() const { return snapshot_; }
        explicit operator bool() const { return snapshot_ != nullptr; }

        void Reset() {
            if (domain_ != nullptr) domain_->ClearHazard(slot_);
            domain_ = nullptr;
            snapshot_ = nullptr;
            slot_ = UINT32_MAX;
        }

    private:
        friend class Domain;
        Guard(Domain* domain, uint32_t slot, const Snapshot* snapshot)
            : domain_(domain), snapshot_(snapshot), slot_(slot) {}
        void MoveFrom(Guard* other) {
            domain_ = other->domain_;
            snapshot_ = other->snapshot_;
            slot_ = other->slot_;
            other->domain_ = nullptr;
            other->snapshot_ = nullptr;
            other->slot_ = UINT32_MAX;
        }
        Domain* domain_ = nullptr;
        const Snapshot* snapshot_ = nullptr;
        uint32_t slot_ = UINT32_MAX;
    };

    explicit Domain(Profile profile)
        : profile_(profile), slot_capacity_(profile == Profile::kAppPath
              ? kAppPathHazardSlots : kProviderHazardSlots),
          atfork_registered_(detail::RegisterAtFork()),
          fork_generation_(detail::g_fork_generation) {}

    Domain(const Domain&) = delete;
    Domain& operator=(const Domain&) = delete;

    ~Domain() {
        ReleaseCurrentThread();
        Snapshot* active = active_.exchange(nullptr);
        delete active;
        for (uint32_t i = 0; i < retired_count_; ++i) delete retired_[i].pointer;
    }

    Guard Acquire() {
        EnsurePostForkReady();
        if (!atfork_registered_) return {};
        const uint32_t slot = AcquireSlot();
        if (slot == UINT32_MAX) return {};
        for (;;) {
            Snapshot* candidate = active_.load();
            slots_[slot].hazard.store(candidate);
            if (active_.load() == candidate) {
                return Guard(this, slot, candidate);
            }
            slots_[slot].hazard.store(nullptr);
        }
    }

    bool Publish(Snapshot* candidate, size_t bytes) {
        EnsurePostForkReady();
        if (!atfork_registered_) return false;
        if (candidate == nullptr || bytes == 0) return false;
        Reclaim();
        Snapshot* current = active_.load();
        const size_t current_bytes = active_bytes_.load();
        if (current != nullptr
            && (retired_count_ >= kMaxRetiredSnapshots
                || current_bytes > kMaxRetiredSnapshotBytes - retired_bytes_)) {
            detail::SaturatingIncrement(&reload_rejected_);
            return false;
        }
        Snapshot* previous = active_.exchange(candidate);
        const size_t previous_bytes = active_bytes_.exchange(bytes);
        if (previous != nullptr) {
            retired_[retired_count_++] = {previous, previous_bytes};
            retired_bytes_ += previous_bytes;
            detail::UpdateHighWater(&retired_count_high_water_, retired_count_);
            detail::UpdateHighWater(&retired_bytes_high_water_, retired_bytes_);
        }
        Reclaim();
        return true;
    }

    void Reclaim() {
        EnsurePostForkReady();
        uint32_t output = 0;
        size_t remaining_bytes = 0;
        for (uint32_t i = 0; i < retired_count_; ++i) {
            if (IsHazard(retired_[i].pointer)) {
                retired_[output++] = retired_[i];
                remaining_bytes += retired_[i].bytes;
            } else {
                delete retired_[i].pointer;
            }
        }
        retired_count_ = output;
        retired_bytes_ = remaining_bytes;
    }

    void ReleaseCurrentThread() {
        EnsurePostForkReady();
        detail::ThreadBinding& binding = detail::g_thread_binding;
        if (binding.domain == this && binding.release != nullptr) {
            binding.release(binding.domain, binding.slot, &binding);
        }
    }

    Metrics metrics() const {
        return {
            slot_acquire_fail_.load(),
            slots_high_water_.load(),
            reload_rejected_.load(),
            retired_count_high_water_.load(),
            retired_bytes_high_water_.load(),
            post_fork_rebuild_.load(),
        };
    }

    uint32_t slot_capacity() const { return slot_capacity_; }
    bool atfork_registered() const { return atfork_registered_; }
    uint32_t retired_count() const { return retired_count_; }
    size_t retired_bytes() const { return retired_bytes_; }

    static void MarkPostForkDirtyForTesting() {
        detail::AtForkChildHandler();
    }

private:
    struct Slot {
        std::atomic<void*> owner{nullptr};
        std::atomic<Snapshot*> hazard{nullptr};
    };
    struct Retired {
        Snapshot* pointer = nullptr;
        size_t bytes = 0;
    };

    static void ReleaseBinding(void* raw, uint32_t slot,
                               detail::ThreadBinding* binding) {
        Domain* domain = static_cast<Domain*>(raw);
        if (domain != nullptr && slot < domain->slot_capacity_) {
            domain->slots_[slot].hazard.store(nullptr);
            void* expected = binding;
            if (domain->slots_[slot].owner.compare_exchange_strong(
                    expected, nullptr)) {
                domain->slots_in_use_.fetch_sub(1);
            }
        }
        binding->domain = nullptr;
        binding->slot = UINT32_MAX;
        binding->release = nullptr;
    }

    uint32_t AcquireSlot() {
        detail::ThreadBinding& binding = detail::g_thread_binding;
        if (binding.domain == this && binding.slot < slot_capacity_) {
            return binding.slot;
        }
        if (binding.release != nullptr) {
            binding.release(binding.domain, binding.slot, &binding);
        }
        for (uint32_t i = 0; i < slot_capacity_; ++i) {
            void* expected = nullptr;
            if (slots_[i].owner.compare_exchange_strong(expected, &binding)) {
                binding.domain = this;
                binding.slot = i;
                binding.release = &ReleaseBinding;
                const uint32_t in_use = slots_in_use_.fetch_add(1) + 1;
                detail::UpdateHighWater(&slots_high_water_, in_use);
                return i;
            }
        }
        detail::SaturatingIncrement(&slot_acquire_fail_);
        return UINT32_MAX;
    }

    void ClearHazard(uint32_t slot) {
        if (slot < slot_capacity_) slots_[slot].hazard.store(nullptr);
    }

    bool IsHazard(const Snapshot* candidate) const {
        for (uint32_t i = 0; i < slot_capacity_; ++i) {
            if (slots_[i].hazard.load() == candidate) return true;
        }
        return false;
    }

    void EnsurePostForkReady() {
        const sig_atomic_t current = detail::g_fork_generation;
        if (fork_generation_ == current) return;
        detail::ThreadBinding& binding = detail::g_thread_binding;
        if (binding.domain == this) {
            binding.domain = nullptr;
            binding.slot = UINT32_MAX;
            binding.release = nullptr;
        }
        for (uint32_t i = 0; i < slot_capacity_; ++i) {
            slots_[i].hazard.store(nullptr);
            slots_[i].owner.store(nullptr);
        }
        slots_in_use_.store(0);
        Snapshot* inherited = active_.exchange(nullptr);
        delete inherited;
        active_bytes_.store(0);
        for (uint32_t i = 0; i < retired_count_; ++i) {
            delete retired_[i].pointer;
            retired_[i] = {};
        }
        retired_count_ = 0;
        retired_bytes_ = 0;
        fork_generation_ = current;
        detail::SaturatingIncrement(&post_fork_rebuild_);
    }

    Profile profile_;
    uint32_t slot_capacity_;
    Slot slots_[kMaxHazardSlots]{};
    std::atomic<Snapshot*> active_{nullptr};
    std::atomic<size_t> active_bytes_{0};
    Retired retired_[kMaxRetiredSnapshots]{};
    uint32_t retired_count_ = 0;
    size_t retired_bytes_ = 0;
    std::atomic<uint32_t> slots_in_use_{0};
    std::atomic<uint64_t> slot_acquire_fail_{0};
    std::atomic<uint32_t> slots_high_water_{0};
    std::atomic<uint64_t> reload_rejected_{0};
    std::atomic<uint32_t> retired_count_high_water_{0};
    std::atomic<size_t> retired_bytes_high_water_{0};
    std::atomic<uint64_t> post_fork_rebuild_{0};
    bool atfork_registered_ = false;
    sig_atomic_t fork_generation_ = 0;
};

}  // namespace pathguard::snapshot
