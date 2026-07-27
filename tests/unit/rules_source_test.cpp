#include <cstdint>
#include <string>

#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using pathguard::rules::ByteSpan;
using pathguard::rules::Diagnostic;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

SourceBuffer MakeSource(std::string bytes) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(bytes),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return std::move(*source);
}

}  // namespace

int main() {
    const SourceBuffer empty = MakeSource("");
    assert(empty.size() == 0);
    assert(empty.IsValidSpan({0, 0}));
    assert(!empty.IsValidSpan({0, 1}));

    const SourceBuffer ascii = MakeSource("ab\ncd");
    assert(ascii.IsValidSpan({0, 5}));
    assert(!ascii.IsValidSpan({4, 3}));
    assert(!ascii.IsValidSpan({0, 6}));
    const auto ascii_eof = ascii.line_index().PositionAt(5);
    assert(ascii_eof.has_value());
    assert(ascii_eof->line == 2 && ascii_eof->column == 3);
    assert(!ascii.line_index().PositionAt(6).has_value());

    const SourceBuffer unicode = MakeSource(
        std::string("A\xE4\xB8\xAD\xF0\x9F\x98\x80Z\nQ", 11));
    const auto after_unicode = unicode.line_index().PositionAt(8);
    assert(after_unicode.has_value());
    assert(after_unicode->line == 1 && after_unicode->column == 4);
    const auto second_line = unicode.line_index().PositionAt(10);
    assert(second_line.has_value());
    assert(second_line->line == 2 && second_line->column == 1);

    const SourceBuffer crlf = MakeSource("a\r\nb");
    const auto cr = crlf.line_index().PositionAt(1);
    const auto lf = crlf.line_index().PositionAt(2);
    const auto b = crlf.line_index().PositionAt(3);
    assert(cr.has_value() && cr->line == 1 && cr->column == 2);
    assert(lf.has_value() && lf->line == 1 && lf->column == 2);
    assert(b.has_value() && b->line == 2 && b->column == 1);

    const SourceBuffer bom = MakeSource("\xEF\xBB\xBF" "format = 1\n");
    const auto first_visible = bom.line_index().PositionAt(3);
    assert(first_visible.has_value());
    assert(first_visible->line == 1 && first_visible->column == 1);

    RulesLimits tiny;
    tiny.max_source_bytes = 3;
    Diagnostic too_large;
    assert(!SourceBuffer::Create("rules.toml", "1234", tiny, &too_large));
    assert(too_large.code == pathguard::rules::kResourceLimit);
    const ByteSpan expected_too_large{0, 4};
    assert(too_large.primary == expected_too_large);
    return 0;
}
