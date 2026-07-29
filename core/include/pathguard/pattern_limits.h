#pragma once

#include <cstddef>

namespace pathguard::pattern {

struct PatternLimitsProfile {
    std::size_t max_selectors_per_app;
    std::size_t max_actions_per_app;
    std::size_t max_pattern_tokens;
    std::size_t max_pattern_tokens_per_app;
    std::size_t max_character_classes;
    std::size_t max_brace_expansions;
    std::size_t max_brace_expanded_bytes;
    std::size_t max_degenerate_patterns_per_root;
    std::size_t max_degenerate_patterns_per_app;
    std::size_t max_candidates_per_bucket;
    std::size_t max_match_set;
    std::size_t matcher_transition_budget;
    std::size_t max_except_patterns_per_selector;
    std::size_t max_except_refs_per_app;
};

inline constexpr PatternLimitsProfile kPatternLimitsProfileV1{
    .max_selectors_per_app = 256,
    .max_actions_per_app = 512,
    .max_pattern_tokens = 64,
    .max_pattern_tokens_per_app = 4096,
    .max_character_classes = 4096,
    .max_brace_expansions = 32,
    .max_brace_expanded_bytes = 64 * 1024,
    .max_degenerate_patterns_per_root = 16,
    .max_degenerate_patterns_per_app = 32,
    .max_candidates_per_bucket = 64,
    .max_match_set = 64,
    .matcher_transition_budget = 4096,
    .max_except_patterns_per_selector = 8,
    .max_except_refs_per_app = 256,
};

}  // namespace pathguard::pattern
