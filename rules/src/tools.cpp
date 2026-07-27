#include "pathguard/rules/tools.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <tuple>
#include <utility>

namespace pathguard::rules {
namespace {

struct PathRef {
    std::string_view bytes;
    RuleId id = 0;
};

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

ByteSpan Origin(const OriginMap& origins, RuleId id) {
    const RuleOrigin* origin = origins.Find(id);
    return origin == nullptr ? ByteSpan{} : origin->primary;
}

using PlanKey = std::pair<std::string, std::string>;

std::map<PlanKey, std::string> IndexPolicy(const CanonicalPolicy& policy) {
    std::map<PlanKey, std::string> output;
    for (const CanonicalAppPolicy& app : policy.apps) {
        for (const CanonicalRedirectRule& redirect : app.redirects) {
            output.emplace(PlanKey{app.package, redirect.source.bytes},
                           redirect.target.bytes);
        }
    }
    return output;
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
    if (!result.resolved.has_value()) return output;
    if (result.resolved->allow_legacy_mount
        && output.size() < limits.max_diagnostics) {
        Diagnostic legacy{kLintLegacy, "rules.lint_legacy", {}, false};
        legacy.severity = DiagnosticSeverity::kWarning;
        legacy.phase = DiagnosticPhase::kSemantic;
        output.push_back(std::move(legacy));
    }
    for (const ResolvedAppPolicy& app : result.resolved->apps) {
        std::vector<PathRef> paths;
        for (const ResolvedDenyRule& deny : app.deny) {
            paths.push_back({deny.path.bytes, deny.id});
        }
        for (const ResolvedRedirectRule& redirect : app.redirects) {
            paths.push_back({redirect.source.bytes, redirect.id});
            paths.push_back({redirect.target.bytes, redirect.id});
        }
        std::map<std::string, PathRef> skeletons;
        for (const PathRef& path : paths) {
            const std::string skeleton = VisualSkeleton(path.bytes);
            const auto [found, inserted] = skeletons.emplace(skeleton, path);
            if (!inserted && found->second.bytes != path.bytes
                && output.size() < limits.max_diagnostics) {
                Diagnostic unicode{kLintUnicode, "rules.lint_unicode_near",
                                   Origin(result.origins, path.id), false};
                unicode.severity = DiagnosticSeverity::kWarning;
                unicode.phase = DiagnosticPhase::kSemantic;
                unicode.related.push_back(
                    {Origin(result.origins, found->second.id), "near_path"});
                output.push_back(std::move(unicode));
            }
        }
    }
    return output;
}

std::vector<PolicyChange> BuildPolicyPlan(const CanonicalPolicy& before,
                                          const CanonicalPolicy& after) {
    const auto old_index = IndexPolicy(before);
    const auto new_index = IndexPolicy(after);
    std::vector<PolicyChange> output;
    auto old_rule = old_index.begin();
    auto new_rule = new_index.begin();
    while (old_rule != old_index.end() || new_rule != new_index.end()) {
        if (new_rule == new_index.end()
            || (old_rule != old_index.end() && old_rule->first < new_rule->first)) {
            output.push_back({PolicyChangeKind::kRemove, old_rule->first.first,
                              old_rule->first.second, old_rule->second, {}});
            ++old_rule;
        } else if (old_rule == old_index.end()
                   || new_rule->first < old_rule->first) {
            output.push_back({PolicyChangeKind::kAdd, new_rule->first.first,
                              new_rule->first.second, {}, new_rule->second});
            ++new_rule;
        } else {
            if (old_rule->second != new_rule->second) {
                output.push_back({PolicyChangeKind::kModify,
                                  old_rule->first.first, old_rule->first.second,
                                  old_rule->second, new_rule->second});
            }
            ++old_rule;
            ++new_rule;
        }
    }
    return output;
}

PathExplanation ExplainPath(const ResolvedPolicy& policy,
                            std::string_view package,
                            std::string_view path,
                            const RulesLimits& limits) {
    PathExplanation output;
    output.package = package;
    output.query = path;
    const auto query = NormalizeRulePath(path, limits);
    if (!query.has_value()) return output;
    const auto app = std::find_if(
        policy.apps.begin(), policy.apps.end(),
        [package](const ResolvedAppPolicy& item) {
            return item.package == package;
        });
    if (app == policy.apps.end()) return output;
    std::vector<const ResolvedRedirectRule*> matches;
    for (const ResolvedRedirectRule& redirect : app->redirects) {
        if (IsSameOrAncestor(redirect.source, *query)) {
            matches.push_back(&redirect);
        }
    }
    std::sort(matches.begin(), matches.end(),
              [](const auto* lhs, const auto* rhs) {
                  return std::tie(lhs->source.bytes, lhs->target.bytes)
                      < std::tie(rhs->source.bytes, rhs->target.bytes);
              });
    if (matches.empty()) return output;
    const auto longest = std::max_element(
        matches.begin(), matches.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->source.bytes.size() < rhs->source.bytes.size();
        });
    output.source = (*longest)->source.bytes;
    output.target = (*longest)->target.bytes;
    for (const ResolvedRedirectRule* match : matches) {
        if (match != *longest) output.shadowed_parents.push_back(match->source.bytes);
    }
    std::sort(output.shadowed_parents.begin(), output.shadowed_parents.end());
    return output;
}

}  // namespace pathguard::rules
