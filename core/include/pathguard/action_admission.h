#pragma once

#include <stdint.h>

#include "pathguard/capabilities.h"

namespace pathguard {

enum class AdmissionDomain : uint8_t {
    kMount = 0,
    kAppPath = 1,
    kProvider = 2,
    kCompleteVfs = 3,
    kEvent = 4,
};

enum class AdapterState : uint8_t {
    kInactive,
    kActive,
    kUnsupported,
    kDegraded,
};

enum class ActionAdmissionState : uint8_t {
    kInactive,
    kActive,
    kUnsupported,
};

enum class ActionAdmissionReason : uint8_t {
    kNone,
    kIntentDisabled,
    kGenerationStale,
    kAdapterInactive,
    kAdapterUnsupported,
    kCapabilityMissing,
    kOperationMissing,
};

struct DomainCapabilityState {
    AdapterState state = AdapterState::kInactive;
    OperationMask observed_operations = 0;
    int32_t probe_error = 0;
};

struct CapabilitySnapshot {
    uint64_t capability_generation = 0;
    uint64_t plan_generation = 0;
    CapabilityBits observed_capabilities = 0;
    DomainCapabilityState domains[5]{};
};

struct ActionRequirement {
    AdmissionDomain domain = AdmissionDomain::kMount;
    CapabilityBits required_capabilities = 0;
    OperationMask required_operations = 0;
    bool intent_enabled = true;
};

struct ActionAdmission {
    ActionAdmissionState state = ActionAdmissionState::kInactive;
    ActionAdmissionReason reason = ActionAdmissionReason::kAdapterInactive;
    CapabilityBits required_capabilities = 0;
    CapabilityBits observed_capabilities = 0;
    CapabilityBits missing_capabilities = 0;
    OperationMask required_operations = 0;
    OperationMask observed_operations = 0;
    OperationMask missing_operations = 0;
    uint64_t capability_generation = 0;
    uint64_t plan_generation = 0;
    int32_t probe_error = 0;

    bool active() const { return state == ActionAdmissionState::kActive; }
};

inline ActionAdmission AdmitAction(const ActionRequirement& requirement,
                                   const CapabilitySnapshot& snapshot,
                                   uint64_t expected_plan_generation) {
    ActionAdmission output;
    output.required_capabilities = requirement.required_capabilities;
    output.observed_capabilities = snapshot.observed_capabilities;
    output.missing_capabilities = requirement.required_capabilities
        & ~snapshot.observed_capabilities;
    output.required_operations = requirement.required_operations;
    output.capability_generation = snapshot.capability_generation;
    output.plan_generation = snapshot.plan_generation;
    const uint8_t domain = static_cast<uint8_t>(requirement.domain);
    if (domain >= 5) {
        output.state = ActionAdmissionState::kUnsupported;
        output.reason = ActionAdmissionReason::kAdapterUnsupported;
        return output;
    }
    const DomainCapabilityState& observed = snapshot.domains[domain];
    output.observed_operations = observed.observed_operations;
    output.missing_operations = requirement.required_operations
        & ~observed.observed_operations;
    output.probe_error = observed.probe_error;
    if (!requirement.intent_enabled) {
        output.reason = ActionAdmissionReason::kIntentDisabled;
        return output;
    }
    if (snapshot.plan_generation != expected_plan_generation
        || snapshot.capability_generation == 0) {
        output.reason = ActionAdmissionReason::kGenerationStale;
        return output;
    }
    if (observed.state == AdapterState::kUnsupported) {
        output.state = ActionAdmissionState::kUnsupported;
        output.reason = ActionAdmissionReason::kAdapterUnsupported;
        return output;
    }
    if (observed.state != AdapterState::kActive) {
        output.reason = ActionAdmissionReason::kAdapterInactive;
        return output;
    }
    if (output.missing_capabilities != 0) {
        output.state = ActionAdmissionState::kUnsupported;
        output.reason = ActionAdmissionReason::kCapabilityMissing;
        return output;
    }
    if (output.missing_operations != 0) {
        output.state = ActionAdmissionState::kUnsupported;
        output.reason = ActionAdmissionReason::kOperationMissing;
        return output;
    }
    output.state = ActionAdmissionState::kActive;
    output.reason = ActionAdmissionReason::kNone;
    return output;
}

}  // namespace pathguard
