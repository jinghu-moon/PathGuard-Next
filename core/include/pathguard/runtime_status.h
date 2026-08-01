#pragma once

#include <stdint.h>

#include "pathguard/action_admission.h"

namespace pathguard {

inline constexpr uint32_t kRuntimeStatusVersion = 2;
inline constexpr uint32_t kMaxRuntimeActionStatus = 16;

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

enum class RuntimeActionKind : uint8_t {
    kUnknown = 0,
    kDeny = 1,
    kRedirect = 2,
    kObserve = 3,
    kExport = 4,
};

struct RuntimeActionStatus {
    RuntimeActionKind kind = RuntimeActionKind::kUnknown;
    AdmissionDomain domain = AdmissionDomain::kMount;
    bool intent_enabled = false;
    uint8_t reserved = 0;
    OperationMask action_mask = 0;
    uint64_t conflict_id = 0;
    uint64_t rule_id = 0;
    uint32_t selector_id = 0;
    ActionAdmission admission;
};

struct RuntimeStatusCounters {
    uint64_t hazard_slot_acquire_fail_total = 0;
    uint32_t hazard_slots_in_use_high_watermark = 0;
    uint64_t snapshot_reload_rejected_retire_limit_total = 0;
    uint32_t retired_snapshot_count_high_watermark = 0;
    uint64_t retired_snapshot_bytes_high_watermark = 0;
    uint64_t event_overflow_total = 0;
    uint64_t diagnostic_drop_total = 0;
};

struct RuntimeStatusRecord {
    uint32_t version = kRuntimeStatusVersion;
    int32_t pid = -1;
    uint32_t uid = 0;
    uint64_t process_start_time = 0;
    uint64_t content_generation = 0;
    uint64_t snapshot_generation = 0;
    uint64_t plan_generation = 0;
    uint64_t capability_generation = 0;
    uint64_t topology_generation = 0;
    CapabilityBits observed_capabilities = 0;
    EnforcementState enforcement = EnforcementState::kInactive;
    uint8_t backend = 0;
    TransactionOutcome transaction = TransactionOutcome::kNone;
    SecurityLevel security = SecurityLevel::kNone;
    RuntimeReason reason = RuntimeReason::kNone;
    int32_t error = 0;
    uint32_t action_count = 0;
    uint32_t action_total = 0;
    bool actions_truncated = false;
    RuntimeStatusCounters counters;
    RuntimeActionStatus actions[kMaxRuntimeActionStatus]{};
};

inline bool AppendRuntimeAction(RuntimeStatusRecord* status,
                                const RuntimeActionStatus& action) {
    if (status == nullptr) return false;
    if (status->action_total != UINT32_MAX) ++status->action_total;
    if (status->action_count >= kMaxRuntimeActionStatus) {
        status->actions_truncated = true;
        return false;
    }
    status->actions[status->action_count++] = action;
    return true;
}

}  // namespace pathguard
