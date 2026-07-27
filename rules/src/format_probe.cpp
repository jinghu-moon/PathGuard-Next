#include "pathguard/rules/format_probe.h"

#include <cctype>
#include <string_view>

#include "pathguard/rules_contract.h"

namespace pathguard::rules {
namespace {

bool HasUtf8Bom(std::string_view bytes) {
    return bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xefU
        && static_cast<unsigned char>(bytes[1]) == 0xbbU
        && static_cast<unsigned char>(bytes[2]) == 0xbfU;
}

bool IsHorizontalSpace(char value) {
    return value == ' ' || value == '\t';
}

ByteSpan SpanAt(std::size_t begin, std::size_t end) {
    return {static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(end)};
}

FormatProbeResult Error(std::string_view code, std::string_view message_key,
                        std::size_t begin, std::size_t end) {
    return {0, {code, message_key, SpanAt(begin, end), false}};
}

}  // namespace

FormatProbeResult ProbeRulesFormat(const SourceBuffer& source) {
    const std::string_view bytes = source.bytes();
    std::size_t offset = HasUtf8Bom(bytes) ? 3 : 0;
    while (offset < bytes.size()) {
        const char value = bytes[offset];
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
            ++offset;
            continue;
        }
        if (value == '#') {
            while (offset < bytes.size() && bytes[offset] != '\n') ++offset;
            continue;
        }
        break;
    }
    if (offset == bytes.size()) {
        return Error(kFormatMissing, "rules.format_missing", offset, offset);
    }

    const std::size_t declaration_begin = offset;
    constexpr std::string_view key = "format";
    if (bytes.substr(offset, key.size()) != key) {
        return Error(kFormatInvalid, "rules.format_must_be_first",
                     offset, std::min(bytes.size(), offset + 1));
    }
    offset += key.size();
    if (offset < bytes.size() && !IsHorizontalSpace(bytes[offset])
        && bytes[offset] != '=') {
        return Error(kFormatInvalid, "rules.format_bare_key_required",
                     declaration_begin, std::min(bytes.size(), offset + 1));
    }
    while (offset < bytes.size() && IsHorizontalSpace(bytes[offset])) ++offset;
    if (offset == bytes.size() || bytes[offset] != '=') {
        return Error(kFormatInvalid, "rules.format_equals_required",
                     declaration_begin, offset);
    }
    ++offset;
    while (offset < bytes.size() && IsHorizontalSpace(bytes[offset])) ++offset;
    const std::size_t value_begin = offset;
    while (offset < bytes.size() && !IsHorizontalSpace(bytes[offset])
           && bytes[offset] != '#' && bytes[offset] != '\r'
           && bytes[offset] != '\n') {
        ++offset;
    }
    const std::string_view value = bytes.substr(value_begin, offset - value_begin);
    while (offset < bytes.size() && IsHorizontalSpace(bytes[offset])) ++offset;
    if (offset < bytes.size() && bytes[offset] != '#'
        && bytes[offset] != '\r' && bytes[offset] != '\n') {
        return Error(kFormatInvalid, "rules.format_invalid_value",
                     value_begin, offset + 1);
    }
    if (value == "1") return {1, {}};

    bool decimal = !value.empty();
    for (char digit : value) {
        decimal = decimal && digit >= '0' && digit <= '9';
    }
    const bool canonical_decimal = decimal
        && (value == "0" || value.front() != '0');
    if (canonical_decimal) {
        return Error(kFormatUnsupported, "rules.format_unsupported",
                     value_begin, offset);
    }
    return Error(kFormatInvalid, "rules.format_invalid_value",
                 value_begin, offset);
}

}  // namespace pathguard::rules
