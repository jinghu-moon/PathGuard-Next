#pragma once

#include <stdint.h>

namespace pathguard {

enum class MountTransactionState : uint32_t {
    kPending = 0,
    kApplying = 1,
    kComplete = 2,
    kCancelRequested = 3,
    kCancelled = 4,
    kFailed = 5,
    kRollbackComplete = 6,
    kNamespaceTainted = 7,
};

constexpr bool IsMountTransactionTerminal(MountTransactionState state) {
    return state == MountTransactionState::kComplete
        || state == MountTransactionState::kCancelled
        || state == MountTransactionState::kFailed
        || state == MountTransactionState::kRollbackComplete
        || state == MountTransactionState::kNamespaceTainted;
}

constexpr bool MountTransactionHasResult(MountTransactionState state) {
    return state == MountTransactionState::kComplete
        || state == MountTransactionState::kFailed
        || state == MountTransactionState::kRollbackComplete
        || state == MountTransactionState::kNamespaceTainted;
}

constexpr bool IsMountTransitionAllowed(MountTransactionState from,
                                        MountTransactionState to) {
    switch (from) {
        case MountTransactionState::kPending:
            return to == MountTransactionState::kApplying
                || to == MountTransactionState::kComplete
                || to == MountTransactionState::kCancelRequested
                || to == MountTransactionState::kFailed;
        case MountTransactionState::kApplying:
            return to == MountTransactionState::kComplete
                || to == MountTransactionState::kCancelRequested
                || to == MountTransactionState::kRollbackComplete
                || to == MountTransactionState::kNamespaceTainted;
        case MountTransactionState::kCancelRequested:
            return to == MountTransactionState::kCancelled
                || to == MountTransactionState::kRollbackComplete
                || to == MountTransactionState::kNamespaceTainted;
        case MountTransactionState::kFailed:
            return to == MountTransactionState::kRollbackComplete;
        case MountTransactionState::kComplete:
        case MountTransactionState::kCancelled:
        case MountTransactionState::kRollbackComplete:
        case MountTransactionState::kNamespaceTainted:
            return false;
    }
    return false;
}

}  // namespace pathguard
