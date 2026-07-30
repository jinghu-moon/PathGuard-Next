#include <string_view>
#include <vector>

#include "pathguard/pattern.h"
#include "pathguard/policy_pattern_runtime.h"
#include "pathguard/policy_v6.h"
#include "test_assert.h"

namespace {

void Compare(std::string_view glob, std::string_view path) {
    using namespace pathguard;
    const auto compiled = pattern::CompilePattern(glob);
    assert(compiled.ok());
    PolicyV6 policy;
    PolicyPackageV6 package;
    package.package = "com.example.app";
    package.all_users = true;
    package.all_processes = true;
    PolicySelectorV6 selector;
    selector.match_kind = PolicyMatchKind::kGlob;
    selector.root = "Pictures";
    selector.base_pattern = *compiled.program;
    package.selectors.push_back(selector);
    PolicyActionV6 action;
    action.kind = PolicyActionKind::kRedirect;
    action.domain = PolicyExecutionDomain::kAppPath;
    action.target = "Download/Target";
    action.required_capabilities = kCapabilityAppPathAdapter;
    action.required_operations = kAppPathOperationsV1;
    package.actions.push_back(action);
    policy.packages.push_back(package);
    std::vector<std::uint8_t> bytes;
    std::string error;
    assert(EncodePolicyV6(policy, &bytes, &error));
    policy_v6_view::PolicyV6View view;
    assert(view.Initialize(bytes.data(), bytes.size()));
    policy_v6_view::PackageRef package_ref;
    policy_v6_view::SelectorRef selector_ref;
    assert(view.PackageAt(0, &package_ref));
    assert(view.SelectorAt(package_ref.first_selector, &selector_ref));
    if (glob == "**") {
        assert(policy_pattern_runtime::IsUniversalDescendantPattern(
            view, selector_ref));
    }
    policy_pattern_runtime::MatchScratch raw_scratch;
    const auto raw = policy_pattern_runtime::MatchPattern(
        view, selector_ref.base_pattern_id, path.data(), path.size(),
        &raw_scratch);
    pattern::PatternMatchScratch host_scratch;
    const auto host = pattern::MatchPattern(*compiled.program, path,
                                            &host_scratch);
    assert((raw == policy_pattern_runtime::MatchResult::kMatch)
           == (host == pattern::PatternMatchResult::kMatch));
}

}  // namespace

int main() {
    Compare("*.jpg", "a.jpg");
    Compare("*.jpg", "Album/a.jpg");
    Compare("IMG_?.jp[ge]", "IMG_1.jpg");
    Compare("IMG_?.jp[ge]", "IMG_10.jpg");
    Compare("[^abc]*", "x-file");
    Compare("[!abc]*", "a-file");
    Compare("**/*.jpg", "a.jpg");
    Compare("**/*.jpg", "A/B/a.jpg");
    Compare("a/**", "a");
    Compare("a/**", "a/b");
    Compare("**/leaf", "leaf");
    Compare("**/leaf", "A/B/leaf");
    Compare("**/private/**", "Trips/private/secret.jpg");
    Compare("文件-?.png", "文件-一.png");
    return 0;
}
