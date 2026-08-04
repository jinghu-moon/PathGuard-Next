#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/policy_format.h"
#include "pathguard/policy_v6.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using namespace pathguard::rules;

RulesBuildResult Compile(std::string text) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(text),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return CompileRules(*source, RulesLimits{});
}

bool HasCode(const RulesBuildResult& result, std::string_view code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const Diagnostic& value) { return value.code == code; });
}

}  // namespace

int main() {
    using namespace pathguard;
    using namespace pathguard::rules;

    const RulesBuildResult literal = Compile(R"(format = 2
[compatibility]
allow_legacy_mount = true
[apps."org.localsend.localsend_app"]
users = [0]
redirect_rules = [
  { select = { root = "Download", glob = "localsend-source", type = "directory" }, to = "Download/localsend-redirect" },
]
)");
    assert(literal.ok());
    assert(literal.blob->bytes[4] == binary_format::kFormatVersion);
    assert(literal.blob->bytes[6] == binary_format::kSchemaVersion);
    PolicyV6 literal_policy;
    const auto literal_decode = DecodePolicyV6(literal.blob->bytes, &literal_policy);
    assert(literal_decode.ok);
    assert(literal_policy.packages.front().selectors.front().root
           == "Download/localsend-source");
    assert(literal_policy.packages.front().actions.front().domain
           == PolicyExecutionDomain::kMount);
    assert(VerifyPolicyBlob(*literal.policy_v6, *literal.blob));

    const RulesBuildResult glob = Compile(R"(format = 2
[apps."org.localsend.localsend_app"]
users = [0]
provider = { enabled = true }
redirect_rules = [
  { select = { root = "Pictures", glob = "**/IMG_[0-9]?.jpg", except = ["private/**", "**/thumbnail-*/**"], type = "file" }, to = "Download/images", priority = 7, enforcement = "provider" },
]
)");
    assert(glob.ok());
    PolicyV6 glob_policy;
    assert(DecodePolicyV6(glob.blob->bytes, &glob_policy).ok);
    const auto& glob_selector = glob_policy.packages.front().selectors.front();
    const auto& glob_action = glob_policy.packages.front().actions.front();
    assert(glob_selector.match_kind == PolicyMatchKind::kGlob);
    assert(glob_selector.except_patterns.size() == 2);
    assert(glob_action.domain == PolicyExecutionDomain::kProvider);
    assert(glob_action.required_capabilities
           == kCapabilityProviderCallerUid);
    assert(glob_action.required_operations
           == (kOperationOpenRead | kOperationOpenWrite | kOperationCreate
               | kOperationLookupStat | kOperationAccess | kOperationRename
               | kOperationUnlink));
    assert(glob_action.reverse == PolicyReverseMode::kNone);
    assert((glob_action.options
            & binary_format::kActionOptionPrivateAudit) == 0);
    DeviceSnapshot provider_unobserved;
    provider_unobserved.topology_supported = true;
    const AdmissionResult deferred = AdmitPolicy(
        *glob.policy_v6, glob.requirements, provider_unobserved);
    assert(deferred.admitted);  // Dynamic action admission is process-local.

    const RulesBuildResult app_path = Compile(R"(format = 2
[apps."org.localsend.localsend_app"]
users = [0]
provider = { enabled = true }
redirect_rules = [
  { select = { root = "Download", glob = "localsend-source/**", type = "file" }, to = "Download/localsend-redirect" },
]
)");
    assert(app_path.ok());
    const auto& app_path_action =
        app_path.policy_v6->packages.front().actions.front();
    assert(app_path_action.domain == PolicyExecutionDomain::kAppPath);
    assert(app_path_action.required_capabilities == kCapabilityAppPathAdapter);
    assert(app_path_action.required_operations
           == kAppPathFileRedirectOperationsV1);
    assert((app_path_action.required_operations & kOperationTruncate) == 0);
    assert((app_path_action.required_operations & kOperationWatch) == 0);
    assert((app_path_action.required_operations & ~UINT64_C(0x0000bfff)) == 0);

    const RulesBuildResult reordered = Compile(R"(format=2
[apps."org.localsend.localsend_app"]
provider={enabled=true}
users=[0]
redirect_rules=[{to="Download/images",enforcement="provider",priority=7,select={type="file",except=["**/thumbnail-*/**","private/**"],glob="**/IMG_[0-9]?.jpg",root="Pictures"}}]
)");
    assert(reordered.ok());
    assert(reordered.blob->bytes == glob.blob->bytes);

    const RulesBuildResult brace_same_source = Compile(R"(format = 2
[apps."com.example.brace"]
users = [0]
redirect_rules = [
  { select = { root = "Pictures", glob = "**/*.{jpg,png}", type = "file" }, to = "Download/images" },
]
)");
    assert(brace_same_source.ok());
    assert(brace_same_source.policy_v6->packages.front().actions.size() == 2);
    for (const auto& action : brace_same_source.policy_v6->packages.front().actions) {
        assert(action.reverse == PolicyReverseMode::kNone);
    }

    const RulesBuildResult distinct_sources = Compile(R"(format = 2
[apps."com.example.multi"]
users = [0]
redirect_rules = [
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" }, to = "Download/images" },
  { select = { root = "DCIM", glob = "**/*.jpg", type = "file" }, to = "Download/images" },
]
)");
    assert(distinct_sources.ok());
    std::vector<std::string> flat_targets;
    for (const auto& action : distinct_sources.policy_v6->packages.front().actions) {
        assert(action.reverse == PolicyReverseMode::kNone);
        assert(action.target == "Download/images");
        flat_targets.push_back(action.target);
    }
    assert(flat_targets.size() == 2);
    assert(flat_targets[0] == flat_targets[1]);

    const RulesBuildResult audited = Compile(R"(format = 2
[apps."com.example.audit"]
users = [0]
redirect_rules = [
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" }, to = "Download/images", audit = true },
]
)");
    assert(audited.ok());
    const auto& audited_action =
        audited.policy_v6->packages.front().actions.front();
    assert(audited_action.reverse == PolicyReverseMode::kNone);
    assert((audited_action.options
            & binary_format::kActionOptionPrivateAudit) != 0);
    assert(audited_action.required_operations
           == kAppPathFileRedirectOperationsV1);

    const RulesBuildResult audited_app_and_provider = Compile(R"(format = 2
[apps."com.example.audit"]
users = [0]
redirect_rules = [
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" }, to = "Download/images" },
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" }, to = "Download/images", enforcement = "provider", audit = true },
]
)");
    assert(audited_app_and_provider.ok());
    bool found_plain_app_path = false;
    bool found_audited_provider = false;
    for (const auto& action :
         audited_app_and_provider.policy_v6->packages.front().actions) {
        const bool private_audit = (action.options
            & binary_format::kActionOptionPrivateAudit) != 0;
        found_plain_app_path |=
            action.domain == PolicyExecutionDomain::kAppPath && !private_audit;
        found_audited_provider |=
            action.domain == PolicyExecutionDomain::kProvider && private_audit;
    }
    assert(found_plain_app_path);
    assert(found_audited_provider);

    const RulesBuildResult invalid_audit = Compile(R"(format = 2
[apps."com.example.audit"]
users = [0]
redirect_rules = [
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" }, to = "Download/images", audit = "yes" },
]
    )");
    assert(!invalid_audit.ok());
    assert(HasCode(invalid_audit, kTypeMismatch));

    const RulesBuildResult distinct_sources_reordered = Compile(R"(format = 2
[apps."com.example.multi"]
users = [0]
redirect_rules = [
  { select = { root = "DCIM", glob = "**/*.jpg", type = "file" }, to = "Download/images" },
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" }, to = "Download/images" },
]
)");
    assert(distinct_sources_reordered.ok());
    assert(distinct_sources_reordered.blob->bytes
           == distinct_sources.blob->bytes);

    const RulesBuildResult distinct_sources_priority = Compile(R"(format = 2
[apps."com.example.multi"]
users = [0]
redirect_rules = [
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" }, to = "Download/images", priority = 19 },
  { select = { root = "DCIM", glob = "**/*.jpg", type = "file" }, to = "Download/images" },
]
)");
    assert(distinct_sources_priority.ok());
    std::vector<std::string> priority_targets;
    for (const auto& action :
         distinct_sources_priority.policy_v6->packages.front().actions) {
        priority_targets.push_back(action.target);
    }
    std::sort(flat_targets.begin(), flat_targets.end());
    std::sort(priority_targets.begin(), priority_targets.end());
    assert(priority_targets == flat_targets);

    const RulesBuildResult cross_domain = Compile(R"(format = 2
[apps."org.localsend.localsend_app"]
users = [0]
provider = { enabled = true }
redirect_rules = [
  { select = { root = "Download", glob = "localsend-source", type = "directory" }, to = "Download/localsend-redirect" },
  { select = { root = "Download/localsend-source", glob = "**", type = "file" }, to = "Download/localsend-redirect", enforcement = "provider" },
  { select = { root = "Pictures", glob = "**", type = "file" }, to = "Download/localsend-redirect", enforcement = "provider" },
]
)");
    assert(cross_domain.ok());
    std::string mount_target;
    std::string download_provider_target;
    std::string pictures_provider_target;
    const auto& cross_package = cross_domain.policy_v6->packages.front();
    for (const auto& action : cross_package.actions) {
        const std::string& root =
            cross_package.selectors[action.selector_index].root;
        assert(action.target == "Download/localsend-redirect");
        assert(action.reverse == PolicyReverseMode::kNone);
        if (action.domain == PolicyExecutionDomain::kMount) {
            mount_target = action.target;
        } else if (root == "Download/localsend-source") {
            download_provider_target = action.target;
        } else if (root == "Pictures") {
            pictures_provider_target = action.target;
        }
    }
    assert(!mount_target.empty());
    assert(mount_target == download_provider_target);
    assert(mount_target == pictures_provider_target);

    const RulesBuildResult user_owned_pg_path = Compile(R"(format = 2
[apps."com.example.reserved"]
users = [0]
redirect_rules = [
  { select = { root = "Pictures/_pg", glob = "**", type = "file" }, to = "Download/out" },
]
)");
    assert(user_owned_pg_path.ok());

    const RulesBuildResult old = Compile(
        "format = 1\n[apps.\"com.example.app\"]\nredirect=[]\n");
    assert(!old.ok());
    assert(HasCode(old, kFormatUnsupported));
    return 0;
}
