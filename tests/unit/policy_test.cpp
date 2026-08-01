#include <string>

#include "pathguard/policy_v6.h"
#include "test_assert.h"

namespace {

pathguard::PolicyV6 MakePolicy(bool provider_enabled) {
    using namespace pathguard;
    PolicyV6 policy;
    PolicyPackageV6 package;
    package.package = "com.example.app";
    package.users = {0};
    package.all_processes = true;
    package.provider_enabled = provider_enabled;
    package.selectors.push_back({
        PolicyMatchKind::kLiteralPrefix,
        PolicyObjectType::kDirectory,
        "Download/Source",
    });
    PolicyActionV6 action;
    action.selector_index = 0;
    action.kind = PolicyActionKind::kRedirect;
    action.domain = PolicyExecutionDomain::kMount;
    action.target = "PathGuard/Target";
    action.preserve = PolicyPreserveMode::kRelative;
    action.collision = PolicyCollisionMode::kReject;
    action.reverse = PolicyReverseMode::kStaticUnique;
    package.actions.push_back(action);
    policy.packages.push_back(package);
    return policy;
}

}  // namespace

int main() {
    const pathguard::PolicyV6 policy = MakePolicy(false);
    const pathguard::PolicyV6 provider_policy = MakePolicy(true);
    assert(pathguard::ComputePolicyV6ContentGeneration(policy) != 0);
    assert(pathguard::ComputePolicyV6ContentGeneration(policy)
           != pathguard::ComputePolicyV6ContentGeneration(provider_policy));
    assert(pathguard::ComputePolicyV6PlanGeneration(
               policy.packages.front(), policy.allow_legacy_mount)
           != pathguard::ComputePolicyV6PlanGeneration(
               provider_policy.packages.front(),
               provider_policy.allow_legacy_mount));
    return 0;
}
