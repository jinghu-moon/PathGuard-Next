#pragma once

#include <cstddef>
#include <string_view>

namespace pathguard::rules {

struct RulesLimits {
    std::size_t max_source_bytes = 1024 * 1024;
    std::size_t max_container_depth = 128;
    std::size_t max_tokens_or_nodes = 65'536;
    std::size_t max_apps = 1024;
    std::size_t max_rules_per_app = 4096;
    std::size_t max_expanded_rules = 8192;
    std::size_t max_path_bytes = 4095;
    std::size_t max_path_components = 256;
    std::size_t max_string_token_bytes = 4096;
    std::size_t max_rewrites = 4096;
    std::size_t max_rewrite_segments = 24'576;
    std::size_t max_diagnostics = 64;
    std::size_t max_related_spans = 8;
    std::size_t max_generated_bytes = 2 * 1024 * 1024;
};

inline constexpr std::string_view kArrowContext = "PG-ARROW-CONTEXT";
inline constexpr std::string_view kArrowOperand = "PG-ARROW-OPERAND";
inline constexpr std::string_view kArrowChained = "PG-ARROW-CHAINED";
inline constexpr std::string_view kArrowMissingComma = "PG-ARROW-MISSING-COMMA";
inline constexpr std::string_view kArrowCommentInside = "PG-ARROW-COMMENT-INSIDE";
inline constexpr std::string_view kArrowStringBoundary = "PG-ARROW-STRING-BOUNDARY";
inline constexpr std::string_view kRuleArrowScope = "PG-RULE-ARROW-SCOPE";
inline constexpr std::string_view kRedirectSyntax = "PG-REDIRECT-SYNTAX";
inline constexpr std::string_view kDesugarInternal = "PG-DESUGAR-INTERNAL";
inline constexpr std::string_view kCompilerInternal = "PG-COMPILER-INTERNAL";
inline constexpr std::string_view kResourceLimit = "PG-RESOURCE-LIMIT";
inline constexpr std::string_view kFormatMissing = "PG-FORMAT-MISSING";
inline constexpr std::string_view kFormatInvalid = "PG-FORMAT-INVALID";
inline constexpr std::string_view kFormatUnsupported = "PG-FORMAT-UNSUPPORTED";
inline constexpr std::string_view kDiagnosticsOmitted = "PG-DIAGNOSTICS-OMITTED";
inline constexpr std::string_view kTomlParse = "PG-TOML-PARSE";
inline constexpr std::string_view kTypeMismatch = "PG-RULE-TYPE";
inline constexpr std::string_view kUnknownField = "PG-RULE-UNKNOWN-FIELD";
inline constexpr std::string_view kInvalidValue = "PG-RULE-INVALID-VALUE";
inline constexpr std::string_view kPathInvalid = "PG-PATH-INVALID";
inline constexpr std::string_view kRuleRedundant = "PG-RULE-REDUNDANT";
inline constexpr std::string_view kRuleConflict = "PG-RULE-CONFLICT";
inline constexpr std::string_view kRedirectCycle = "PG-REDIRECT-CYCLE";
inline constexpr std::string_view kProviderConflict = "PG-PROVIDER-CONFLICT";
inline constexpr std::string_view kExecutorUnavailable = "PG-EXECUTOR-UNAVAILABLE";
inline constexpr std::string_view kPolicyEncode = "PG-POLICY-ENCODE";

}  // namespace pathguard::rules
