#include "pathguard/provider_redirect_lifecycle.hpp"

#include <cassert>

int main() {
    using pathguard::provider_redirect::InstallResult;
    using pathguard::provider_redirect::MustRetainModule;
    using pathguard::provider_redirect::ShouldEnableHooks;

    InstallResult untouched;
    assert(!MustRetainModule(untouched));
    assert(!ShouldEnableHooks(untouched));

    InstallResult partial_commit;
    partial_commit.hook_registration_attempted = true;
    partial_commit.hooks_committed = false;
    partial_commit.virtualization_active = false;
    assert(MustRetainModule(partial_commit));
    assert(!ShouldEnableHooks(partial_commit));

    InstallResult partial_identity;
    partial_identity.identity_hook_attempted = true;
    partial_identity.identity_hooks = false;
    assert(MustRetainModule(partial_identity));
    assert(!ShouldEnableHooks(partial_identity));

    InstallResult active;
    active.hook_registration_attempted = true;
    active.hooks_committed = true;
    active.identity_hook_attempted = true;
    active.identity_hooks = true;
    active.virtualization_active = true;
    assert(MustRetainModule(active));
    assert(ShouldEnableHooks(active));

    return 0;
}
