#include <algorithm>
#include <string>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules/tools.h"
#include "test_assert.h"

namespace {

pathguard::rules::RulesBuildResult Compile(std::string text) {
    using namespace pathguard::rules;
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(text),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return CompileRules(*source, RulesLimits{});
}

bool HasCode(const std::vector<pathguard::rules::Diagnostic>& diagnostics,
             std::string_view code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const auto& item) { return item.code == code; });
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    const RulesBuildResult redundant = Compile(R"(format = 2
[compatibility]
allow_legacy_mount = true
[apps."com.example.app"]
redirect_rules = [
  { select = { root = "Root", glob = "A", type = "any" }, to = "B" },
  { select = { root = "Root", glob = "A", type = "any" }, to = "B" },
]
)");
    assert(redundant.ok());
    const auto redundant_lint = LintRules(redundant, RulesLimits{});
    assert(HasCode(redundant_lint, kRuleRedundant));
    assert(HasCode(redundant_lint, kLintLegacy));

    const RulesBuildResult unicode = Compile(
        "format = 2\n[apps.\"com.example.app\"]\n"
        "redirect_rules=["
        "{select={root=\"Path\",glob=\"A\",type=\"any\"},to=\"Path/A\"},"
        "{select={root=\"Path\",glob=\"B\",type=\"any\"},"
        "to=\"Path/\xef\xbc\xa1\"}]\n");
    assert(unicode.ok());
    assert(HasCode(LintRules(unicode, RulesLimits{}), kLintUnicode));

    const RulesBuildResult before = Compile(R"(format = 2
[apps."com.example.app"]
redirect_rules = [
  { select = { root = "Root", glob = "A", type = "any" }, to = "B" },
  { select = { root = "Root", glob = "C", type = "any" }, to = "D" },
]
)");
    const RulesBuildResult after = Compile(R"(format = 2
[apps."com.example.app"]
redirect_rules = [
  { select = { root = "Root", glob = "A", type = "any" }, to = "X" },
  { select = { root = "Root", glob = "E", type = "any" }, to = "F" },
]
)");
    assert(before.ok() && after.ok());
    const auto plan = BuildPolicyPlan(*before.canonical_v2, *after.canonical_v2);
    assert(plan.size() == 3);
    assert(plan[0].kind == PolicyChangeKind::kModify);
    assert(plan[0].source == "Root/A" && plan[0].before_target == "B"
           && plan[0].after_target == "X");
    assert(plan[1].kind == PolicyChangeKind::kRemove
           && plan[1].source == "Root/C");
    assert(plan[2].kind == PolicyChangeKind::kAdd
           && plan[2].source == "Root/E");

    const RulesBuildResult deny_before = Compile(R"(format = 2
[apps."com.example.app"]
deny_rules = [ { select = { root = "Root", glob = "Private", type = "any" } } ]
redirect_rules = [ { select = { root = "Root", glob = "A", type = "any" }, to = "B" } ]
)");
    const RulesBuildResult deny_after = Compile(R"(format = 2
[apps."com.example.app"]
deny_rules = [ { select = { root = "Root", glob = "Secret", type = "any" } } ]
redirect_rules = [ { select = { root = "Root", glob = "A", type = "any" }, to = "B" } ]
)");
    assert(deny_before.ok() && deny_after.ok());
    const auto deny_plan = BuildPolicyPlan(
        *deny_before.canonical_v2, *deny_after.canonical_v2);
    assert(deny_plan.size() == 2);
    assert(deny_plan[0].kind == PolicyChangeKind::kRemove);
    assert(deny_plan[0].rule_kind == PolicyRuleKind::kDeny);
    assert(deny_plan[0].source == "Root/Private");
    assert(deny_plan[1].kind == PolicyChangeKind::kAdd);
    assert(deny_plan[1].rule_kind == PolicyRuleKind::kDeny);
    assert(deny_plan[1].source == "Root/Secret");

    const RulesBuildResult overlap = Compile(R"(format = 2
[apps."com.example.app"]
redirect_rules = [
  { select = { root = "Root", glob = "A", type = "any" }, to = "X" },
  { select = { root = "Root/A", glob = "B", type = "any" }, to = "Y" },
]
)");
    assert(overlap.ok());
    const PathExplanation explanation = ExplainPath(
        *overlap.canonical_v2, "com.example.app", "Root/A/B/file", RulesLimits{});
    assert(explanation.source == "Root/A/B");
    assert(explanation.target == "Y");
    assert(explanation.shadowed_parents.size() == 1);
    assert(explanation.shadowed_parents.front() == "Root/A");

    const PathExplanation denied = ExplainPath(
        *deny_after.canonical_v2, "com.example.app", "Root/Secret/file",
        RulesLimits{});
    assert(denied.action == PolicyRuleKind::kDeny);
    assert(denied.source == "Root/Secret");
    assert(!denied.target.has_value());
    return 0;
}
