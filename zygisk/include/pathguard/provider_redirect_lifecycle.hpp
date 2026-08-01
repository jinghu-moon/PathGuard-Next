#pragma once

#include <stdint.h>

namespace pathguard::provider_redirect {

enum class HookState : uint8_t {
    kUnregistered,
    kRegistered,
    kCommittedPassthrough,
    kCommittedActive,
};

class HookLifecycle final {
public:
    bool Register() noexcept {
        if (state_ != HookState::kUnregistered) return false;
        state_ = HookState::kRegistered;
        return true;
    }
    bool Commit(bool active) noexcept {
        if (state_ != HookState::kRegistered) return false;
        state_ = active ? HookState::kCommittedActive
            : HookState::kCommittedPassthrough;
        return true;
    }
    bool SetActive(bool active) noexcept {
        if (state_ != HookState::kCommittedActive
            && state_ != HookState::kCommittedPassthrough) return false;
        state_ = active ? HookState::kCommittedActive
            : HookState::kCommittedPassthrough;
        return true;
    }
    HookState state() const noexcept { return state_; }
    bool unloadable() const noexcept {
        return state_ == HookState::kUnregistered;
    }
private:
    HookState state_ = HookState::kUnregistered;
};

struct InstallResult {
    bool hook_registration_attempted = false;
    bool hooks_committed = false;
    bool virtualization_active = false;
    bool identity_hook_attempted = false;
    bool identity_hooks = false;
    uint64_t observed_capabilities = 0;
    uint64_t observed_operations = 0;
    uint64_t snapshot_generation = 0;
    uint64_t capability_generation = 0;
    uint64_t hazard_slot_acquire_fail_total = 0;
    uint32_t hazard_slots_in_use_high_watermark = 0;
    uint64_t snapshot_reload_rejected_retire_limit_total = 0;
    uint32_t retired_snapshot_count_high_watermark = 0;
    uint64_t retired_snapshot_bytes_high_watermark = 0;
};

template <typename Function>
constexpr bool IsResolvedJniHook(Function original,
                                 Function replacement) noexcept {
    return original != nullptr && original != replacement;
}

// JNI replacements and a failed PLT commit can leave callbacks installed.
constexpr bool MustRetainModule(const InstallResult& result) noexcept {
    return result.hook_registration_attempted || result.identity_hook_attempted;
}

// Registered callbacks may run while pltHookCommit() is still in progress.
// They must remain passive until the complete hook set has been validated.
constexpr bool ShouldEnableHooks(const InstallResult& result) noexcept {
    return result.virtualization_active;
}

}  // namespace pathguard::provider_redirect
