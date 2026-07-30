#include "pathguard/pattern.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace pathguard::pattern {
namespace {

struct Scalar {
    std::uint32_t value = 0;
    std::size_t width = 0;
};

std::optional<Scalar> DecodeScalar(std::string_view input,
                                   std::size_t offset) noexcept {
    if (offset >= input.size()) return std::nullopt;
    const auto lead = static_cast<std::uint8_t>(input[offset]);
    if (lead < 0x80U) return Scalar{lead, 1};
    std::size_t width = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if ((lead & 0xe0U) == 0xc0U) {
        width = 2;
        value = lead & 0x1fU;
        minimum = 0x80U;
    } else if ((lead & 0xf0U) == 0xe0U) {
        width = 3;
        value = lead & 0x0fU;
        minimum = 0x800U;
    } else if ((lead & 0xf8U) == 0xf0U) {
        width = 4;
        value = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return std::nullopt;
    }
    if (offset + width > input.size()) return std::nullopt;
    for (std::size_t index = 1; index < width; ++index) {
        const auto continuation = static_cast<std::uint8_t>(input[offset + index]);
        if ((continuation & 0xc0U) != 0x80U) return std::nullopt;
        value = (value << 6U) | (continuation & 0x3fU);
    }
    if (value < minimum || value > 0x10ffffU
        || (value >= 0xd800U && value <= 0xdfffU)) {
        return std::nullopt;
    }
    return Scalar{value, width};
}

bool IsValidUtf8(std::string_view input) noexcept {
    std::size_t offset = 0;
    while (offset < input.size()) {
        const auto scalar = DecodeScalar(input, offset);
        if (!scalar.has_value()) return false;
        offset += scalar->width;
    }
    return true;
}

void SetClassBit(CharacterClass* value, std::uint8_t character) {
    value->bitmap[character / 64U] |= UINT64_C(1) << (character % 64U);
}

bool ClassContains(const CharacterClass& value, std::uint32_t scalar) {
    bool member = false;
    if (scalar < 128U) {
        member = (value.bitmap[scalar / 64U]
                  & (UINT64_C(1) << (scalar % 64U))) != 0;
    }
    return value.negated ? !member : member;
}

void AppendU64(std::string* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output->push_back(static_cast<char>((value >> (index * 8U)) & 0xffU));
    }
}

void FlushLiteral(std::string* literal, PatternComponent* component,
                  std::uint16_t* token_count, std::uint16_t* specificity) {
    if (literal->empty()) return;
    std::uint16_t scalars = 0;
    for (std::size_t offset = 0; offset < literal->size();) {
        const auto scalar = DecodeScalar(*literal, offset);
        offset += scalar->width;
        ++scalars;
    }
    component->tokens.push_back(
        {PatternTokenKind::kLiteral, std::move(*literal), 0});
    literal->clear();
    ++*token_count;
    *specificity = static_cast<std::uint16_t>(std::min<std::uint32_t>(
        std::numeric_limits<std::uint16_t>::max(),
        static_cast<std::uint32_t>(*specificity) + scalars * 16U));
}

PatternCompileResult CompileError(PatternCompileError error,
                                  std::size_t offset) {
    return {std::nullopt, error, offset};
}

bool ParseCharacterClass(std::string_view component, std::size_t begin,
                         CharacterClass* output, std::size_t* end) {
    std::size_t cursor = begin + 1;
    if (cursor >= component.size()) return false;
    if (component[cursor] == '!' || component[cursor] == '^') {
        output->negated = true;
        ++cursor;
    }

    struct Atom {
        std::uint8_t value;
        bool escaped;
    };
    std::vector<Atom> atoms;
    bool closed = false;
    while (cursor < component.size()) {
        if (component[cursor] == ']') {
            closed = true;
            ++cursor;
            break;
        }
        bool escaped = false;
        if (component[cursor] == '\\') {
            escaped = true;
            if (++cursor >= component.size()) return false;
        }
        const auto value = static_cast<std::uint8_t>(component[cursor]);
        if (value >= 0x80U || value == '/' || (!escaped && value == '[')) {
            return false;
        }
        atoms.push_back({value, escaped});
        ++cursor;
    }
    if (!closed || atoms.empty()) return false;

    for (std::size_t index = 0; index < atoms.size();) {
        if (index + 2 < atoms.size() && atoms[index + 1].value == '-'
            && !atoms[index + 1].escaped) {
            if (atoms[index].value > atoms[index + 2].value) return false;
            for (std::uint16_t value = atoms[index].value;
                 value <= atoms[index + 2].value; ++value) {
                SetClassBit(output, static_cast<std::uint8_t>(value));
            }
            index += 3;
            continue;
        }
        if (atoms[index].value == '-' && !atoms[index].escaped) return false;
        SetClassBit(output, atoms[index].value);
        ++index;
    }
    *end = cursor;
    return true;
}

bool Spend(std::size_t* transitions, std::size_t budget) noexcept {
    if (*transitions >= budget) return false;
    ++*transitions;
    return true;
}

PatternMatchResult MatchComponentWithClasses(
    const PatternProgram& owner, const PatternComponent& component,
    std::string_view input, PatternMatchScratch* scratch,
    std::size_t* transitions, std::size_t budget) noexcept {
    const std::size_t state_count = input.size() + 1;
    std::fill_n(scratch->scalar_current.begin(), state_count, 0);
    scratch->scalar_current[0] = 1;
    for (const PatternToken& token : component.tokens) {
        std::fill_n(scratch->scalar_next.begin(), state_count, 0);
        if (token.kind == PatternTokenKind::kStarComponent) {
            std::copy_n(scratch->scalar_current.begin(), state_count,
                        scratch->scalar_next.begin());
            for (std::size_t offset = 0; offset < input.size();) {
                const auto scalar = DecodeScalar(input, offset);
                if (!scalar.has_value()) return PatternMatchResult::kInvalidPathEncoding;
                if (!Spend(transitions, budget)) return PatternMatchResult::kBudgetExceeded;
                if (scratch->scalar_next[offset]) {
                    scratch->scalar_next[offset + scalar->width] = 1;
                }
                offset += scalar->width;
            }
        } else {
            for (std::size_t offset = 0; offset <= input.size();) {
                if (scratch->scalar_current[offset]) {
                    if (!Spend(transitions, budget)) return PatternMatchResult::kBudgetExceeded;
                    if (token.kind == PatternTokenKind::kLiteral) {
                        if (input.substr(offset).starts_with(token.literal)) {
                            scratch->scalar_next[offset + token.literal.size()] = 1;
                        }
                    } else if (offset < input.size()) {
                        const auto scalar = DecodeScalar(input, offset);
                        if (!scalar.has_value()) return PatternMatchResult::kInvalidPathEncoding;
                        bool matches = token.kind == PatternTokenKind::kOneComponentChar;
                        if (token.kind == PatternTokenKind::kCharacterClass) {
                            if (token.character_class >= owner.character_classes.size()) {
                                return PatternMatchResult::kBudgetExceeded;
                            }
                            matches = ClassContains(
                                owner.character_classes[token.character_class],
                                scalar->value);
                        }
                        if (matches) scratch->scalar_next[offset + scalar->width] = 1;
                    }
                }
                if (offset == input.size()) break;
                const auto scalar = DecodeScalar(input, offset);
                if (!scalar.has_value()) return PatternMatchResult::kInvalidPathEncoding;
                offset += scalar->width;
            }
        }
        scratch->scalar_current.swap(scratch->scalar_next);
    }
    return scratch->scalar_current[input.size()]
        ? PatternMatchResult::kMatch : PatternMatchResult::kNoMatch;
}

std::size_t FindUnescaped(std::string_view input, char wanted,
                          std::size_t begin = 0) {
    bool escaped = false;
    for (std::size_t index = begin; index < input.size(); ++index) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (input[index] == '\\') {
            escaped = true;
            continue;
        }
        if (input[index] == wanted) return index;
    }
    return std::string_view::npos;
}

}  // namespace

PatternCompileResult CompilePattern(
    std::string_view pattern,
    const PatternLimitsProfile& limits) {
    if (!IsValidUtf8(pattern)) {
        return CompileError(PatternCompileError::kInvalidUtf8, 0);
    }
    if (pattern.empty() || pattern.size() > limits.max_path_bytes
        || pattern.front() == '/' || pattern.back() == '/') {
        return CompileError(PatternCompileError::kInvalidPath, 0);
    }
    if (pattern.front() == '!') {
        return CompileError(PatternCompileError::kUnsupportedSyntax, 0);
    }

    PatternProgram output;
    std::size_t component_begin = 0;
    while (component_begin <= pattern.size()) {
        const std::size_t slash = pattern.find('/', component_begin);
        const std::size_t component_end = slash == std::string_view::npos
            ? pattern.size() : slash;
        const std::string_view component = pattern.substr(
            component_begin, component_end - component_begin);
        if (component.empty() || component == "." || component == "..") {
            return CompileError(PatternCompileError::kInvalidPath,
                                component_begin);
        }
        if (output.components.size() >= limits.max_path_components) {
            return CompileError(PatternCompileError::kResourceLimit,
                                component_begin);
        }

        PatternComponent compiled;
        if (component == "**") {
            compiled.globstar = true;
            ++output.token_count;
            output.specificity = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(65535U, output.specificity + 1U));
        } else {
            if (component.find("**") != std::string_view::npos) {
                return CompileError(PatternCompileError::kInvalidGlobstar,
                                    component_begin);
            }
            std::string literal;
            for (std::size_t cursor = 0; cursor < component.size();) {
                const char value = component[cursor];
                if ((value == '+' || value == '@' || value == '!'
                     || value == '*' || value == '?')
                    && cursor + 1 < component.size()
                    && component[cursor + 1] == '(') {
                    return CompileError(PatternCompileError::kUnsupportedSyntax,
                                        component_begin + cursor);
                }
                if (value == '\\') {
                    if (cursor + 1 >= component.size()) {
                        return CompileError(PatternCompileError::kTrailingEscape,
                                            component_begin + cursor);
                    }
                    const auto escaped = DecodeScalar(component, cursor + 1);
                    literal.append(component.substr(cursor + 1, escaped->width));
                    cursor += 1 + escaped->width;
                    continue;
                }
                if (value == '*' || value == '?' || value == '[') {
                    FlushLiteral(&literal, &compiled, &output.token_count,
                                 &output.specificity);
                    if (value == '*') {
                        compiled.tokens.push_back(
                            {PatternTokenKind::kStarComponent, {}, 0});
                        output.specificity = static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(65535U,
                                output.specificity + 2U));
                        ++output.token_count;
                        ++cursor;
                    } else if (value == '?') {
                        if (cursor + 1 < component.size()
                            && component[cursor + 1] == '(') {
                            return CompileError(
                                PatternCompileError::kUnsupportedSyntax,
                                component_begin + cursor);
                        }
                        compiled.tokens.push_back(
                            {PatternTokenKind::kOneComponentChar, {}, 0});
                        output.specificity = static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(65535U,
                                output.specificity + 4U));
                        ++output.token_count;
                        ++cursor;
                    } else {
                        CharacterClass character_class;
                        std::size_t end = 0;
                        if (!ParseCharacterClass(component, cursor,
                                                 &character_class, &end)) {
                            return CompileError(
                                PatternCompileError::kInvalidCharacterClass,
                                component_begin + cursor);
                        }
                        auto found = std::find(output.character_classes.begin(),
                                               output.character_classes.end(),
                                               character_class);
                        std::size_t class_id = static_cast<std::size_t>(
                            found - output.character_classes.begin());
                        if (found == output.character_classes.end()) {
                            output.character_classes.push_back(character_class);
                        }
                        compiled.tokens.push_back({
                            PatternTokenKind::kCharacterClass, {},
                            static_cast<std::uint16_t>(class_id)});
                        output.specificity = static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(65535U,
                                output.specificity + 8U));
                        ++output.token_count;
                        cursor = end;
                    }
                    continue;
                }
                const auto scalar = DecodeScalar(component, cursor);
                literal.append(component.substr(cursor, scalar->width));
                cursor += scalar->width;
            }
            FlushLiteral(&literal, &compiled, &output.token_count,
                         &output.specificity);
        }
        if (output.token_count > limits.max_pattern_tokens) {
            return CompileError(PatternCompileError::kResourceLimit,
                                component_begin);
        }
        output.components.push_back(std::move(compiled));
        if (slash == std::string_view::npos) break;
        component_begin = slash + 1;
    }

    for (std::size_t index = 0; index < output.components.size(); ++index) {
        if (index != 0) output.canonical.push_back('/');
        const PatternComponent& component = output.components[index];
        if (component.globstar) {
            output.canonical.push_back('G');
            continue;
        }
        for (const PatternToken& token : component.tokens) {
            output.canonical.push_back(static_cast<char>(token.kind));
            if (token.kind == PatternTokenKind::kLiteral) {
                output.canonical.append(std::to_string(token.literal.size()));
                output.canonical.push_back(':');
                output.canonical.append(token.literal);
            } else if (token.kind == PatternTokenKind::kCharacterClass) {
                const CharacterClass& value =
                    output.character_classes[token.character_class];
                output.canonical.push_back(value.negated ? 1 : 0);
                AppendU64(&output.canonical, value.bitmap[0]);
                AppendU64(&output.canonical, value.bitmap[1]);
            }
        }
    }
    return {std::move(output), PatternCompileError::kNone, 0};
}

PatternMatchResult MatchPattern(
    const PatternProgram& program, std::string_view relative_path,
    PatternMatchScratch* scratch,
    const PatternLimitsProfile& limits) noexcept {
    if (scratch == nullptr || relative_path.empty()
        || relative_path.size() > limits.max_path_bytes
        || relative_path.size() >= scratch->scalar_current.size()
        || relative_path.front() == '/' || relative_path.back() == '/') {
        return PatternMatchResult::kNoMatch;
    }
    if (!IsValidUtf8(relative_path)) {
        return PatternMatchResult::kInvalidPathEncoding;
    }
    std::size_t path_count = 0;
    std::size_t begin = 0;
    while (begin <= relative_path.size()) {
        const std::size_t slash = relative_path.find('/', begin);
        const std::size_t end = slash == std::string_view::npos
            ? relative_path.size() : slash;
        if (end == begin || path_count >= limits.max_path_components
            || path_count >= scratch->path_components.size()) {
            return PatternMatchResult::kNoMatch;
        }
        scratch->path_components[path_count++] =
            relative_path.substr(begin, end - begin);
        if (slash == std::string_view::npos) break;
        begin = slash + 1;
    }

    std::fill(scratch->component_current.begin(),
              scratch->component_current.end(), 0);
    scratch->component_current[0] = 1;
    std::size_t transitions = 0;
    for (std::size_t pattern_index = 0;
         pattern_index < program.components.size(); ++pattern_index) {
        const PatternComponent& component = program.components[pattern_index];
        std::fill(scratch->component_next.begin(),
                  scratch->component_next.end(), 0);
        if (component.globstar) {
            const bool trailing = pattern_index + 1 == program.components.size();
            if (!trailing) {
                std::copy_n(scratch->component_current.begin(), path_count + 1,
                            scratch->component_next.begin());
            }
            for (std::size_t path_index = 0; path_index < path_count;
                 ++path_index) {
                if (!Spend(&transitions, limits.matcher_transition_budget)) {
                    return PatternMatchResult::kBudgetExceeded;
                }
                const bool can_consume = trailing
                    ? scratch->component_current[path_index]
                        || scratch->component_next[path_index]
                    : scratch->component_next[path_index];
                if (can_consume) scratch->component_next[path_index + 1] = 1;
            }
        } else {
            for (std::size_t path_index = 0; path_index < path_count;
                 ++path_index) {
                if (!scratch->component_current[path_index]) continue;
                const PatternMatchResult matched = MatchComponentWithClasses(
                    program, component, scratch->path_components[path_index],
                    scratch, &transitions, limits.matcher_transition_budget);
                if (matched == PatternMatchResult::kBudgetExceeded
                    || matched == PatternMatchResult::kInvalidPathEncoding) {
                    return matched;
                }
                if (matched == PatternMatchResult::kMatch) {
                    scratch->component_next[path_index + 1] = 1;
                }
            }
        }
        scratch->component_current.swap(scratch->component_next);
    }
    return scratch->component_current[path_count]
        ? PatternMatchResult::kMatch : PatternMatchResult::kNoMatch;
}

std::string_view PatternTokenName(PatternTokenKind kind) {
    switch (kind) {
        case PatternTokenKind::kLiteral: return "LITERAL";
        case PatternTokenKind::kStarComponent: return "STAR_COMPONENT";
        case PatternTokenKind::kOneComponentChar: return "ONE_COMPONENT_CHAR";
        case PatternTokenKind::kCharacterClass: return "CHAR_CLASS";
    }
    return "UNKNOWN";
}

BraceExpansionResult ExpandPatternBraces(
    std::string_view pattern,
    const PatternLimitsProfile& limits) {
    BraceExpansionResult result;
    result.patterns.emplace_back(pattern);
    for (;;) {
        bool expanded_any = false;
        std::vector<std::string> next;
        for (const std::string& candidate : result.patterns) {
            const std::size_t open = FindUnescaped(candidate, '{');
            const std::size_t stray_close = FindUnescaped(candidate, '}');
            if (open == std::string::npos) {
                if (stray_close != std::string::npos) {
                    result.patterns.clear();
                    result.error = BraceExpandError::kInvalidSyntax;
                    return result;
                }
                next.push_back(candidate);
                continue;
            }
            if (stray_close != std::string::npos && stray_close < open) {
                result.patterns.clear();
                result.error = BraceExpandError::kInvalidSyntax;
                return result;
            }
            std::size_t close = std::string::npos;
            bool escaped = false;
            for (std::size_t cursor = open + 1; cursor < candidate.size();
                 ++cursor) {
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (candidate[cursor] == '\\') {
                    escaped = true;
                } else if (candidate[cursor] == '{') {
                    result.patterns.clear();
                    result.error = BraceExpandError::kNested;
                    return result;
                } else if (candidate[cursor] == '}') {
                    close = cursor;
                    break;
                }
            }
            if (close == std::string::npos) {
                result.patterns.clear();
                result.error = BraceExpandError::kInvalidSyntax;
                return result;
            }
            const std::string_view body(candidate.data() + open + 1,
                                        close - open - 1);
            std::size_t alternative_begin = 0;
            std::size_t alternative_count = 0;
            while (alternative_begin <= body.size()) {
                const std::size_t comma = body.find(',', alternative_begin);
                const std::size_t end = comma == std::string_view::npos
                    ? body.size() : comma;
                const std::string_view alternative = body.substr(
                    alternative_begin, end - alternative_begin);
                if (alternative.empty()) {
                    result.patterns.clear();
                    result.error = BraceExpandError::kEmptyAlternative;
                    return result;
                }
                if (alternative.find("..") != std::string_view::npos
                    || alternative.find_first_of("/*?[]\\{}")
                        != std::string_view::npos) {
                    result.patterns.clear();
                    result.error = BraceExpandError::kUnsupportedAlternative;
                    return result;
                }
                next.push_back(candidate.substr(0, open)
                    + std::string(alternative) + candidate.substr(close + 1));
                ++alternative_count;
                if (next.size() > limits.max_brace_expansions) {
                    result.actual = next.size();
                    result.limit = limits.max_brace_expansions;
                    result.patterns.clear();
                    result.error = BraceExpandError::kResultLimit;
                    return result;
                }
                if (comma == std::string_view::npos) break;
                alternative_begin = comma + 1;
            }
            if (alternative_count < 2) {
                result.patterns.clear();
                result.error = BraceExpandError::kInvalidSyntax;
                return result;
            }
            expanded_any = true;
        }
        result.patterns = std::move(next);
        if (!expanded_any) break;
    }
    std::sort(result.patterns.begin(), result.patterns.end());
    result.patterns.erase(std::unique(result.patterns.begin(),
                                      result.patterns.end()),
                          result.patterns.end());
    std::size_t total_bytes = 0;
    for (const std::string& expanded : result.patterns) {
        if (expanded.size() > limits.max_brace_expanded_bytes - total_bytes) {
            result.actual = total_bytes + expanded.size();
            result.limit = limits.max_brace_expanded_bytes;
            result.patterns.clear();
            result.error = BraceExpandError::kByteLimit;
            return result;
        }
        total_bytes += expanded.size();
    }
    result.actual = total_bytes;
    result.limit = limits.max_brace_expanded_bytes;
    return result;
}

}  // namespace pathguard::pattern
