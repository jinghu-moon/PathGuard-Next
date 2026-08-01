#pragma once

#include <stdint.h>

#include "pathguard/policy_v6_view.h"
#include "pathguard/runtime_status.h"

namespace pathguard {

inline RuntimeActionKind RuntimeKindFromPolicy(uint8_t kind) {
    switch (kind) {
        case 0: return RuntimeActionKind::kDeny;
        case 1: return RuntimeActionKind::kRedirect;
        case 2: return RuntimeActionKind::kObserve;
        case 3: return RuntimeActionKind::kExport;
        default: return RuntimeActionKind::kUnknown;
    }
}

inline bool AppendPackageRuntimeActions(
        const policy_v6_view::PolicyV6View& policy,
        const policy_v6_view::PackageRef& package,
        AdmissionDomain domain, const CapabilitySnapshot& capabilities,
        RuntimeStatusRecord* status) {
    if (!policy.valid() || status == nullptr) return false;
    CapabilitySnapshot scoped_capabilities = capabilities;
    if (scoped_capabilities.plan_generation == 0) {
        scoped_capabilities.plan_generation = package.plan_generation;
    }
    for (uint32_t index = 0; index < package.action_count; ++index) {
        policy_v6_view::ActionRef action;
        if (!policy.ActionAt(package.first_action + index, &action)) return false;
        if (action.domain != static_cast<uint8_t>(domain)) continue;
        RuntimeActionStatus runtime;
        runtime.kind = RuntimeKindFromPolicy(action.kind);
        runtime.domain = domain;
        runtime.intent_enabled = true;
        runtime.action_mask = action.required_operations;
        runtime.rule_id = action.rule_id;
        runtime.selector_id = action.selector_id;
        runtime.admission = AdmitAction({
            domain,
            action.required_capabilities,
            action.required_operations,
            true,
        }, scoped_capabilities, package.plan_generation);
        AppendRuntimeAction(status, runtime);
    }
    return true;
}

}  // namespace pathguard
