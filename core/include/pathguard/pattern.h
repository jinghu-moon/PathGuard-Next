#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/pattern_limits.h"

namespace pathguard::pattern {

enum class PatternTokenKind : std::uint8_t {
    kLiteral,
    kStarComponent,
    kOneComponentChar,
    kCharacterClass,
};

struct CharacterClass {
    std::array<std::uint64_t, 2> bitmap{};
    bool negated = false;

    bool operator==(const CharacterClass&) const = default;
};

struct PatternToken {
    PatternTokenKind kind = PatternTokenKind::kLiteral;
    std::string literal;
    std::uint16_t character_class = 0;

    bool operator==(const PatternToken&) const = default;
};

struct PatternComponent {
    bool globstar = false;
    std::vector<PatternToken> tokens;

    bool operator==(const PatternComponent&) const = default;
};

struct PatternProgram {
    std::vector<PatternComponent> components;
    std::vector<CharacterClass> character_classes;
    std::uint16_t token_count = 0;
    std::uint16_t specificity = 0;
    std::string canonical;

    bool operator==(const PatternProgram&) const = default;
};

enum class PatternCompileError : std::uint8_t {
    kNone,
    kInvalidUtf8,
    kInvalidPath,
    kTrailingEscape,
    kInvalidGlobstar,
    kInvalidCharacterClass,
    kUnsupportedSyntax,
    kResourceLimit,
};

struct PatternCompileResult {
    std::optional<PatternProgram> program;
    PatternCompileError error = PatternCompileError::kNone;
    std::size_t byte_offset = 0;

    bool ok() const { return program.has_value(); }
};

enum class PatternMatchResult : std::uint8_t {
    kMatch,
    kNoMatch,
    kInvalidPathEncoding,
    kBudgetExceeded,
};

struct PatternMatchScratch {
    std::array<std::string_view, 256> path_components{};
    std::array<std::uint8_t, 257> component_current{};
    std::array<std::uint8_t, 257> component_next{};
    std::array<std::uint8_t, 4096> scalar_current{};
    std::array<std::uint8_t, 4096> scalar_next{};
};

PatternCompileResult CompilePattern(
    std::string_view pattern,
    const PatternLimitsProfile& limits = kPatternLimitsProfileV1);

PatternMatchResult MatchPattern(
    const PatternProgram& program, std::string_view relative_path,
    PatternMatchScratch* scratch,
    const PatternLimitsProfile& limits = kPatternLimitsProfileV1) noexcept;

std::string_view PatternTokenName(PatternTokenKind kind);

enum class BraceExpandError : std::uint8_t {
    kNone,
    kInvalidSyntax,
    kNested,
    kEmptyAlternative,
    kUnsupportedAlternative,
    kResultLimit,
    kByteLimit,
};

struct BraceExpansionResult {
    std::vector<std::string> patterns;
    BraceExpandError error = BraceExpandError::kNone;
    std::size_t actual = 0;
    std::size_t limit = 0;

    bool ok() const { return error == BraceExpandError::kNone; }
};

BraceExpansionResult ExpandPatternBraces(
    std::string_view pattern,
    const PatternLimitsProfile& limits = kPatternLimitsProfileV1);

}  // namespace pathguard::pattern
