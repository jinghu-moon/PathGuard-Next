#pragma once

#include <stdint.h>

#include "pathguard/capabilities.h"

namespace pathguard {

using MountActionMask = uint32_t;

inline constexpr MountActionMask kMountActionRedirect = UINT32_C(1) << 0;
inline constexpr MountActionMask kMountActionDenyAnchor = UINT32_C(1) << 1;
inline constexpr MountActionMask kMountActionIsolateRoot = UINT32_C(1) << 2;
inline constexpr MountActionMask kMountActionRestore = UINT32_C(1) << 3;

enum class MountBackendKind : uint8_t {
    kUnsupported = 0,
    kStrictOpenTree = 1,
    kStrictProcFd = 2,
    kLegacyString = 3,
};

enum class MountBackendReason : uint8_t {
    kNone = 0,
    kCapabilityMissing = 1,
    kLegacyNotAuthorized = 2,
    kUnsupportedAction = 3,
};

struct MountBackendCapabilities {
    CapabilityBits primitives = 0;
    MountActionMask strict_actions = 0;
    MountActionMask legacy_actions = 0;
};

struct MountBackendSelection {
    MountBackendKind backend = MountBackendKind::kUnsupported;
    MountBackendReason reason = MountBackendReason::kCapabilityMissing;
};

constexpr bool SupportsAllActions(MountActionMask supported,
                                  MountActionMask required) {
    return required != 0 && (supported & required) == required;
}

constexpr MountBackendSelection SelectMountBackend(
        MountActionMask required_actions,
        const MountBackendCapabilities& capabilities,
        bool allow_legacy_string_bind) {
    if (required_actions == 0) {
        return {MountBackendKind::kUnsupported,
                MountBackendReason::kUnsupportedAction};
    }
    if (SupportsAllActions(capabilities.strict_actions, required_actions)) {
        if ((capabilities.primitives & kCapabilityOpenTreeMoveMount) != 0) {
            return {MountBackendKind::kStrictOpenTree, MountBackendReason::kNone};
        }
        if ((capabilities.primitives & kCapabilityProcFdMount) != 0) {
            return {MountBackendKind::kStrictProcFd, MountBackendReason::kNone};
        }
    }
    if ((capabilities.primitives & kCapabilityStringBindMount) == 0) {
        return {MountBackendKind::kUnsupported,
                MountBackendReason::kCapabilityMissing};
    }
    if (!allow_legacy_string_bind) {
        return {MountBackendKind::kUnsupported,
                MountBackendReason::kLegacyNotAuthorized};
    }
    if (!SupportsAllActions(capabilities.legacy_actions, required_actions)) {
        return {MountBackendKind::kUnsupported,
                MountBackendReason::kUnsupportedAction};
    }
    return {MountBackendKind::kLegacyString, MountBackendReason::kNone};
}

}  // namespace pathguard
