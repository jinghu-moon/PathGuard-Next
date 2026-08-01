#include "pathguard/rules/tools.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <tuple>
#include <utility>

namespace pathguard::rules {
namespace {

std::uint32_t DecodeCodePoint(std::string_view input, std::size_t* offset) {
    const auto lead = static_cast<unsigned char>(input[*offset]);
    std::size_t width = 1;
    std::uint32_t value = lead;
    if ((lead & 0xe0U) == 0xc0U) {
        width = 2;
        value = lead & 0x1fU;
    } else if ((lead & 0xf0U) == 0xe0U) {
        width = 3;
        value = lead & 0x0fU;
    } else if ((lead & 0xf8U) == 0xf0U) {
        width = 4;
        value = lead & 0x07U;
    }
    if (*offset + width > input.size()) {
        ++*offset;
        return lead;
    }
    for (std::size_t index = 1; index < width; ++index) {
        const auto byte = static_cast<unsigned char>(input[*offset + index]);
        if ((byte & 0xc0U) != 0x80U) {
            ++*offset;
            return lead;
        }
        value = (value << 6U) | (byte & 0x3fU);
    }
    *offset += width;
    return value;
}

void AppendCodePoint(std::uint32_t value, std::string* output) {
    if (value <= 0x7fU) {
        output->push_back(static_cast<char>(value));
    } else if (value <= 0x7ffU) {
        output->push_back(static_cast<char>(0xc0U | (value >> 6U)));
        output->push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else if (value <= 0xffffU) {
        output->push_back(static_cast<char>(0xe0U | (value >> 12U)));
        output->push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        output->push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else {
        output->push_back(static_cast<char>(0xf0U | (value >> 18U)));
        output->push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
        output->push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        output->push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    }
}

std::string VisualSkeleton(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t offset = 0; offset < input.size();) {
        std::uint32_t code_point = DecodeCodePoint(input, &offset);
        if (code_point >= 0x0300U && code_point <= 0x036fU) continue;
        if (code_point >= 0xff01U && code_point <= 0xff5eU) {
            code_point -= 0xfee0U;
        }
        if (code_point <= 0x7fU) {
            code_point = static_cast<unsigned char>(
                std::tolower(static_cast<unsigned char>(code_point)));
        }
        AppendCodePoint(code_point, &output);
    }
    return output;
}

PolicyRuleKind ToRuleKind(RuleActionKind kind) {
    switch (kind) {
        case RuleActionKind::kDeny: return PolicyRuleKind::kDeny;
        case RuleActionKind::kRedirect: return PolicyRuleKind::kRedirect;
        case RuleActionKind::kObserve: return PolicyRuleKind::kObserve;
        case RuleActionKind::kExport: return PolicyRuleKind::kExport;
    }
    return PolicyRuleKind::kDeny;
}

std::string SelectorText(const CanonicalSelectorV2& selector) {
    return selector.root + "/" + selector.glob;
}

using PlanKey = std::tuple<std::string, PolicyRuleKind, std::string>;

std::map<PlanKey, std::string> IndexPolicy(const CanonicalPolicyV2& policy) {
    std::map<PlanKey, std::string> output;
    for (const CanonicalAppPolicyV2& app : policy.apps) {
        for (const CanonicalActionV2& action : app.actions) {
            output.emplace(
                PlanKey{app.package, ToRuleKind(action.action),
                        SelectorText(action.selector)},
                action.target);
        }
    }
    return output;
}

bool MatchesSelector(const CanonicalSelectorV2& selector,
                     std::string_view query) {
    if (query.size() <= selector.root.size()
        || query.compare(0, selector.root.size(), selector.root) != 0
        || query[selector.root.size()] != '/') {
        return false;
    }
    const std::string_view relative = query.substr(selector.root.size() + 1);
    if (selector.source_kind == SelectorSourceKind::kLiteral) {
        const std::string source = SelectorText(selector);
        return query == source
            || (query.size() > source.size()
                && query.compare(0, source.size(), source) == 0
                && query[source.size()] == '/');
    }
    pathguard::pattern::PatternMatchScratch scratch;
    if (pathguard::pattern::MatchPattern(selector.base_pattern, relative, &scratch)
        != pathguard::pattern::PatternMatchResult::kMatch) {
        return false;
    }
    for (const auto& except : selector.except_patterns) {
        if (pathguard::pattern::MatchPattern(except, relative, &scratch)
            == pathguard::pattern::PatternMatchResult::kMatch) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<Diagnostic> LintRules(const RulesBuildResult& result,
                                  const RulesLimits& limits) {
    std::vector<Diagnostic> output;
    for (const Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::kWarning) {
            output.push_back(diagnostic);
        }
    }
    if (!result.canonical_v2.has_value()) return output;
    if (result.canonical_v2->allow_legacy_mount
        && output.size() < limits.max_diagnostics) {
        Diagnostic legacy{kLintLegacy, "rules.lint_legacy", {}, false};
        legacy.severity = DiagnosticSeverity::kWarning;
        legacy.phase = DiagnosticPhase::kSemantic;
        output.push_back(std::move(legacy));
    }
    for (const CanonicalAppPolicyV2& app : result.canonical_v2->apps) {
        std::map<std::string, std::string> skeletons;
        std::map<std::tuple<RuleActionKind, std::string, std::string>, RuleId>
            rules;
        for (const CanonicalActionV2& action : app.actions) {
            const auto rule_key = std::make_tuple(
                action.action, SelectorText(action.selector), action.target);
            if (!rules.emplace(rule_key, action.id).second
                && output.size() < limits.max_diagnostics) {
                Diagnostic redundant{kRuleRedundant, "rules.rule_redundant",
                                     {}, false};
                redundant.severity = DiagnosticSeverity::kWarning;
                redundant.phase = DiagnosticPhase::kSemantic;
                output.push_back(std::move(redundant));
            }
            std::vector<std::string> paths{
                SelectorText(action.selector), action.target};
            for (const std::string& path : paths) {
                if (path.empty()) continue;
                const std::string skeleton = VisualSkeleton(path);
                const auto [found, inserted] = skeletons.emplace(skeleton, path);
                if (!inserted && found->second != path
                    && output.size() < limits.max_diagnostics) {
                    Diagnostic unicode{kLintUnicode, "rules.lint_unicode_near",
                                       {}, false};
                    unicode.severity = DiagnosticSeverity::kWarning;
                    unicode.phase = DiagnosticPhase::kSemantic;
                    output.push_back(std::move(unicode));
                }
            }
        }
    }
    return output;
}

std::vector<PolicyChange> BuildPolicyPlan(const CanonicalPolicyV2& before,
                                          const CanonicalPolicyV2& after) {
    const auto old_index = IndexPolicy(before);
    const auto new_index = IndexPolicy(after);
    std::vector<PolicyChange> output;
    auto old_rule = old_index.begin();
    auto new_rule = new_index.begin();
    while (old_rule != old_index.end() || new_rule != new_index.end()) {
        if (new_rule == new_index.end()
            || (old_rule != old_index.end() && old_rule->first < new_rule->first)) {
            output.push_back({PolicyChangeKind::kRemove,
                              std::get<1>(old_rule->first),
                              std::get<0>(old_rule->first),
                              std::get<2>(old_rule->first),
                              old_rule->second, {}});
            ++old_rule;
        } else if (old_rule == old_index.end()
                   || new_rule->first < old_rule->first) {
            output.push_back({PolicyChangeKind::kAdd,
                              std::get<1>(new_rule->first),
                              std::get<0>(new_rule->first),
                              std::get<2>(new_rule->first),
                              {}, new_rule->second});
            ++new_rule;
        } else {
            if (old_rule->second != new_rule->second) {
                output.push_back({PolicyChangeKind::kModify,
                                  std::get<1>(old_rule->first),
                                  std::get<0>(old_rule->first),
                                  std::get<2>(old_rule->first),
                                  old_rule->second, new_rule->second});
            }
            ++old_rule;
            ++new_rule;
        }
    }
    return output;
}

PathExplanation ExplainPath(const CanonicalPolicyV2& policy,
                            std::string_view package,
                            std::string_view path,
                            const RulesLimits& limits) {
    PathExplanation output;
    output.package = package;
    output.query = path;
    if (!NormalizeRulePath(path, limits).has_value()) return output;
    const auto app = std::find_if(
        policy.apps.begin(), policy.apps.end(),
        [package](const CanonicalAppPolicyV2& item) {
            return item.package == package;
        });
    if (app == policy.apps.end()) return output;

    const CanonicalActionV2* winner = nullptr;
    std::vector<const CanonicalActionV2*> matches;
    for (const CanonicalActionV2& action : app->actions) {
        if (!MatchesSelector(action.selector, path)) continue;
        matches.push_back(&action);
        if (winner == nullptr
            || std::tie(action.priority, action.selector.specificity, action.id)
                > std::tie(winner->priority, winner->selector.specificity,
                           winner->id)) {
            winner = &action;
        }
    }
    if (winner == nullptr) return output;
    output.action = ToRuleKind(winner->action);
    output.source = SelectorText(winner->selector);
    if (!winner->target.empty()) output.target = winner->target;
    for (const CanonicalActionV2* match : matches) {
        if (match != winner) {
            output.shadowed_parents.push_back(SelectorText(match->selector));
        }
    }
    std::sort(output.shadowed_parents.begin(), output.shadowed_parents.end());
    return output;
}

}  // namespace pathguard::rules
