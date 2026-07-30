#pragma once

#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#if defined(_WIN32)
#include <atomic>
#else
#include <pthread.h>
#endif

#include "pathguard/provider_caller_uid.hpp"

namespace pathguard::provider_redirect {

template <typename Value>
inline Value AtomicLoad(const Value* value) {
#if defined(_WIN32)
    return std::atomic_ref<Value>(*const_cast<Value*>(value)).load(
        std::memory_order_seq_cst);
#else
    return __atomic_load_n(value, __ATOMIC_SEQ_CST);
#endif
}

template <typename Value, typename Desired>
inline void AtomicStore(Value* value, Desired desired) {
#if defined(_WIN32)
    std::atomic_ref<Value>(*value).store(static_cast<Value>(desired),
                                         std::memory_order_seq_cst);
#else
    __atomic_store_n(value, static_cast<Value>(desired), __ATOMIC_SEQ_CST);
#endif
}

template <typename Value, typename Desired>
inline bool AtomicCompareExchange(Value* value, Value* expected,
                                  Desired desired) {
#if defined(_WIN32)
    return std::atomic_ref<Value>(*value).compare_exchange_strong(
        *expected, static_cast<Value>(desired), std::memory_order_seq_cst);
#else
    return __atomic_compare_exchange_n(
        value, expected, static_cast<Value>(desired), false,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
}

template <typename Value, typename Desired>
inline Value AtomicExchange(Value* value, Desired desired) {
#if defined(_WIN32)
    return std::atomic_ref<Value>(*value).exchange(
        static_cast<Value>(desired), std::memory_order_seq_cst);
#else
    return __atomic_exchange_n(
        value, static_cast<Value>(desired), __ATOMIC_SEQ_CST);
#endif
}

template <typename Value>
inline Value AtomicAddFetch(Value* value, Value amount) {
#if defined(_WIN32)
    return std::atomic_ref<Value>(*value).fetch_add(
        amount, std::memory_order_seq_cst) + amount;
#else
    return __atomic_add_fetch(value, amount, __ATOMIC_SEQ_CST);
#endif
}

template <typename Value>
inline Value AtomicSubFetch(Value* value, Value amount) {
#if defined(_WIN32)
    return std::atomic_ref<Value>(*value).fetch_sub(
        amount, std::memory_order_seq_cst) - amount;
#else
    return __atomic_sub_fetch(value, amount, __ATOMIC_SEQ_CST);
#endif
}

struct PolicySnapshotMetrics {
    uint64_t hazard_slot_acquire_fail_total = 0;
    uint32_t hazard_slots_in_use_high_watermark = 0;
    uint64_t snapshot_reload_rejected_retire_limit_total = 0;
    uint32_t retired_snapshot_count_high_watermark = 0;
    size_t retired_snapshot_bytes_high_watermark = 0;
    uint64_t post_fork_registry_rebuild_total = 0;
};

inline void SaturatingIncrement(uint64_t* value) {
    uint64_t current = AtomicLoad(value);
    while (current != UINT64_MAX
           && !AtomicCompareExchange(value, &current, current + 1)) {}
}

template <typename Value>
inline void UpdateHighWater(Value* value, Value candidate) {
    Value current = AtomicLoad(value);
    while (candidate > current
           && !AtomicCompareExchange(value, &current, candidate)) {}
}

inline volatile sig_atomic_t g_policy_snapshot_fork_generation = 0;
inline int g_policy_snapshot_atfork_state = 0;

inline void PolicySnapshotAtForkChild() {
    g_policy_snapshot_fork_generation =
        g_policy_snapshot_fork_generation == 1 ? 2 : 1;
}

inline bool InitializePolicySnapshotAtFork() {
    int state = AtomicLoad(&g_policy_snapshot_atfork_state);
    if (state == 1) return true;
    if (state == 2) return false;
    int expected = 0;
    if (AtomicCompareExchange(&g_policy_snapshot_atfork_state, &expected, -1)) {
#if defined(_WIN32)
        const int error = 0;
#else
        const int error = pthread_atfork(nullptr, nullptr, PolicySnapshotAtForkChild);
#endif
        AtomicStore(&g_policy_snapshot_atfork_state, error == 0 ? 1 : 2);
        return error == 0;
    }
    do {
        state = AtomicLoad(&g_policy_snapshot_atfork_state);
    } while (state == -1);
    return state == 1;
}

template <typename Snapshot>
class PolicySnapshotDomainBase {
public:
    class Guard {
    public:
        Guard() = default;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& other) noexcept { Move(&other); }
        Guard& operator=(Guard&& other) noexcept {
            if (this != &other) { Reset(); Move(&other); }
            return *this;
        }
        ~Guard() { Reset(); }
        explicit operator bool() const { return snapshot_ != nullptr; }
        const Snapshot& operator*() const { return *snapshot_; }
        const Snapshot* operator->() const { return snapshot_; }
    private:
        template <typename, uint32_t> friend class PolicySnapshotDomain;
        Guard(CallerUidContext* state, const Snapshot* snapshot)
            : state_(state), snapshot_(snapshot) {}
        void Reset() {
            if (state_ != nullptr) {
                AtomicStore(&state_->policy_hazard, nullptr);
            }
            state_ = nullptr; snapshot_ = nullptr;
        }
        void Move(Guard* other) {
            state_ = other->state_; snapshot_ = other->snapshot_;
            other->state_ = nullptr; other->snapshot_ = nullptr;
        }
        CallerUidContext* state_ = nullptr;
        const Snapshot* snapshot_ = nullptr;
    };

    virtual Guard Acquire(CallerUidContext* state) = 0;
    virtual void Release(CallerUidContext* state) = 0;
};

template <typename Snapshot, uint32_t SlotCount>
class PolicySnapshotDomain final : public PolicySnapshotDomainBase<Snapshot> {
public:
    using Guard = typename PolicySnapshotDomainBase<Snapshot>::Guard;
    using Releaser = void (*)(Snapshot*);

    explicit PolicySnapshotDomain(Releaser releaser)
        : releaser_(releaser),
          fork_generation_(g_policy_snapshot_fork_generation) {}

    Guard Acquire(CallerUidContext* state) override {
        EnsureProcessState(state);
        if (state == nullptr || !Bind(state)) return {};
        for (;;) {
            Snapshot* current = AtomicLoad(&active_);
            AtomicStore(&state->policy_hazard, current);
            if (AtomicLoad(&active_) == current) {
                return Guard(state, current);
            }
            AtomicStore(&state->policy_hazard, nullptr);
        }
    }

    void Release(CallerUidContext* state) override {
        EnsureProcessState(state);
        if (state == nullptr || state->policy_domain != this
            || state->policy_hazard_slot >= SlotCount) return;
        const uint32_t slot = state->policy_hazard_slot;
        AtomicStore(&state->policy_hazard, nullptr);
        AtomicStore(&hazards_[slot], nullptr);
        void* expected = state;
        if (AtomicCompareExchange(&owners_[slot], &expected, nullptr)) {
            AtomicSubFetch(&slots_in_use_, uint32_t{1});
        }
        state->policy_domain = nullptr;
        state->policy_hazard_slot = UINT32_MAX;
        // Retired storage is owned exclusively by the policy publisher. TLS
        // teardown only releases reader ownership; allowing it to compact the
        // retire list would race a concurrent Publish(). The next publish
        // performs reclamation after observing this cleared hazard.
    }

    bool Publish(Snapshot* candidate, size_t bytes) {
        EnsureProcessState(nullptr);
        if (candidate == nullptr || bytes == 0 || bytes > 8 * 1024 * 1024) {
            if (candidate != nullptr && bytes > 8 * 1024 * 1024) {
                SaturatingIncrement(&reload_rejected_);
            }
            return false;
        }
        uint32_t expected = 0;
        if (!AtomicCompareExchange(&writer_active_, &expected, uint32_t{1})) {
            return false;
        }
        Reclaim();
        if (retired_count_ >= 8 || active_bytes_ > 8 * 1024 * 1024
            || retired_bytes_ > 8 * 1024 * 1024 - active_bytes_) {
            SaturatingIncrement(&reload_rejected_);
            AtomicStore(&writer_active_, uint32_t{0});
            return false;
        }
        Snapshot* previous = AtomicExchange(&active_, candidate);
        const size_t previous_bytes = active_bytes_;
        active_bytes_ = bytes;
        if (previous != nullptr) {
            retired_[retired_count_] = previous;
            retired_sizes_[retired_count_++] = previous_bytes;
            retired_bytes_ += previous_bytes;
            UpdateHighWater(&retired_count_high_water_, retired_count_);
            UpdateHighWater(&retired_bytes_high_water_, retired_bytes_);
        }
        Reclaim();
        AtomicStore(&writer_active_, uint32_t{0});
        return true;
    }

    PolicySnapshotMetrics metrics() const {
        return {
            AtomicLoad(&slot_acquire_fail_),
            AtomicLoad(&slots_high_water_),
            AtomicLoad(&reload_rejected_),
            AtomicLoad(&retired_count_high_water_),
            AtomicLoad(&retired_bytes_high_water_),
            AtomicLoad(&post_fork_rebuild_),
        };
    }

    uint32_t retired_count() const {
        return AtomicLoad(&retired_count_);
    }

private:
    void EnsureProcessState(CallerUidContext* current) {
        const sig_atomic_t generation = g_policy_snapshot_fork_generation;
        if (generation == fork_generation_) return;
        if (current != nullptr) {
            current->policy_hazard = nullptr;
            current->policy_domain = nullptr;
            current->policy_hazard_slot = UINT32_MAX;
        }
        for (uint32_t index = 0; index < SlotCount; ++index) {
            owners_[index] = nullptr;
            hazards_[index] = nullptr;
        }
        // A child must not inherit its parent's identity/admission decision.
        // The next specialization publishes a snapshot built for the child;
        // until then Acquire observes no active policy and fails open.
        Snapshot* inherited = AtomicExchange(&active_, nullptr);
        if (inherited != nullptr) releaser_(inherited);
        active_bytes_ = 0;
        for (uint32_t index = 0; index < retired_count_; ++index) {
            releaser_(retired_[index]);
            retired_[index] = nullptr;
            retired_sizes_[index] = 0;
        }
        retired_count_ = 0;
        retired_bytes_ = 0;
        slots_in_use_ = 0;
        fork_generation_ = generation;
        SaturatingIncrement(&post_fork_rebuild_);
    }

    bool Bind(CallerUidContext* state) {
        if (state->policy_domain == this && state->policy_hazard_slot < SlotCount) {
            const uint32_t slot = state->policy_hazard_slot;
            if (AtomicLoad(&owners_[slot]) == state
                && AtomicLoad(&hazards_[slot])
                    == &state->policy_hazard) {
                return true;
            }
            state->policy_domain = nullptr;
            state->policy_hazard_slot = UINT32_MAX;
        }
        if (state->policy_domain != nullptr) return false;
        for (uint32_t index = 0; index < SlotCount; ++index) {
            void* expected = nullptr;
            if (AtomicCompareExchange(&owners_[index], &expected,
                                      static_cast<void*>(state))) {
                state->policy_domain = this;
                state->policy_hazard_slot = index;
                hazards_[index] = &state->policy_hazard;
                const uint32_t in_use = AtomicAddFetch(
                    &slots_in_use_, uint32_t{1});
                UpdateHighWater(&slots_high_water_, in_use);
                return true;
            }
        }
        SaturatingIncrement(&slot_acquire_fail_);
        return false;
    }

    bool Hazardous(Snapshot* snapshot) const {
        for (uint32_t index = 0; index < SlotCount; ++index) {
            void** hazard = AtomicLoad(&hazards_[index]);
            if (hazard != nullptr
                && AtomicLoad(hazard) == snapshot) return true;
        }
        return false;
    }

    void Reclaim() {
        uint32_t kept = 0;
        size_t kept_bytes = 0;
        for (uint32_t index = 0; index < retired_count_; ++index) {
            if (Hazardous(retired_[index])) {
                retired_[kept] = retired_[index];
                retired_sizes_[kept] = retired_sizes_[index];
                kept_bytes += retired_sizes_[index];
                ++kept;
            } else {
                releaser_(retired_[index]);
            }
        }
        retired_count_ = kept;
        retired_bytes_ = kept_bytes;
    }

    Releaser releaser_;
    Snapshot* active_ = nullptr;
    size_t active_bytes_ = 0;
    void* owners_[SlotCount]{};
    void** hazards_[SlotCount]{};
    Snapshot* retired_[8]{};
    size_t retired_sizes_[8]{};
    uint32_t retired_count_ = 0;
    size_t retired_bytes_ = 0;
    uint32_t writer_active_ = 0;
    sig_atomic_t fork_generation_ = 0;
    uint32_t slots_in_use_ = 0;
    uint64_t slot_acquire_fail_ = 0;
    uint32_t slots_high_water_ = 0;
    uint64_t reload_rejected_ = 0;
    uint32_t retired_count_high_water_ = 0;
    size_t retired_bytes_high_water_ = 0;
    uint64_t post_fork_rebuild_ = 0;
};

}  // namespace pathguard::provider_redirect
