#include "pathguard/provider_redirect_lifecycle.hpp"

#include <cassert>

namespace {

int Replacement() { return 0; }
int Original() { return 1; }

}  // namespace

int main() {
    using pathguard::provider_redirect::InstallResult;
    using pathguard::provider_redirect::MustRetainModule;
    using pathguard::provider_redirect::ShouldEnableHooks;
    using pathguard::provider_redirect::HookLifecycle;
    using pathguard::provider_redirect::HookState;
    using pathguard::provider_redirect::IsResolvedJniHook;

    assert(!IsResolvedJniHook(static_cast<int (*)()>(nullptr), Replacement));
    assert(!IsResolvedJniHook(Replacement, Replacement));
    assert(IsResolvedJniHook(Original, Replacement));

    HookLifecycle lifecycle;
    assert(lifecycle.unloadable());
    assert(lifecycle.Register());
    assert(!lifecycle.unloadable());
    assert(lifecycle.Commit(false));
    assert(lifecycle.state() == HookState::kCommittedPassthrough);
    assert(lifecycle.SetActive(true));
    assert(lifecycle.state() == HookState::kCommittedActive);
    assert(lifecycle.SetActive(false));
    assert(!lifecycle.unloadable());
    assert(!lifecycle.Register());

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
