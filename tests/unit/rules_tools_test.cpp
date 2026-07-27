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

    const RulesBuildResult redundant = Compile(
        "format = 1\n[compatibility]\nallow_legacy_mount=true\n"
        "[apps.\"com.example.app\"]\n"
        "redirect=[\"A\" -> \"B\",\"A\" -> \"B\"]\n");
    assert(redundant.ok());
    const auto redundant_lint = LintRules(redundant, RulesLimits{});
    assert(HasCode(redundant_lint, kRuleRedundant));
    assert(HasCode(redundant_lint, kLintLegacy));

    const RulesBuildResult unicode = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect=[\"Path/A\" -> \"Path/\xef\xbc\xa1\"]\n");
    assert(unicode.ok());
    assert(HasCode(LintRules(unicode, RulesLimits{}), kLintUnicode));

    const RulesBuildResult before = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect=[\"A\" -> \"B\",\"C\" -> \"D\"]\n");
    const RulesBuildResult after = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect=[\"A\" -> \"X\",\"E\" -> \"F\"]\n");
    assert(before.ok() && after.ok());
    const auto plan = BuildPolicyPlan(*before.canonical, *after.canonical);
    assert(plan.size() == 3);
    assert(plan[0].kind == PolicyChangeKind::kModify);
    assert(plan[0].source == "A" && plan[0].before_target == "B"
           && plan[0].after_target == "X");
    assert(plan[1].kind == PolicyChangeKind::kRemove && plan[1].source == "C");
    assert(plan[2].kind == PolicyChangeKind::kAdd && plan[2].source == "E");

    const RulesBuildResult overlap = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect=[\"A\" -> \"X\",\"A/B\" -> \"Y\"]\n");
    assert(!overlap.ok());
    assert(overlap.resolved.has_value());
    const PathExplanation explanation = ExplainPath(
        *overlap.resolved, "com.example.app", "A/B/file", RulesLimits{});
    assert(explanation.source == "A/B");
    assert(explanation.target == "Y");
    assert(explanation.shadowed_parents.size() == 1);
    assert(explanation.shadowed_parents.front() == "A");
    return 0;
}
