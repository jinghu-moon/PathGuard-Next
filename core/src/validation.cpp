#include "pathguard/validation.h"

#include <algorithm>
#include <unordered_map>

#include "pathguard/path.h"

namespace pathguard {

bool ValidatePolicy(AppPolicy* policy, ParseError* error) {
    if (policy == nullptr) {
        return false;
    }
    for (Rule& rule : policy->rules) {
        std::string normalized_source;
        if (!NormalizePath(rule.source, &normalized_source)) {
            if (error != nullptr) *error = {rule.line, "invalid source path"};
            return false;
        }
        rule.source = std::move(normalized_source);
        if (rule.action == RuleAction::kRedirect) {
            std::string expanded_target;
            if (!ExpandPackagePlaceholder(rule.target, policy->package, &expanded_target)
                || !NormalizePath(expanded_target, &expanded_target)
                || rule.source == expanded_target
                || IsPathOrDescendant(expanded_target, rule.source)) {
                if (error != nullptr) *error = {rule.line, "invalid redirect target"};
                return false;
            }
            rule.target = std::move(expanded_target);
        }
    }

    for (const Rule& lhs : policy->rules) {
        if (lhs.action != RuleAction::kRedirect) continue;
        for (const Rule& rhs : policy->rules) {
            if (rhs.action == RuleAction::kRedirect && lhs.target == rhs.source
                && lhs.source != rhs.source) {
                if (error != nullptr) *error = {rhs.line, "redirect cycle"};
                return false;
            }
            if (lhs.action != rhs.action && lhs.source != rhs.source
                && IsPathOrDescendant(lhs.source, rhs.source)) {
                if (error != nullptr) *error = {lhs.line, "parent rule conflicts with child rule"};
                return false;
            }
        }
    }
    return true;
}

}  // namespace pathguard
