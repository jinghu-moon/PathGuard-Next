#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pathguard/pattern_limits.h"
#include "pathguard/policy_v6_view.h"

namespace pathguard::policy_pattern_runtime {

enum class MatchResult : uint8_t {
    kNoMatch,
    kMatch,
    kInvalidPathEncoding,
    kBudgetExceeded,
    kInvalidPolicy,
};

struct ComponentSpan {
    uint16_t offset = 0;
    uint16_t size = 0;
};

struct PatternComponentSpan {
    uint32_t first_token = 0;
    uint16_t token_count = 0;
    bool globstar = false;
};

struct MatchScratch {
    ComponentSpan path_components[256]{};
    PatternComponentSpan pattern_components[64]{};
    bool current[257]{};
    bool next[257]{};
    uint32_t transitions = 0;
};

inline bool IsUniversalDescendantPattern(
        const policy_v6_view::PolicyV6View& policy,
        const policy_v6_view::SelectorRef& selector) {
    if (selector.match_kind == 0) return true;
    policy_v6_view::PatternRef pattern;
    policy_v6_view::TokenRef token;
    return selector.except_count == 0
        && policy.PatternAt(selector.base_pattern_id, &pattern)
        && pattern.token_count == 1
        && policy.TokenAt(pattern.first_token, &token)
        && token.kind == 3;
}

inline bool DecodeScalar(const char* value, size_t size, size_t offset,
                         size_t* next, uint32_t* scalar) {
    if (value == nullptr || next == nullptr || scalar == nullptr || offset >= size) {
        return false;
    }
    const uint8_t first = static_cast<uint8_t>(value[offset]);
    size_t width = 0;
    uint32_t result = 0;
    uint32_t minimum = 0;
    if (first < 0x80) { width = 1; result = first; }
    else if ((first & 0xe0) == 0xc0) { width = 2; result = first & 0x1f; minimum = 0x80; }
    else if ((first & 0xf0) == 0xe0) { width = 3; result = first & 0x0f; minimum = 0x800; }
    else if ((first & 0xf8) == 0xf0) { width = 4; result = first & 0x07; minimum = 0x10000; }
    else return false;
    if (width > size - offset) return false;
    for (size_t i = 1; i < width; ++i) {
        const uint8_t byte = static_cast<uint8_t>(value[offset + i]);
        if ((byte & 0xc0) != 0x80) return false;
        result = (result << 6) | (byte & 0x3f);
    }
    if ((width != 1 && result < minimum) || result > 0x10ffff
        || (result >= 0xd800 && result <= 0xdfff)) return false;
    *next = offset + width;
    *scalar = result;
    return true;
}

inline MatchResult SplitPath(const char* path, size_t size,
                             MatchScratch* scratch, uint16_t* count) {
    if (path == nullptr || scratch == nullptr || count == nullptr
        || size == 0 || size > pattern::kPatternLimitsProfileV1.max_path_bytes) {
        return MatchResult::kInvalidPathEncoding;
    }
    uint16_t components = 0;
    size_t start = 0;
    size_t cursor = 0;
    while (cursor <= size) {
        if (cursor != size && path[cursor] != '/') {
            size_t next = 0;
            uint32_t scalar = 0;
            if (!DecodeScalar(path, size, cursor, &next, &scalar)) {
                return MatchResult::kInvalidPathEncoding;
            }
            cursor = next;
            continue;
        }
        if (cursor == start || components >= 256) {
            return MatchResult::kInvalidPathEncoding;
        }
        scratch->path_components[components++] = {
            static_cast<uint16_t>(start),
            static_cast<uint16_t>(cursor - start),
        };
        start = ++cursor;
    }
    *count = components;
    return MatchResult::kNoMatch;
}

inline bool CharacterClassMatches(
        const policy_v6_view::CharacterClassRef& cls, uint32_t scalar) {
    bool member = false;
    if (scalar < 128) {
        const uint64_t word = scalar < 64 ? cls.low : cls.high;
        member = (word & (UINT64_C(1) << (scalar % 64))) != 0;
    }
    return cls.negated ? !member : member;
}

inline MatchResult MatchComponent(
        const policy_v6_view::PolicyV6View& policy,
        const PatternComponentSpan& pattern_component,
        const char* input, size_t input_size, MatchScratch* scratch) {
    uint16_t token = 0;
    size_t cursor = 0;
    uint16_t star_token = UINT16_MAX;
    size_t star_cursor = 0;
    while (cursor < input_size) {
        if (++scratch->transitions
            > pattern::kPatternLimitsProfileV1.matcher_transition_budget) {
            return MatchResult::kBudgetExceeded;
        }
        policy_v6_view::TokenRef current_token;
        if (token < pattern_component.token_count
            && !policy.TokenAt(pattern_component.first_token + token,
                               &current_token)) return MatchResult::kInvalidPolicy;
        if (token < pattern_component.token_count && current_token.kind == 1) {
            star_token = token++;
            star_cursor = cursor;
            continue;
        }
        bool matched = false;
        size_t next = cursor;
        if (token < pattern_component.token_count && current_token.kind == 0) {
            policy_v6_view::StringRef literal;
            if (!policy.StringAt(current_token.operand, &literal)) {
                return MatchResult::kInvalidPolicy;
            }
            matched = literal.size <= input_size - cursor
                && memcmp(input + cursor, literal.data, literal.size) == 0;
            next = cursor + literal.size;
        } else if (token < pattern_component.token_count
                   && (current_token.kind == 2 || current_token.kind == 4)) {
            uint32_t scalar = 0;
            if (!DecodeScalar(input, input_size, cursor, &next, &scalar)) {
                return MatchResult::kInvalidPathEncoding;
            }
            matched = current_token.kind == 2;
            if (current_token.kind == 4) {
                policy_v6_view::CharacterClassRef cls;
                if (!policy.CharacterClassAt(current_token.operand, &cls)) {
                    return MatchResult::kInvalidPolicy;
                }
                matched = CharacterClassMatches(cls, scalar);
            }
        }
        if (matched) {
            ++token;
            cursor = next;
            continue;
        }
        if (star_token == UINT16_MAX) return MatchResult::kNoMatch;
        uint32_t ignored = 0;
        if (!DecodeScalar(input, input_size, star_cursor, &star_cursor, &ignored)) {
            return MatchResult::kInvalidPathEncoding;
        }
        cursor = star_cursor;
        token = star_token + 1;
    }
    while (token < pattern_component.token_count) {
        policy_v6_view::TokenRef trailing;
        if (!policy.TokenAt(pattern_component.first_token + token, &trailing)) {
            return MatchResult::kInvalidPolicy;
        }
        if (trailing.kind != 1) return MatchResult::kNoMatch;
        ++token;
    }
    return MatchResult::kMatch;
}

inline MatchResult MatchPattern(
        const policy_v6_view::PolicyV6View& policy, uint32_t pattern_id,
        const char* relative_path, size_t relative_size, MatchScratch* scratch) {
    if (scratch == nullptr) return MatchResult::kInvalidPolicy;
    *scratch = {};
    policy_v6_view::PatternRef pattern_ref;
    if (!policy.PatternAt(pattern_id, &pattern_ref)
        || pattern_ref.component_count > 64) return MatchResult::kInvalidPolicy;
    uint16_t path_count = 0;
    const MatchResult split = SplitPath(relative_path, relative_size, scratch,
                                        &path_count);
    if (split == MatchResult::kInvalidPathEncoding) return split;
    uint16_t component_count = 0;
    uint32_t component_first = pattern_ref.first_token;
    for (uint16_t i = 0; i <= pattern_ref.token_count; ++i) {
        policy_v6_view::TokenRef token;
        const bool separator = i == pattern_ref.token_count
            || (policy.TokenAt(pattern_ref.first_token + i, &token)
                && token.kind == 5);
        if (!separator) continue;
        if (component_count >= 64 || component_first >= pattern_ref.first_token + i) {
            return MatchResult::kInvalidPolicy;
        }
        policy_v6_view::TokenRef first;
        if (!policy.TokenAt(component_first, &first)) return MatchResult::kInvalidPolicy;
        scratch->pattern_components[component_count++] = {
            component_first,
            static_cast<uint16_t>(pattern_ref.first_token + i - component_first),
            first.kind == 3,
        };
        component_first = pattern_ref.first_token + i + 1;
    }
    if (component_count != pattern_ref.component_count) return MatchResult::kInvalidPolicy;
    scratch->current[0] = true;
    for (uint16_t p = 0; p < component_count; ++p) {
        memset(scratch->next, 0, sizeof(scratch->next));
        const PatternComponentSpan& component = scratch->pattern_components[p];
        if (component.globstar) {
            const bool trailing = p + 1 == component_count;
            if (!trailing) {
                memcpy(scratch->next, scratch->current,
                       (path_count + 1) * sizeof(bool));
            }
            for (uint16_t i = 0; i < path_count; ++i) {
                if (++scratch->transitions
                    > pattern::kPatternLimitsProfileV1.matcher_transition_budget) {
                    return MatchResult::kBudgetExceeded;
                }
                const bool can_consume = trailing
                    ? scratch->current[i] || scratch->next[i]
                    : scratch->next[i];
                if (can_consume) scratch->next[i + 1] = true;
            }
        } else {
            for (uint16_t i = 0; i < path_count; ++i) {
                if (!scratch->current[i]) continue;
                const ComponentSpan& path_component = scratch->path_components[i];
                const MatchResult result = MatchComponent(
                    policy, component, relative_path + path_component.offset,
                    path_component.size, scratch);
                if (result == MatchResult::kBudgetExceeded
                    || result == MatchResult::kInvalidPathEncoding
                    || result == MatchResult::kInvalidPolicy) return result;
                if (result == MatchResult::kMatch) scratch->next[i + 1] = true;
            }
        }
        memcpy(scratch->current, scratch->next, sizeof(scratch->current));
    }
    return scratch->current[path_count] ? MatchResult::kMatch
                                        : MatchResult::kNoMatch;
}

inline uint16_t PatternSpecificity(
        const policy_v6_view::PolicyV6View& policy, uint32_t pattern_id) {
    policy_v6_view::PatternRef pattern;
    if (!policy.PatternAt(pattern_id, &pattern)) return 0;
    uint32_t score = 0;
    for (uint16_t i = 0; i < pattern.token_count; ++i) {
        policy_v6_view::TokenRef token;
        if (!policy.TokenAt(pattern.first_token + i, &token)) return 0;
        if (token.kind == 0) score += 16;
        else if (token.kind == 1) score += 2;
        else if (token.kind == 2) score += 4;
        else if (token.kind == 3) score += 1;
        else if (token.kind == 4) score += 8;
    }
    return static_cast<uint16_t>(score > UINT16_MAX ? UINT16_MAX : score);
}

}  // namespace pathguard::policy_pattern_runtime
