#include "pathguard/validation.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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

    std::unordered_map<std::string, std::size_t> redirect_sources;
    std::unordered_set<std::string> deny_sources;
    redirect_sources.reserve(policy->rules.size());
    deny_sources.reserve(policy->rules.size());
    for (const Rule& rule : policy->rules) {
        if (rule.action == RuleAction::kRedirect) {
            redirect_sources.emplace(rule.source, rule.line);
        } else {
            deny_sources.insert(rule.source);
        }
    }

    for (const Rule& rule : policy->rules) {
        if (rule.action != RuleAction::kRedirect) continue;
        const auto chained_redirect = redirect_sources.find(rule.target);
        if (rule.source != rule.target && chained_redirect != redirect_sources.end()) {
            if (error != nullptr) *error = {chained_redirect->second, "redirect cycle"};
            return false;
        }

        std::string ancestor = rule.source;
        while (true) {
            const std::size_t separator = ancestor.rfind('/');
            if (separator == std::string::npos || separator == 0) break;
            ancestor.resize(separator);
            if (deny_sources.contains(ancestor)) {
                if (error != nullptr) *error = {rule.line, "parent rule conflicts with child rule"};
                return false;
            }
        }
    }
    return true;
}

}  // namespace pathguard
