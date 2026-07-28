#pragma once

namespace pathguard::provider_redirect {

struct InstallResult {
    bool hook_registration_attempted = false;
    bool hooks_committed = false;
    bool virtualization_active = false;
    bool identity_hooks = false;
};

// A failed PLT commit can still leave a subset of callbacks installed.
constexpr bool MustRetainModule(const InstallResult& result) noexcept {
    return result.hook_registration_attempted;
}

// Registered callbacks may run while pltHookCommit() is still in progress.
// They must remain passive until the complete hook set has been validated.
constexpr bool ShouldEnableHooks(const InstallResult& result) noexcept {
    return result.virtualization_active;
}

}  // namespace pathguard::provider_redirect
