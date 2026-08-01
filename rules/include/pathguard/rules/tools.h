#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/rules/semantic.h"

namespace pathguard::rules {

std::vector<Diagnostic> LintRules(const RulesBuildResult& result,
                                  const RulesLimits& limits);

enum class PolicyChangeKind {
    kAdd,
    kRemove,
    kModify,
};

enum class PolicyRuleKind {
    kDeny,
    kRedirect,
    kObserve,
    kExport,
};

struct PolicyChange {
    PolicyChangeKind kind = PolicyChangeKind::kAdd;
    PolicyRuleKind rule_kind = PolicyRuleKind::kRedirect;
    std::string package;
    std::string source;
    std::string before_target;
    std::string after_target;
};

std::vector<PolicyChange> BuildPolicyPlan(const CanonicalPolicyV2& before,
                                          const CanonicalPolicyV2& after);

struct PathExplanation {
    std::string package;
    std::string query;
    std::optional<PolicyRuleKind> action;
    std::optional<std::string> source;
    std::optional<std::string> target;
    std::vector<std::string> shadowed_parents;
};

PathExplanation ExplainPath(const CanonicalPolicyV2& policy,
                            std::string_view package,
                            std::string_view path,
                            const RulesLimits& limits);

}  // namespace pathguard::rules
