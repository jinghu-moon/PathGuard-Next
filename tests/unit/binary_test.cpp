#include <cstdint>
#include <string>
#include <vector>

#include "pathguard/policy_format.h"
#include "pathguard/policy_v6.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;

    PolicyV6 policy;
    PolicyPackageV6 package;
    package.package = "org.localsend.localsend_app";
    package.users = {0};
    package.all_processes = true;
    package.provider_enabled = true;
    package.selectors.push_back({
        PolicyMatchKind::kLiteralPrefix,
        PolicyObjectType::kDirectory,
        "Download/localsend-source",
    });
    PolicyActionV6 action;
    action.selector_index = 0;
    action.kind = PolicyActionKind::kRedirect;
    action.domain = PolicyExecutionDomain::kMount;
    action.target = "Download/localsend-redirect";
    action.preserve = PolicyPreserveMode::kRelative;
    action.collision = PolicyCollisionMode::kReject;
    action.reverse = PolicyReverseMode::kStaticUnique;
    package.actions.push_back(action);
    policy.packages.push_back(package);

    std::vector<std::uint8_t> bytes;
    std::string error;
    assert(EncodePolicyV6(policy, &bytes, &error));
    assert(bytes[4] == binary_format::kFormatVersion);
    assert(bytes[6] == binary_format::kSchemaVersion);

    PolicyV6 decoded;
    const PolicyV6DecodeResult result = DecodePolicyV6(bytes, &decoded);
    assert(result.ok);
    assert(result.content_generation
           == ComputePolicyV6ContentGeneration(policy));
    assert(decoded.packages.front().provider_enabled);
    assert(decoded.packages.front().actions.front().target
           == "Download/localsend-redirect");

    std::vector<std::uint8_t> old = bytes;
    old[4] = 5;
    assert(!DecodePolicyV6(old, &decoded).ok);
    bytes[binary_format::kPayloadChecksumOffset] ^= 1;
    assert(!DecodePolicyV6(bytes, &decoded).ok);
    return 0;
}
