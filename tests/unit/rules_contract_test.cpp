#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

#include "pathguard/rules_contract.h"
#include "test_assert.h"

int main() {
    const pathguard::rules::RulesLimits limits;
    const std::array<std::pair<std::string_view, std::size_t>, 14> values{{
        {"source-bytes", limits.max_source_bytes},
        {"container-depth", limits.max_container_depth},
        {"tokens-or-nodes", limits.max_tokens_or_nodes},
        {"apps", limits.max_apps},
        {"rules-per-app", limits.max_rules_per_app},
        {"expanded-rules", limits.max_expanded_rules},
        {"path-bytes", limits.max_path_bytes},
        {"path-components", limits.max_path_components},
        {"string-token-bytes", limits.max_string_token_bytes},
        {"rewrites", limits.max_rewrites},
        {"rewrite-segments", limits.max_rewrite_segments},
        {"diagnostics", limits.max_diagnostics},
        {"related-spans", limits.max_related_spans},
        {"generated-bytes", limits.max_generated_bytes},
    }};
    for (const auto& [name, limit] : values) {
        assert(!name.empty());
        assert(limit > 0);
        const auto accepted = [limit](std::size_t value) { return value <= limit; };
        assert(accepted(limit - 1));
        assert(accepted(limit));
        assert(!accepted(limit + 1));
    }

    using namespace pathguard::rules;
    const std::array codes{
        kArrowContext,
        kArrowOperand,
        kArrowChained,
        kArrowMissingComma,
        kArrowCommentInside,
        kArrowStringBoundary,
        kRuleArrowScope,
        kRedirectSyntax,
        kDesugarInternal,
        kCompilerInternal,
        kResourceLimit,
    };
    for (std::string_view code : codes) {
        assert(code.starts_with("PG-"));
    }
    return 0;
}
