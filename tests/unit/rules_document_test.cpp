#include <algorithm>
#include <string>
#include <string_view>

#include "pathguard/rules/compiler.h"
#include "pathguard/rules/desugarer.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using pathguard::rules::Diagnostic;
using pathguard::rules::RulesCompileResult;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

SourceBuffer MakeSource(std::string bytes) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(bytes),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return std::move(*source);
}

RulesCompileResult Compile(std::string bytes) {
    SourceBuffer source = MakeSource(std::move(bytes));
    return pathguard::rules::ParseRulesDocument(source, RulesLimits{});
}

RulesCompileResult CompileWithLimits(std::string bytes, RulesLimits limits) {
    SourceBuffer source = MakeSource(std::move(bytes));
    return pathguard::rules::ParseRulesDocument(source, limits);
}

void Expect(std::string bytes, std::string_view code) {
    const RulesCompileResult result = Compile(std::move(bytes));
    assert(!result.ok());
    assert(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const Diagnostic& diagnostic) {
                           return diagnostic.code == code;
                       }));
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    const std::string prefix =
        "format = 1\n[apps.\"com.example.app\"]\n";
    Expect(prefix + "deny = [\"A\" -> \"B\"]\n", kRuleArrowScope);
    Expect("format = 1\nredirect = [\"A\" -> \"B\"]\n",
           kRuleArrowScope);
    Expect(prefix + "redirect = [[\"A\" -> \"B\"]]\n",
           kRuleArrowScope);
    const RulesCompileResult dotted = Compile(
        "format = 1\napps.\"com.example.app\".redirect = [\"A\" -> \"B\"]\n");
    assert(dotted.ok() && dotted.document->apps.size() == 1);

    Expect(prefix +
        "redirect = [{ from = \"A\", to = \"B\" }]\n",
        kRedirectSyntax);
    Expect(prefix +
        "redirect = [\"A\" -> \"B\", { from = \"C\", to = \"D\" }]\n",
        kRedirectSyntax);
    Expect(prefix + "redirect = [\"A\"]\n", kRedirectSyntax);
    Expect(prefix + "redirect = []\nunknown = true\n", kUnknownField);
    Expect("format = 1\nextra = true\n[apps.\"com.example.app\"]\n"
           "deny = [\"A\"]\n", kUnknownField);
    Expect(prefix + "enabled = \"yes\"\ndeny = [\"A\"]\n", kTypeMismatch);
    Expect(prefix + "users = [0, 0]\ndeny = [\"A\"]\n", kInvalidValue);
    Expect(prefix + "users = [-1]\ndeny = [\"A\"]\n", kInvalidValue);
    Expect(prefix + "users = []\ndeny = [\"A\"]\n", kInvalidValue);
    Expect(prefix + "processes = [\"other.app\"]\ndeny = [\"A\"]\n",
           kInvalidValue);
    Expect("format = 1\n[apps.\"bad\"]\ndeny = [\"A\"]\n",
           kInvalidValue);
    Expect(prefix + "deny = [7]\n", kTypeMismatch);

    const RulesCompileResult disabled = Compile(
        prefix + "enabled = false\ndeny = [\"A\"]\nredirect = []\n");
    assert(disabled.ok());
    assert(!disabled.document->apps.front().enabled);

    RulesLimits one_rule;
    one_rule.max_rules_per_app = 1;
    const RulesCompileResult over_limit = CompileWithLimits(
        prefix + "deny = [\"A\", \"B\"]\n", one_rule);
    assert(!over_limit.ok());
    assert(over_limit.diagnostics.front().code == kResourceLimit);

    SourceBuffer source = MakeSource(prefix + "redirect = [\"A\" -> \"B\"]\n");
    DesugarResult duplicate = DesugarRulesSource(source, RulesLimits{});
    duplicate.redirects.push_back(duplicate.redirects.front());
    const RulesCompileResult duplicate_result = DecodeDesugaredRules(
        source, std::move(duplicate), RulesLimits{});
    assert(!duplicate_result.ok());
    assert(duplicate_result.diagnostics.front().code == kDesugarInternal);

    DesugarResult missing = DesugarRulesSource(source, RulesLimits{});
    missing.redirects.front().generated_table = {0, 1};
    const RulesCompileResult missing_result = DecodeDesugaredRules(
        source, std::move(missing), RulesLimits{});
    assert(!missing_result.ok());
    assert(missing_result.diagnostics.front().code == kDesugarInternal);
    return 0;
}
