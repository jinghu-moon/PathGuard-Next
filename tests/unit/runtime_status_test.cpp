#include "pathguard/runtime_status.h"
#include "pathguard/runtime_status_builder.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    pathguard::RuntimeStatusRecord status;
    assert(status.version == kRuntimeStatusVersion);
    assert(status.enforcement == pathguard::EnforcementState::kInactive);
    assert(status.transaction == pathguard::TransactionOutcome::kNone);
    assert(status.security == pathguard::SecurityLevel::kNone);
    assert(status.reason == pathguard::RuntimeReason::kNone);
    assert(status.action_count == 0);
    assert(status.action_total == 0);
    assert(!status.actions_truncated);

    ActionAdmission admission;
    admission.state = ActionAdmissionState::kUnsupported;
    admission.reason = ActionAdmissionReason::kCapabilityMissing;
    admission.required_capabilities = UINT64_C(0x30);
    admission.observed_capabilities = UINT64_C(0x10);
    admission.missing_capabilities = UINT64_C(0x20);
    admission.required_operations = UINT64_C(0x07);
    admission.observed_operations = UINT64_C(0x03);
    admission.missing_operations = UINT64_C(0x04);
    admission.capability_generation = 4;
    admission.plan_generation = 7;
    admission.probe_error = 95;
    RuntimeActionStatus action{
        RuntimeActionKind::kRedirect,
        AdmissionDomain::kProvider,
        true,
        0,
        UINT32_C(0x2),
        UINT64_C(0x1234),
        9,
        11,
        admission,
    };
    assert(AppendRuntimeAction(&status, action));
    assert(status.action_count == 1 && status.action_total == 1);
    assert(status.actions[0].admission.missing_capabilities == UINT64_C(0x20));
    assert(status.actions[0].rule_id == 9);
    assert(status.actions[0].selector_id == 11);

    for (uint32_t index = 1; index < kMaxRuntimeActionStatus; ++index) {
        action.rule_id = index;
        assert(AppendRuntimeAction(&status, action));
    }
    assert(!AppendRuntimeAction(&status, action));
    assert(status.action_count == kMaxRuntimeActionStatus);
    assert(status.action_total == kMaxRuntimeActionStatus + 1);
    assert(status.actions_truncated);

    status.counters.hazard_slot_acquire_fail_total = 2;
    status.counters.snapshot_reload_rejected_retire_limit_total = 3;
    status.counters.event_overflow_total = 4;
    status.counters.diagnostic_drop_total = 5;
    assert(status.counters.event_overflow_total == 4);

    status.enforcement = pathguard::EnforcementState::kFailed;
    status.transaction = pathguard::TransactionOutcome::kNamespaceTainted;
    status.reason = pathguard::RuntimeReason::kOwnerDeath;
    assert(status.enforcement != pathguard::EnforcementState::kActive);
    assert(status.transaction != pathguard::TransactionOutcome::kRollbackComplete);

    using namespace pathguard::rules;
    Diagnostic error;
    const auto source = SourceBuffer::Create("runtime-status.toml", R"(format = 2
[apps."org.localsend.localsend_app"]
users = [0]
provider = { enabled = true }
redirect_rules = [
  { select = { root = "Download/localsend-source", glob = "**", type = "file" }, to = "Download/localsend-redirect", enforcement = "provider" },
]
)", RulesLimits{}, &error);
    assert(source.has_value());
    const RulesBuildResult built = CompileRules(*source, RulesLimits{});
    assert(built.ok());
    policy_v6_view::PolicyV6View policy;
    assert(policy.Initialize(built.blob->bytes.data(), built.blob->bytes.size()));
    policy_v6_view::PackageRef package;
    assert(policy.FindPackage("org.localsend.localsend_app",
                              sizeof("org.localsend.localsend_app") - 1,
                              &package));
    CapabilitySnapshot capabilities;
    capabilities.capability_generation = 1;
    capabilities.observed_capabilities = kCapabilityProviderCallerUid;
    capabilities.domains[static_cast<unsigned>(AdmissionDomain::kProvider)] = {
        AdapterState::kActive, kProviderCompositeOperationsV1, 0};
    RuntimeStatusRecord provider_status;
    assert(AppendPackageRuntimeActions(policy, package,
        AdmissionDomain::kProvider, capabilities, &provider_status));
    assert(provider_status.action_count == 1);
    assert(provider_status.actions[0].kind == RuntimeActionKind::kRedirect);
    assert(provider_status.actions[0].domain == AdmissionDomain::kProvider);
    assert(provider_status.actions[0].rule_id != 0);
    assert(provider_status.actions[0].action_mask
           == provider_status.actions[0].admission.required_operations);
    assert(provider_status.actions[0].admission.active());
    return 0;
}
