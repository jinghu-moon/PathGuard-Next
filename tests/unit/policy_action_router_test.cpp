#include <string>

#include "pathguard/policy_action_router.h"
#include "pathguard/storage_path_adapter.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    using namespace pathguard::rules;
    Diagnostic error;
    auto source = SourceBuffer::Create("router.toml", R"(format = 2
[apps."com.example.app"]
users = [0]
provider = { enabled = true }
deny_rules = [{ select = { root = "Pictures", glob = "**/private/**", type = "file" }, enforcement = "provider", priority = 100 }]
redirect_rules = [{ select = { root = "Pictures", glob = "**/*.{jpg,png}", except = ["**/thumbnail-*/**"], type = "file" }, to = "Download/Images", enforcement = "provider" }]
)", RulesLimits{}, &error);
    assert(source.has_value());
    const RulesBuildResult built = CompileRules(*source, RulesLimits{});
    assert(built.ok());
    bool has_provider_deny = false;
    for (const auto& action : built.policy_v6->packages.front().actions) {
        has_provider_deny = has_provider_deny
            || (action.kind == PolicyActionKind::kDeny
                && action.domain == PolicyExecutionDomain::kProvider);
    }
    assert(has_provider_deny);
    policy_v6_view::PolicyV6View policy;
    assert(policy.Initialize(built.blob->bytes.data(), built.blob->bytes.size()));
    policy_v6_view::PackageRef package;
    assert(policy.FindPackage("com.example.app", sizeof("com.example.app") - 1,
                              &package));
    CapabilitySnapshot capabilities;
    capabilities.capability_generation = 4;
    capabilities.plan_generation = package.plan_generation;
    capabilities.observed_capabilities = kCapabilityProviderCallerUid
        | kCapabilityProviderQueryInsertMapping;
    capabilities.domains[static_cast<unsigned>(AdmissionDomain::kProvider)] = {
        AdapterState::kActive, kProviderCompositeOperationsV1, 0};
    policy_pattern_runtime::MatchScratch scratch;
    policy_action_router::Request request{
        package, "Pictures", sizeof("Pictures") - 1,
        "Trips/IMG_001.jpg", sizeof("Trips/IMG_001.jpg") - 1,
        1, AdmissionDomain::kProvider, kOperationOpenWrite,
    };
    auto result = policy_action_router::Route(policy, request, capabilities,
                                               &scratch);
    assert(result.disposition == policy_action_router::Disposition::kRedirect);
    assert(result.target.Equals("Download/Images", sizeof("Download/Images") - 1));
    assert(result.collision_mode == 1);
    assert(result.reverse_mode == 1);

    request.relative_path = "Trips/private/secret.jpg";
    request.relative_size = sizeof("Trips/private/secret.jpg") - 1;
    bool deny_selector_matches = false;
    std::uint64_t deny_rule_id = 0;
    for (std::uint32_t local = 0; local < package.selector_count; ++local) {
        policy_v6_view::SelectorRef selector;
        assert(policy.SelectorAt(package.first_selector + local, &selector));
        for (std::uint32_t action_index = 0;
             action_index < selector.action_count; ++action_index) {
            policy_v6_view::ActionRef action;
            assert(policy.ActionAt(selector.first_action + action_index, &action));
            if (action.kind == 0) {
                deny_rule_id = action.rule_id;
                policy_v6_view::StringRef deny_root;
                assert(policy.StringAt(selector.root_id, &deny_root));
                assert(deny_root.Equals(request.root, request.root_size));
                assert(selector.object_type == request.object_type);
                assert(policy_action_router::CandidateCacheMatches(
                    policy, selector, request));
                policy_pattern_runtime::MatchScratch direct_scratch;
                deny_selector_matches = policy_pattern_runtime::MatchPattern(
                    policy, selector.base_pattern_id, request.relative_path,
                    request.relative_size, &direct_scratch)
                    == policy_pattern_runtime::MatchResult::kMatch;
                const ActionRequirement deny_requirement{
                    AdmissionDomain::kProvider,
                    action.required_capabilities,
                    action.required_operations | kOperationOpenWrite,
                    true,
                };
                assert(AdmitAction(deny_requirement, capabilities,
                                   package.plan_generation).active());
            }
        }
    }
    assert(deny_selector_matches);
    result = policy_action_router::Route(policy, request, capabilities, &scratch);
    assert(result.disposition == policy_action_router::Disposition::kDeny);
    assert(result.rule_id == deny_rule_id);

    request.relative_path = "Trips/thumbnail-small/a.jpg";
    request.relative_size = sizeof("Trips/thumbnail-small/a.jpg") - 1;
    result = policy_action_router::Route(policy, request, capabilities, &scratch);
    assert(result.disposition == policy_action_router::Disposition::kPass);

    request.relative_path = "Trips/notes.txt";
    request.relative_size = sizeof("Trips/notes.txt") - 1;
    result = policy_action_router::Route(policy, request, capabilities, &scratch);
    assert(result.disposition == policy_action_router::Disposition::kPass);

    capabilities.observed_capabilities &= ~kCapabilityProviderCallerUid;
    request.relative_path = "Trips/IMG_002.jpg";
    request.relative_size = sizeof("Trips/IMG_002.jpg") - 1;
    result = policy_action_router::Route(policy, request, capabilities, &scratch);
    assert(result.disposition == policy_action_router::Disposition::kPass);
    assert(result.reason == policy_action_router::Reason::kCapabilityMissing);

    capabilities.observed_capabilities |= kCapabilityProviderCallerUid;
    const storage_path_adapter::PolicyScope scope{10001, 0, package.index};
    char rewritten[4096]{};
    auto absolute = storage_path_adapter::Rewrite(
        policy, &scope, 1, 10001,
        "/storage/emulated/0/Pictures/Trips/IMG_003.jpg",
        AdmissionDomain::kProvider, kOperationOpenWrite, 1,
        capabilities, &scratch, rewritten, sizeof(rewritten));
    assert(absolute.disposition
           == storage_path_adapter::RewriteDisposition::kRedirect);
    assert(std::string_view(rewritten)
           == "/storage/emulated/0/Download/Images/Trips/IMG_003.jpg");
    absolute = storage_path_adapter::Rewrite(
        policy, &scope, 1, 10002,
        "/storage/emulated/0/Pictures/Trips/IMG_003.jpg",
        AdmissionDomain::kProvider, kOperationOpenWrite, 1,
        capabilities, &scratch, rewritten, sizeof(rewritten));
    assert(absolute.disposition == storage_path_adapter::RewriteDisposition::kPass);
    absolute = storage_path_adapter::Rewrite(
        policy, &scope, 1, 10001,
        "/storage/emulated/0/Pictures/Trips/../escape.jpg",
        AdmissionDomain::kProvider, kOperationOpenWrite, 1,
        capabilities, &scratch, rewritten, sizeof(rewritten));
    assert(absolute.disposition == storage_path_adapter::RewriteDisposition::kPass);
    assert(absolute.rule_id == 0);
    return 0;
}
