#pragma once

#include <stddef.h>

namespace pathguard::pattern {

struct PatternLimitsProfile {
    size_t max_path_bytes;
    size_t max_path_components;
    size_t max_selectors_per_app;
    size_t max_actions_per_app;
    size_t max_pattern_tokens;
    size_t max_pattern_tokens_per_app;
    size_t max_character_classes;
    size_t max_brace_expansions;
    size_t max_brace_expanded_bytes;
    size_t max_degenerate_patterns_per_root;
    size_t max_degenerate_patterns_per_app;
    size_t max_candidates_per_bucket;
    size_t max_match_set;
    size_t matcher_transition_budget;
    size_t max_except_patterns_per_selector;
    size_t max_except_refs_per_app;
};

inline constexpr PatternLimitsProfile kPatternLimitsProfileV1{
    .max_path_bytes = 4095,
    .max_path_components = 256,
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
