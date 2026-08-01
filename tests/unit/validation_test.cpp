#include <string>
#include <vector>

#include "pathguard/policy_v6.h"
#include "test_assert.h"

namespace {

bool Encodes(pathguard::PolicyV6 policy) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    return pathguard::EncodePolicyV6(policy, &bytes, &error);
}

pathguard::PolicyV6 ValidPolicy() {
    using namespace pathguard;
    PolicyV6 policy;
    PolicyPackageV6 package;
    package.package = "com.example.app";
    package.users = {0};
    package.selectors.push_back({
        PolicyMatchKind::kLiteralPrefix,
        PolicyObjectType::kDirectory,
        "Pictures",
    });
    PolicyActionV6 action;
    action.selector_index = 0;
    action.kind = PolicyActionKind::kRedirect;
    action.domain = PolicyExecutionDomain::kMount;
    action.target = "Sandbox/Pictures";
    action.preserve = PolicyPreserveMode::kRelative;
    action.collision = PolicyCollisionMode::kReject;
    action.reverse = PolicyReverseMode::kStaticUnique;
    package.actions.push_back(action);
    policy.packages.push_back(package);
    return policy;
}

}  // namespace

int main() {
    using namespace pathguard;
    assert(Encodes(ValidPolicy()));

    PolicyV6 missing_package = ValidPolicy();
    missing_package.packages.front().package.clear();
    assert(!Encodes(std::move(missing_package)));

    PolicyV6 missing_target = ValidPolicy();
    missing_target.packages.front().actions.front().target.clear();
    assert(!Encodes(std::move(missing_target)));

    PolicyV6 invalid_selector = ValidPolicy();
    invalid_selector.packages.front().actions.front().selector_index = 2;
    assert(!Encodes(std::move(invalid_selector)));

    PolicyV6 invalid_domain = ValidPolicy();
    invalid_domain.packages.front().actions.front().domain =
        static_cast<PolicyExecutionDomain>(255);
    assert(!Encodes(std::move(invalid_domain)));
    return 0;
}
