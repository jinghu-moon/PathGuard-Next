#include "pathguard/mount_backend.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    MountBackendCapabilities capabilities;
    capabilities.primitives = kCapabilityOpenTreeMoveMount
        | kCapabilityProcFdMount | kCapabilityStringBindMount;
    capabilities.strict_actions = kMountActionRedirect;
    capabilities.legacy_actions = kMountActionRedirect;

    auto selection = SelectMountBackend(
        kMountActionRedirect, capabilities, true);
    assert(selection.backend == MountBackendKind::kStrictOpenTree);

    capabilities.primitives &= ~kCapabilityOpenTreeMoveMount;
    selection = SelectMountBackend(kMountActionRedirect, capabilities, true);
    assert(selection.backend == MountBackendKind::kStrictProcFd);

    capabilities.primitives &= ~kCapabilityProcFdMount;
    selection = SelectMountBackend(kMountActionRedirect, capabilities, false);
    assert(selection.backend == MountBackendKind::kUnsupported);
    assert(selection.reason == MountBackendReason::kLegacyNotAuthorized);

    selection = SelectMountBackend(kMountActionRedirect, capabilities, true);
    assert(selection.backend == MountBackendKind::kLegacyString);

    selection = SelectMountBackend(
        kMountActionRedirect | kMountActionDenyAnchor, capabilities, true);
    assert(selection.backend == MountBackendKind::kUnsupported);
    assert(selection.reason == MountBackendReason::kUnsupportedAction);

    capabilities.primitives = 0;
    selection = SelectMountBackend(kMountActionRedirect, capabilities, true);
    assert(selection.reason == MountBackendReason::kCapabilityMissing);

    assert(NextMountBackend(
        MountBackendKind::kStrictOpenTree, kMountActionDenyAnchor,
        false, false) == MountBackendKind::kStrictProcFd);
    assert(NextMountBackend(
        MountBackendKind::kStrictProcFd, kMountActionRedirect,
        true, true) == MountBackendKind::kLegacyString);
    assert(NextMountBackend(
        MountBackendKind::kStrictProcFd, kMountActionDenyAnchor,
        true, true) == MountBackendKind::kUnsupported);
    assert(NextMountBackend(
        MountBackendKind::kStrictProcFd, kMountActionRedirect,
        false, true) == MountBackendKind::kUnsupported);
    assert(NextMountBackend(
        MountBackendKind::kStrictProcFd, kMountActionRedirect,
        true, false) == MountBackendKind::kUnsupported);
    assert(NextMountBackend(
        MountBackendKind::kLegacyString, kMountActionRedirect,
        true, true) == MountBackendKind::kUnsupported);
    return 0;
}
