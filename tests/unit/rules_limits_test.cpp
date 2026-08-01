#include <algorithm>
#include <string>
#include <string_view>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using namespace pathguard::rules;

std::string Valid(std::string_view glob = "A") {
    return "format = 2\n[apps.\"com.example.app\"]\n"
        "redirect_rules=[{select={root=\"\",glob=\""
        + std::string(glob) + "\",type=\"any\"},to=\"Target\"}]\n";
}

RulesBuildResult Compile(std::string text, const RulesLimits& limits) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(text), limits,
                                       &error);
    if (!source.has_value()) {
        RulesBuildResult result;
        result.diagnostics.push_back(error);
        return result;
    }
    return CompileRules(*source, limits);
}

bool HasResourceLimit(const RulesBuildResult& result) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& item) { return item.code == kResourceLimit; });
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    RulesLimits source;
    source.max_source_bytes = 8;
    Diagnostic source_error;
    assert(!SourceBuffer::Create("rules.toml", Valid(), source, &source_error));
    assert(source_error.code == kResourceLimit);

    RulesLimits apps;
    apps.max_apps = 0;
    assert(HasResourceLimit(Compile(Valid(), apps)));

    RulesLimits app_rules;
    app_rules.max_rules_per_app = 0;
    assert(HasResourceLimit(Compile(Valid(), app_rules)));

    RulesLimits path_bytes;
    path_bytes.max_path_bytes = 2;
    assert(HasResourceLimit(Compile(Valid("Long"), path_bytes))
           || !Compile(Valid("Long"), path_bytes).ok());

    RulesLimits path_components;
    path_components.max_path_components = 1;
    assert(!Compile(Valid("A/B"), path_components).ok());

    RulesLimits diagnostics;
    diagnostics.max_diagnostics = 2;
    const RulesBuildResult bounded = Compile(
        "format = 2\n[apps.\"com.example.app\"]\nunknown=true\nother=true\n",
        diagnostics);
    assert(!bounded.ok());
    assert(bounded.diagnostics.size() <= diagnostics.max_diagnostics);
    return 0;
}
