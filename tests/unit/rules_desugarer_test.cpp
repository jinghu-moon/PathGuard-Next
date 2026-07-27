#include <cstdint>
#include <string>
#include <vector>

#include "pathguard/rules/desugarer.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using pathguard::rules::ArrowRewrite;
using pathguard::rules::ByteSpan;
using pathguard::rules::DesugarResult;
using pathguard::rules::Diagnostic;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

SourceBuffer MakeSource(std::string bytes, RulesLimits limits = {}) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(bytes), limits,
                                       &error);
    assert(source.has_value());
    return std::move(*source);
}

void ExpectInternal(const SourceBuffer& source,
                    const std::vector<ArrowRewrite>& rewrites,
                    RulesLimits limits = {}) {
    const DesugarResult result =
        pathguard::rules::EmitDesugaredSource(source, rewrites, limits);
    assert(!result.ok());
    assert(result.diagnostics.size() == 1);
    assert(result.diagnostics.front().code == pathguard::rules::kDesugarInternal
           || result.diagnostics.front().code == pathguard::rules::kResourceLimit);
    assert(!result.generated_storage.has_value());
    assert(result.redirects.empty());
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    const SourceBuffer source = MakeSource(
        "format = 1\nredirect = [\"A\" -> \"B\", \"C\" -> \"D\"]\n");
    const DesugarResult result = DesugarRulesSource(source, RulesLimits{});
    assert(result.ok());
    assert(result.rewritten());
    assert(result.parser_input(source)
           == "format = 1\nredirect = [{ from = \"A\", to = \"B\" }, "
              "{ from = \"C\", to = \"D\" }]\n");
    assert(result.redirects.size() == 2);
    assert(result.redirects[0].id == 1 && result.redirects[1].id == 2);
    assert(source.bytes().substr(result.redirects[0].original_source.begin,
                                 result.redirects[0].original_source.size())
           == "\"A\"");
    assert(result.parser_input(source).substr(
               result.redirects[0].generated_source.begin,
               result.redirects[0].generated_source.size()) == "\"A\"");
    assert(result.rewrite_map.query_count() == 0);

    const SourceBuffer crossline = MakeSource(
        "format = 1\nredirect = [\n  'A'\n    ->\n  \"B\", # tail\n]\n");
    const DesugarResult folded = DesugarRulesSource(crossline, RulesLimits{});
    assert(folded.ok());
    assert(folded.parser_input(crossline).find(
               "{ from = 'A', to = \"B\" }, # tail") != std::string_view::npos);

    const SourceBuffer no_arrow = MakeSource("format = 1\nvalue = \"inside -> text\"\n");
    const DesugarResult identity = DesugarRulesSource(no_arrow, RulesLimits{});
    assert(identity.ok() && !identity.rewritten());
    assert(!identity.generated_storage.has_value());
    assert(identity.redirects.empty());
    assert(identity.rewrite_map.segments().empty());
    assert(identity.parser_input(no_arrow).data() == no_arrow.bytes().data());
    const auto identity_position = identity.rewrite_map.MapGeneratedPosition(7);
    assert(identity_position.has_value());
    const ByteSpan expected_identity{7, 7};
    assert(*identity_position == expected_identity);

    const SourceBuffer one = MakeSource("format = 1\nredirect = [\"A\" -> \"B\"]\n");
    const std::vector<ArrowRewrite> valid{{1, {23, 33}, {23, 26}, {27, 29},
                                           {30, 33}}};
    std::vector<ArrowRewrite> bad = valid;
    bad[0].source = {26, 26};
    ExpectInternal(one, bad);
    bad = valid;
    bad.push_back(valid.front());
    ExpectInternal(one, bad);
    bad = {{2, {23, 33}, {23, 26}, {27, 29}, {30, 33}},
           {1, {12, 22}, {12, 15}, {16, 18}, {19, 22}}};
    ExpectInternal(one, bad);
    bad = valid;
    bad.push_back({2, {30, 33}, {30, 31}, {31, 32}, {32, 33}});
    ExpectInternal(one, bad);

    RulesLimits no_rewrites;
    no_rewrites.max_rewrites = 0;
    ExpectInternal(one, valid, no_rewrites);
    RulesLimits tiny_output;
    tiny_output.max_generated_bytes = one.size();
    ExpectInternal(one, valid, tiny_output);
    RulesLimits tiny_segments;
    tiny_segments.max_rewrite_segments = 2;
    ExpectInternal(one, valid, tiny_segments);

    std::vector<RewriteSegment> invalid_segments{{
        {1, 2}, {0, 1}, RewriteSegmentKind::kCopied}};
    assert(!RewriteMap::Create(std::move(invalid_segments), 2, 2));

    const GeneratedRedirect& generated = result.redirects.front();
    const auto prefix = result.rewrite_map.MapGeneratedPosition(
        generated.generated_table.begin);
    const auto copied = result.rewrite_map.MapGeneratedPosition(
        generated.generated_source.begin + 1);
    const auto arrow = result.rewrite_map.MapGeneratedPosition(
        generated.generated_source.end + 2);
    assert(prefix == generated.original_rule);
    const ByteSpan expected_copied{generated.original_source.begin + 1,
                                   generated.original_source.begin + 1};
    assert(copied == expected_copied);
    assert(arrow == generated.original_arrow);
    assert(result.rewrite_map.query_count() == 3);
    const auto whole = result.rewrite_map.MapGeneratedSpan(
        generated.generated_table);
    assert(whole == generated.original_rule);
    assert(result.rewrite_map.query_count() == 4);
    assert(!result.rewrite_map.MapGeneratedPosition(
        static_cast<std::uint32_t>(result.parser_input(source).size() + 1)));

    const SourceBuffer malformed_source = MakeSource(
        "format = 1\nfoo = {\nredirect = [\"A\" -> \"B\"]\n");
    const DesugarResult malformed =
        DesugarRulesSource(malformed_source, RulesLimits{});
    if (malformed.ok()) {
        assert(malformed.parser_input(malformed_source).find(" -> ")
               == std::string_view::npos);
    } else {
        assert(!malformed.generated_storage.has_value());
        assert(malformed.redirects.empty());
    }
    return 0;
}
