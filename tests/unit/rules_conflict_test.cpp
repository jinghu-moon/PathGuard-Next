#include <algorithm>
#include <string>
#include <string_view>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using namespace pathguard::rules;

RulesBuildResult Compile(std::string body) {
    Diagnostic error;
    auto source = SourceBuffer::Create(
        "rules.toml", "format = 2\n[apps.\"com.example.app\"]\n"
        + std::move(body), RulesLimits{}, &error);
    assert(source.has_value());
    return CompileRules(*source, RulesLimits{});
}

bool HasCode(const RulesBuildResult& result, std::string_view code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const Diagnostic& item) { return item.code == code; });
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    const RulesBuildResult duplicate = Compile(
        "redirect_rules=[{select={root=\"Root\",glob=\"A\",type=\"any\"},to=\"B\"},"
        "{select={root=\"Root\",glob=\"A\",type=\"any\"},to=\"B\"}]\n");
    assert(duplicate.ok());
    assert(duplicate.canonical_v2->apps.front().actions.size() == 1);
    assert(HasCode(duplicate, kRuleRedundant));

    const RulesBuildResult distinct = Compile(
        "redirect_rules=[{select={root=\"Root\",glob=\"A\",type=\"any\"},to=\"B\"},"
        "{select={root=\"Root/A\",glob=\"B\",type=\"any\"},to=\"C\"}]\n");
    assert(distinct.ok());
    assert(distinct.canonical_v2->apps.front().actions.size() == 2);
    assert(distinct.canonical_v2->apps.front().actions.front().selector.root
           == "Root");

    const RulesBuildResult invalid_target = Compile(
        "redirect_rules=[{select={root=\"Root\",glob=\"A\"},to=\"../bad\"}]\n");
    assert(!invalid_target.ok());
    assert(HasCode(invalid_target, kPathInvalid));

    const RulesBuildResult invalid_provider = Compile(
        "provider={enabled=\"yes\"}\nredirect_rules=[]\n");
    assert(!invalid_provider.ok());
    assert(HasCode(invalid_provider, kTypeMismatch));
    return 0;
}
