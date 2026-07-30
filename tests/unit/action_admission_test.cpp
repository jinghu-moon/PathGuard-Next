#include "pathguard/action_admission.h"
#include "pathguard/pattern_runtime.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    static_assert(kCapabilityProviderCallerUid == (UINT64_C(1) << 16));
    static_assert(kCapabilityProviderQueryInsertMapping == (UINT64_C(1) << 17));
    static_assert(kCapabilityFuseCompletePath == (UINT64_C(1) << 18));
    static_assert(kCapabilityAppPathAdapter == (UINT64_C(1) << 19));
    static_assert(kKnownOperationMaskV1 == UINT64_C(0x007fffff));

    CapabilitySnapshot snapshot;
    snapshot.capability_generation = 4;
    snapshot.plan_generation = 7;
    snapshot.observed_capabilities = kCapabilityProviderCallerUid
        | kCapabilityProviderQueryInsertMapping;
    snapshot.domains[static_cast<unsigned>(AdmissionDomain::kProvider)] = {
        AdapterState::kActive, kProviderCompositeOperationsV1, 0};
    const ActionRequirement provider{
        AdmissionDomain::kProvider,
        kCapabilityProviderCallerUid | kCapabilityProviderQueryInsertMapping,
        kProviderCompositeOperationsV1,
        true,
    };
    const ActionAdmission admitted = AdmitAction(provider, snapshot, 7);
    assert(admitted.active());

    snapshot.observed_capabilities &= ~kCapabilityProviderQueryInsertMapping;
    const ActionAdmission missing_capability = AdmitAction(provider, snapshot, 7);
    assert(missing_capability.state == ActionAdmissionState::kUnsupported);
    assert(missing_capability.reason == ActionAdmissionReason::kCapabilityMissing);
    assert(missing_capability.missing_capabilities
           == kCapabilityProviderQueryInsertMapping);

    snapshot.observed_capabilities |= kCapabilityProviderQueryInsertMapping;
    snapshot.domains[static_cast<unsigned>(AdmissionDomain::kProvider)]
        .observed_operations &= ~kOperationRename;
    const ActionAdmission missing_operation = AdmitAction(provider, snapshot, 7);
    assert(missing_operation.state == ActionAdmissionState::kUnsupported);
    assert(missing_operation.reason == ActionAdmissionReason::kOperationMissing);
    assert(missing_operation.missing_operations == kOperationRename);

    snapshot.domains[static_cast<unsigned>(AdmissionDomain::kProvider)]
        .observed_operations |= kOperationRename;
    assert(AdmitAction(provider, snapshot, 8).reason
           == ActionAdmissionReason::kGenerationStale);
    ActionRequirement disabled = provider;
    disabled.intent_enabled = false;
    assert(AdmitAction(disabled, snapshot, 7).reason
           == ActionAdmissionReason::kIntentDisabled);

    using namespace pathguard::pattern;
    PatternPlan plan;
    plan.plan_generation = 7;
    PlanAction mount;
    mount.domain = ExecutionDomain::kMount;
    PlanAction dynamic;
    dynamic.domain = ExecutionDomain::kProvider;
    dynamic.required_capabilities = provider.required_capabilities;
    dynamic.required_operations = provider.required_operations;
    plan.actions = {mount, dynamic};
    snapshot.domains[static_cast<unsigned>(AdmissionDomain::kMount)].state =
        AdapterState::kActive;
    AdmitPatternPlan(&plan, snapshot);
    assert(plan.actions[0].active);
    assert(plan.actions[1].active);

    snapshot.observed_capabilities &= ~kCapabilityProviderCallerUid;
    AdmitPatternPlan(&plan, snapshot);
    assert(plan.actions[0].active);
    assert(!plan.actions[1].active);
    return 0;
}
