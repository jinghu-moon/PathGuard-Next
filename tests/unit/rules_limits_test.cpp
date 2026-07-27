#include <algorithm>
#include <string>

#include "pathguard/rules/arrow_scanner.h"
#include "pathguard/rules/desugarer.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using namespace pathguard::rules;

SourceBuffer Source(std::string text, const RulesLimits& limits = {}) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(text), limits,
                                       &error);
    assert(source.has_value());
    return std::move(*source);
}

bool HasCode(const std::vector<Diagnostic>& diagnostics,
             std::string_view code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const Diagnostic& item) {
                           return item.code == code;
                       });
}

RulesBuildResult Compile(std::string text, const RulesLimits& limits) {
    SourceBuffer source = Source(std::move(text), limits);
    return CompileRules(source, limits);
}

std::string Valid(std::string_view path = "A") {
    return "format = 1\n[apps.\"com.example.app\"]\nredirect=[\""
        + std::string(path) + "\" -> \"Target\"]\n";
}

void ExpectResourceFailure(std::string text, RulesLimits limits) {
    const RulesBuildResult result = Compile(std::move(text), limits);
    assert(!result.ok());
    assert(!result.blob.has_value());
    assert(HasCode(result.diagnostics, kResourceLimit));
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    RulesLimits source_limit;
    source_limit.max_source_bytes = 8;
    Diagnostic source_error;
    assert(!SourceBuffer::Create("rules.toml", Valid(), source_limit,
                                 &source_error));
    assert(source_error.code == kResourceLimit);

    RulesLimits depth;
    depth.max_container_depth = 1;
    ExpectResourceFailure(
        "format = 1\n[apps.\"com.example.app\"]\nusers=[[0]]\n", depth);

    RulesLimits tokens;
    tokens.max_tokens_or_nodes = 4;
    ExpectResourceFailure(Valid(), tokens);

    RulesLimits apps;
    apps.max_apps = 0;
    ExpectResourceFailure(Valid(), apps);

    RulesLimits app_rules;
    app_rules.max_rules_per_app = 0;
    ExpectResourceFailure(Valid(), app_rules);

    RulesLimits expanded;
    expanded.max_expanded_rules = 3;
    ExpectResourceFailure(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "users=[0,1]\nprocesses=[\"com.example.app\",\"com.example.app:worker\"]\n"
        "redirect=[\"A\" -> \"B\"]\n", expanded);

    RulesLimits path_bytes;
    path_bytes.max_path_bytes = 2;
    ExpectResourceFailure(Valid("Long"), path_bytes);

    RulesLimits components;
    components.max_path_components = 1;
    ExpectResourceFailure(Valid("A/B"), components);

    RulesLimits string_token;
    string_token.max_string_token_bytes = 2;
    ExpectResourceFailure(Valid("Long"), string_token);

    RulesLimits rewrites;
    rewrites.max_rewrites = 0;
    ExpectResourceFailure(Valid(), rewrites);

    RulesLimits segments;
    segments.max_rewrite_segments = 1;
    ExpectResourceFailure(Valid(), segments);

    RulesLimits generated;
    generated.max_generated_bytes = 16;
    ExpectResourceFailure(Valid(), generated);

    RulesLimits diagnostics;
    diagnostics.max_diagnostics = 2;
    const RulesBuildResult bounded = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "one=true\ntwo=true\nthree=true\n", diagnostics);
    assert(!bounded.ok());
    assert(bounded.diagnostics.size() <= diagnostics.max_diagnostics);

    RulesLimits related;
    related.max_related_spans = 0;
    const RulesBuildResult duplicate = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect=[\"A\" -> \"B\",\"A\" -> \"B\"]\n", related);
    assert(duplicate.ok());
    assert(!duplicate.diagnostics.empty());
    assert(duplicate.diagnostics.front().related.empty());
    return 0;
}
