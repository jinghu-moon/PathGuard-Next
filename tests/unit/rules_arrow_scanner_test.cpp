#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "pathguard/rules/arrow_scanner.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using pathguard::rules::Diagnostic;
using pathguard::rules::RulesLexResult;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

RulesLexResult Analyze(std::string body, RulesLimits limits = {}) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(body), limits, &error);
    assert(source.has_value());
    return pathguard::rules::AnalyzeRulesSource(*source, limits);
}

bool HasCode(const RulesLexResult& result, std::string_view code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const Diagnostic& value) {
                           return value.code == code;
                       });
}

void ExpectOneCandidate(std::string body) {
    const RulesLexResult result = Analyze(std::move(body));
    assert(result.parser_allowed());
    assert(result.format_version == 1);
    assert(result.candidates.size() == 1);
    assert(result.diagnostics.empty());
}

void ExpectError(std::string body, std::string_view code,
                 RulesLimits limits = {}) {
    const std::size_t source_size = body.size();
    const RulesLexResult result = Analyze(std::move(body), limits);
    assert(!result.parser_allowed());
    assert(result.candidates.empty());
    assert(HasCode(result, code));
    for (const Diagnostic& diagnostic : result.diagnostics) {
        assert(diagnostic.primary.begin <= diagnostic.primary.end);
        assert(diagnostic.primary.end <= source_size);
    }
}

}  // namespace

int main() {
    ExpectOneCandidate("format = 1\nredirect = [\"A\" -> \"B\"]\n");
    ExpectOneCandidate("format = 1\nredirect = ['A'->'B',]\n");
    ExpectOneCandidate("format = 1\nredirect = [\"A\"\n  ->\t\"B\",]\n");
    ExpectOneCandidate("format = 1\nredirect = [[\"A\" -> \"B\"]]\n");
    ExpectOneCandidate("format = 1\nredirect = [\"A\" -> \"B\"");

    const auto escaped = Analyze(
        "format = 1\nredirect = [\"A\\\" -> still string\" -> \"B\"]\n");
    assert(escaped.parser_allowed() && escaped.candidates.size() == 1);
    const auto even_slashes = Analyze(
        "format = 1\nredirect = [\"A\\\\\" -> \"B\"]\n");
    assert(even_slashes.parser_allowed() && even_slashes.candidates.size() == 1);
    const auto literal = Analyze(
        "format = 1\nredirect = ['A -> inside' -> 'B']\n");
    assert(literal.parser_allowed() && literal.candidates.size() == 1);
    const auto invalid_escape_is_deferred = Analyze(
        "format = 1\nredirect = [\"A\\q\" -> \"B\"]\n");
    assert(invalid_escape_is_deferred.parser_allowed());
    assert(invalid_escape_is_deferred.candidates.size() == 1);

    const auto multiline = Analyze(
        "format = 1\nvalue = \"\"\"A -> B # [ ]\"\"\"\n"
        "literal = '''C -> D # { }'''\n");
    assert(multiline.parser_allowed());
    assert(multiline.candidates.empty());
    const auto quote_runs = Analyze(
        "format = 1\na = \"\"\"one\"\"\"\"\nb = '''two'''''\n");
    assert(quote_runs.parser_allowed());
    assert(quote_runs.candidates.empty());
    const auto comments = Analyze(
        "format = 1\n# [ { \"A\" -> \"B\" } ]\r\nvalue = 1 # -> EOF");
    assert(comments.parser_allowed() && comments.candidates.empty());

    ExpectError("format = 1\nfoo = \"A\" -> \"B\"\n",
                pathguard::rules::kArrowContext);
    ExpectError("format = 1\nfoo = { bar = \"A\" -> \"B\" }\n",
                pathguard::rules::kArrowContext);
    ExpectError("format = 1\n[\"A\" -> \"B\"]\n",
                pathguard::rules::kArrowContext);
    ExpectError("format = 1\n[[\"A\" -> \"B\"]]\n",
                pathguard::rules::kArrowContext);
    ExpectError("format = 1\nredirect = [-> \"B\"]\n",
                pathguard::rules::kArrowOperand);
    ExpectError("format = 1\nredirect = [\"A\" ->]\n",
                pathguard::rules::kArrowOperand);
    ExpectError("format = 1\nredirect = [\"A\" # internal\n -> \"B\"]\n",
                pathguard::rules::kArrowCommentInside);
    ExpectError("format = 1\nredirect = [\"A\" -> # internal\n \"B\"]\n",
                pathguard::rules::kArrowCommentInside);
    ExpectError("format = 1\nredirect = [\"A\" -> \"B\" -> \"C\"]\n",
                pathguard::rules::kArrowChained);
    ExpectError("format = 1\nredirect = [\"A\" -> \"B\" \"C\" -> \"D\"]\n",
                pathguard::rules::kArrowMissingComma);
    ExpectError("format = 1\nredirect = [\"A\" -> 7]\n",
                pathguard::rules::kArrowOperand);
    ExpectError("format = 1\nredirect = [\"\"\"A\"\"\" -> \"B\"]\n",
                pathguard::rules::kArrowOperand);
    ExpectError("format = 1\nredirect = [\"unterminated\n",
                pathguard::rules::kArrowStringBoundary);

    const auto after_comment = Analyze(
        "format = 1\nredirect = [\n# before\n\"A\" -> \"B\", # after\n]\n");
    assert(after_comment.parser_allowed() && after_comment.candidates.size() == 1);

    RulesLimits rewrite_limit;
    rewrite_limit.max_rewrites = 1;
    ExpectError(
        "format = 1\nredirect = [\"A\" -> \"B\", \"C\" -> \"D\"]\n",
        pathguard::rules::kResourceLimit, rewrite_limit);

    RulesLimits string_limit;
    string_limit.max_string_token_bytes = 3;
    ExpectError("format = 1\nvalue = \"long\"\n",
                pathguard::rules::kResourceLimit, string_limit);

    RulesLimits token_limit;
    token_limit.max_tokens_or_nodes = 2;
    ExpectError("format = 1\na = 1\n",
                pathguard::rules::kResourceLimit, token_limit);

    RulesLimits depth_limit;
    depth_limit.max_container_depth = 2;
    ExpectError("format = 1\nvalue = [[[1]]]\n",
                pathguard::rules::kResourceLimit, depth_limit);

    RulesLimits diagnostic_limit;
    diagnostic_limit.max_diagnostics = 2;
    const auto bounded = Analyze(
        "format = 1\na = \"A\" -> \"B\"\nb = \"C\" -> \"D\"\n"
        "c = \"E\" -> \"F\"\n",
        diagnostic_limit);
    assert(!bounded.parser_allowed());
    assert(bounded.diagnostics.size() == 2);
    assert(bounded.diagnostics.back().code == pathguard::rules::kDiagnosticsOmitted);

    bool parser_called = false;
    const auto invalid = Analyze("format = 1\nredirect = [\"A\" ->]\n");
    if (invalid.parser_allowed()) parser_called = true;
    assert(!parser_called);
    assert(invalid.candidates.empty());

    const auto format_first = Analyze("format = 2\nredirect = [\"A\" ->]\n");
    assert(HasCode(format_first, pathguard::rules::kFormatUnsupported));
    assert(!HasCode(format_first, pathguard::rules::kArrowOperand));
    return 0;
}
