#pragma once

#include <stdint.h>

namespace pathguard {

enum class EnforcementState : uint8_t {
    kInactive = 0,
    kActive = 1,
    kPendingRestart = 2,
    kFailed = 3,
};

enum class TransactionOutcome : uint8_t {
    kNone = 0,
    kComplete = 1,
    kFailedPreflight = 2,
    kRollbackComplete = 3,
    kNamespaceTainted = 4,
};

enum class SecurityLevel : uint8_t {
    kNone = 0,
    kFdPinned = 1,
    kLegacyToctou = 2,
};

enum class RuntimeReason : uint8_t {
    kNone = 0,
    kCapabilityMissing = 1,
    kLegacyNotAuthorized = 2,
    kUnsupportedAction = 3,
    kTopologyChanged = 4,
    kPolicyChanged = 5,
    kPreflightFailed = 6,
    kApplyFailed = 7,
    kRollbackFailed = 8,
    kOwnerDeath = 9,
};

struct RuntimeStatusRecord {
    uint32_t version = 1;
    int32_t pid = -1;
    uint32_t uid = 0;
    uint64_t process_start_time = 0;
    uint64_t snapshot_generation = 0;
    uint64_t plan_generation = 0;
    uint64_t topology_generation = 0;
    EnforcementState enforcement = EnforcementState::kInactive;
    uint8_t backend = 0;
    TransactionOutcome transaction = TransactionOutcome::kNone;
    SecurityLevel security = SecurityLevel::kNone;
    RuntimeReason reason = RuntimeReason::kNone;
    int32_t error = 0;
};

}  // namespace pathguard
