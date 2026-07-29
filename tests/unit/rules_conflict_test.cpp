#include <algorithm>
#include <string>
#include <string_view>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using pathguard::rules::Diagnostic;
using pathguard::rules::RulesBuildResult;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

SourceBuffer MakeSource(std::string bytes) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(bytes),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return std::move(*source);
}

RulesBuildResult Compile(std::string body) {
    SourceBuffer source = MakeSource("format = 1\n[apps.\"com.example.app\"]\n"
                                     + std::move(body));
    return pathguard::rules::CompileRules(source, RulesLimits{});
}

bool HasCode(const RulesBuildResult& result, std::string_view code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const Diagnostic& diagnostic) {
                           return diagnostic.code == code;
                       });
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    const RulesBuildResult duplicate = Compile(
        "redirect = [\"A\" -> \"B\", \"A\" -> \"B\"]\n");
    assert(duplicate.ok());
    assert(HasCode(duplicate, kRuleRedundant));
    assert(duplicate.canonical->apps.front().redirects.size() == 1);

    assert(HasCode(Compile(
        "redirect = [\"A\" -> \"B\", \"A\" -> \"C\"]\n"),
        kRuleConflict));
    assert(HasCode(Compile(
        "redirect = [\"A\" -> \"X\", \"A/B\" -> \"Y\"]\n"),
        kRuleConflict));
    assert(HasCode(Compile("redirect = [\"A\" -> \"A\"]\n"),
                   kRuleConflict));
    assert(HasCode(Compile("redirect = [\"A\" -> \"A/B\"]\n"),
                   kRuleConflict));
    assert(HasCode(Compile(
        "redirect = [\"A\" -> \"B\", \"B\" -> \"A\"]\n"),
        kRedirectCycle) || HasCode(Compile(
        "redirect = [\"A\" -> \"B\", \"B\" -> \"A\"]\n"),
        kRuleConflict));
    const RulesBuildResult shared_target = Compile(
        "file_picker = true\n"
        "redirect = [\"A\" -> \"Shared\", \"B\" -> \"Shared\"]\n");
    assert(shared_target.ok());
    assert(shared_target.canonical->apps.front().redirects.size() == 2);
    assert(HasCode(Compile("file_picker = true\nredirect = []\n"),
                   kInvalidValue));

    const RulesBuildResult deny = Compile(
        "deny = [\"Private\", \"Private/Sub\"]\n"
        "redirect = [\"Public\" -> \"Target\"]\n");
    assert(deny.ok());
    assert(HasCode(deny, kRuleRedundant));
    assert(deny.canonical->apps.front().deny.size() == 1);
    assert(deny.canonical->apps.front().deny.front().bytes == "Private");

    const RulesBuildResult deny_redirect = Compile(
        "deny = [\"A\"]\nredirect = [\"A/B\" -> \"C\"]\n");
    assert(HasCode(deny_redirect, kRuleConflict));

    const RulesBuildResult nested_deny = Compile(
        "deny = [\"A/Private\"]\nredirect = [\"A\" -> \"C\"]\n");
    assert(nested_deny.ok());
    assert(nested_deny.canonical->apps.front().deny.size() == 1);
    assert(nested_deny.canonical->apps.front().redirects.size() == 1);

    const RulesBuildResult disabled_bad = Compile(
        "enabled = false\nredirect = [\"../bad\" -> \"Target\"]\n");
    assert(HasCode(disabled_bad, kPathInvalid));
    return 0;
}
