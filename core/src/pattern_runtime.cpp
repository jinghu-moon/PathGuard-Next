#include "pathguard/pattern_runtime.h"

#include <algorithm>
#include <cerrno>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace pathguard::pattern {
namespace {

struct IdentityHash {
    std::size_t operator()(const IdentityKey& value) const noexcept {
        const auto uid = static_cast<std::uint32_t>(value.caller_uid);
        return (static_cast<std::size_t>(uid) << 1U)
            ^ static_cast<std::size_t>(value.user_id);
    }
};

struct RootBucket {
    std::map<std::string, std::vector<CandidateRef>, std::less<>> literal;
    std::map<std::string, std::vector<CandidateRef>, std::less<>> extension;
    std::vector<CandidateRef> general;
};

struct PackageBuckets {
    std::map<std::string, RootBucket, std::less<>> roots;
};

struct IdentityBuckets {
    std::unordered_map<PackageId, PackageBuckets> packages;
};

std::string_view FirstComponent(std::string_view path) noexcept {
    return path.substr(0, path.find('/'));
}

std::string_view Extension(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    const std::string_view name = slash == std::string_view::npos
        ? path : path.substr(slash + 1);
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) {
        return {};
    }
    return name.substr(dot + 1);
}

bool ObjectMatches(ObjectType selector, ObjectType operand) noexcept {
    return selector == ObjectType::kAny || selector == operand;
}

int PrimaryPrecedence(RuntimeActionKind kind) noexcept {
    if (kind == RuntimeActionKind::kDeny) return 2;
    if (kind == RuntimeActionKind::kRedirect) return 1;
    return 0;
}

bool Better(const PlanAction& candidate, std::uint16_t candidate_specificity,
            const PlanAction& current, std::uint16_t current_specificity) {
    return std::tuple(PrimaryPrecedence(candidate.kind), candidate.priority,
                      candidate_specificity,
                      std::numeric_limits<RuntimeRuleId>::max()
                          - candidate.rule_id)
        > std::tuple(PrimaryPrecedence(current.kind), current.priority,
                     current_specificity,
                     std::numeric_limits<RuntimeRuleId>::max()
                         - current.rule_id);
}

}  // namespace

struct CandidateIndex::Impl {
    std::unordered_map<IdentityKey, IdentityBuckets, IdentityHash> identities;
};

std::unique_ptr<MatcherSnapshot> MatcherSnapshot::Build(
    PatternPlan plan, std::string* error,
    const PatternLimitsProfile& limits) {
    auto index = CandidateIndex::Build(plan, error, limits);
    if (!index.has_value()) return nullptr;
    std::size_t bytes = sizeof(MatcherSnapshot);
    for (const PlanPackage& package : plan.packages) bytes += package.name.size();
    for (const PlanSelector& selector : plan.selectors) {
        bytes += selector.root.size() + selector.base.canonical.size()
            + selector.first_literal_component.size()
            + selector.fixed_extension.size();
        for (const PatternProgram& except : selector.except) {
            bytes += except.canonical.size();
        }
    }
    for (const PlanAction& action : plan.actions) bytes += action.target.size();
    return std::unique_ptr<MatcherSnapshot>(new MatcherSnapshot(
        std::move(plan), std::move(*index), bytes));
}

std::optional<CandidateIndex> CandidateIndex::Build(
    const PatternPlan& plan, std::string* error,
    const PatternLimitsProfile& limits) {
    auto fail = [&](std::string message) -> std::optional<CandidateIndex> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    auto impl = std::make_shared<Impl>();

    std::unordered_map<std::uint64_t, std::vector<ActionId>> grouped_actions;
    std::unordered_map<PackageId, std::set<SelectorId>> general_by_package;
    for (std::size_t index = 0; index < plan.actions.size(); ++index) {
        const PlanAction& action = plan.actions[index];
        if (action.id != index || action.selector_id >= plan.selectors.size()
            || action.package_id >= plan.packages.size()) {
            return fail("invalid action reference");
        }
        if (!action.active) continue;
        const std::uint64_t key =
            (static_cast<std::uint64_t>(action.package_id) << 32U)
            | action.selector_id;
        grouped_actions[key].push_back(action.id);
        const PlanSelector& selector = plan.selectors[action.selector_id];
        if (selector.first_literal_component.empty()
            && selector.fixed_extension.empty()) {
            general_by_package[action.package_id].insert(action.selector_id);
        }
    }
    for (const auto& [package, selectors] : general_by_package) {
        (void)package;
        if (selectors.size() > limits.max_degenerate_patterns_per_app) {
            return fail("degenerate pattern/package limit");
        }
    }

    std::set<std::tuple<std::int32_t, std::uint32_t, PackageId>> grants;
    for (const ScopeGrant& grant : plan.scopes) {
        if (grant.package_id >= plan.packages.size()) {
            return fail("invalid scope package");
        }
        const PackageId bucket_package = grant.requires_package_attribution
            ? grant.package_id : kUnknownPackageId;
        const auto grant_key = std::tuple(grant.identity.caller_uid,
                                          grant.identity.user_id,
                                          bucket_package);
        if (!grants.insert(grant_key).second) {
            return fail("duplicate scope grant");
        }
        PackageBuckets& package_buckets =
            impl->identities[grant.identity].packages[bucket_package];
        for (const auto& [group_key, action_ids] : grouped_actions) {
            const PackageId action_package = static_cast<PackageId>(group_key >> 32U);
            if (action_package != grant.package_id) continue;
            const SelectorId selector_id = static_cast<SelectorId>(group_key);
            const PlanSelector& selector = plan.selectors[selector_id];
            RootBucket& root = package_buckets.roots[selector.root];
            CandidateRef candidate{selector_id, action_ids};
            if (!selector.first_literal_component.empty()) {
                root.literal[selector.first_literal_component].push_back(
                    std::move(candidate));
            } else if (!selector.fixed_extension.empty()) {
                root.extension[selector.fixed_extension].push_back(
                    std::move(candidate));
            } else {
                root.general.push_back(std::move(candidate));
                if (root.general.size()
                    > limits.max_degenerate_patterns_per_root) {
                    return fail("degenerate pattern/root limit");
                }
            }
        }
    }

    for (auto& [identity, identity_buckets] : impl->identities) {
        (void)identity;
        for (auto& [package, package_buckets] : identity_buckets.packages) {
            (void)package;
            for (auto& [root_name, root] : package_buckets.roots) {
                (void)root_name;
                auto validate = [&](auto& buckets) {
                    for (auto& [key, candidates] : buckets) {
                        (void)key;
                        std::sort(candidates.begin(), candidates.end(),
                            [](const CandidateRef& lhs, const CandidateRef& rhs) {
                                return lhs.selector_id < rhs.selector_id;
                            });
                        if (candidates.size() > limits.max_candidates_per_bucket) {
                            return false;
                        }
                    }
                    return true;
                };
                if (!validate(root.literal) || !validate(root.extension)
                    || root.general.size() > limits.max_candidates_per_bucket) {
                    return fail("candidate/bucket limit");
                }
                std::sort(root.general.begin(), root.general.end(),
                    [](const CandidateRef& lhs, const CandidateRef& rhs) {
                        return lhs.selector_id < rhs.selector_id;
                    });
            }
        }
    }
    return CandidateIndex(std::move(impl));
}

CandidateLookup CandidateIndex::Lookup(
    const IdentityKey& identity, AttributionKind attribution,
    PackageId package_id, std::string_view root,
    std::string_view relative_path) const noexcept {
    CandidateLookup output;
    if (impl_ == nullptr || relative_path.empty()) return output;
    const auto identity_found = impl_->identities.find(identity);
    if (identity_found == impl_->identities.end()) return output;

    const auto append_package = [&](PackageId candidate_package) {
        const auto package_found =
            identity_found->second.packages.find(candidate_package);
        if (package_found == identity_found->second.packages.end()) return;
        const auto root_found = package_found->second.roots.find(root);
        if (root_found == package_found->second.roots.end()) return;
        const RootBucket& bucket = root_found->second;
        const auto literal = bucket.literal.find(FirstComponent(relative_path));
        if (literal != bucket.literal.end() && output.span_count < output.spans.size()) {
            output.spans[output.span_count++] = literal->second;
        }
        const std::string_view extension = Extension(relative_path);
        const auto extension_found = bucket.extension.find(extension);
        if (!extension.empty() && extension_found != bucket.extension.end()
            && output.span_count < output.spans.size()) {
            output.spans[output.span_count++] = extension_found->second;
        }
        if (!bucket.general.empty() && output.span_count < output.spans.size()) {
            output.spans[output.span_count++] = bucket.general;
        }
    };

    if (attribution == AttributionKind::kVerifiedPackage
        && package_id != kUnknownPackageId) {
        append_package(package_id);
    }
    append_package(kUnknownPackageId);
    return output;
}

MatchSet PatternEngine::MatchOperand(const OperationContext& context,
                                     std::size_t operand_index,
                                     RuntimeMatchScratch* scratch) const noexcept {
    if (scratch == nullptr || operand_index >= context.operands.size()) {
        return {{}, DecisionReason::kRuntimeUnavailable};
    }
    const PathOperand& operand = context.operands[operand_index];
    const CandidateLookup candidates = index_.Lookup(
        context.identity, context.attribution, context.subject_package,
        operand.root, operand.relative_path);
    if (candidates.empty()) return {{}, DecisionReason::kNoMatch};

    std::size_t matched_count = 0;
    for (std::size_t span_index = 0;
         span_index < candidates.span_count; ++span_index) {
        for (const CandidateRef& candidate : candidates.spans[span_index]) {
            if (candidate.selector_id >= plan_.selectors.size()) {
                return {{}, DecisionReason::kRuntimeUnavailable};
            }
            bool duplicate = false;
            for (std::size_t index = 0; index < matched_count; ++index) {
                duplicate = duplicate
                    || scratch->matched[index].selector_id == candidate.selector_id;
            }
            if (duplicate) continue;
            const PlanSelector& selector = plan_.selectors[candidate.selector_id];
            if (!ObjectMatches(selector.object_type, operand.object_type)) continue;
            matcher_invocations_.fetch_add(1, std::memory_order_relaxed);
            const PatternMatchResult base = MatchPattern(
                selector.base, operand.relative_path, &scratch->pattern);
            if (base == PatternMatchResult::kInvalidPathEncoding) {
                return {{}, DecisionReason::kInvalidPathEncoding};
            }
            if (base == PatternMatchResult::kBudgetExceeded) {
                return {{}, DecisionReason::kBudgetExceeded};
            }
            if (base != PatternMatchResult::kMatch) continue;

            bool excluded = false;
            for (const PatternProgram& except : selector.except) {
                matcher_invocations_.fetch_add(1, std::memory_order_relaxed);
                const PatternMatchResult except_result = MatchPattern(
                    except, operand.relative_path, &scratch->pattern);
                if (except_result == PatternMatchResult::kInvalidPathEncoding) {
                    return {{}, DecisionReason::kInvalidPathEncoding};
                }
                if (except_result == PatternMatchResult::kBudgetExceeded) {
                    return {{}, DecisionReason::kBudgetExceeded};
                }
                if (except_result == PatternMatchResult::kMatch) {
                    excluded = true;
                    break;
                }
            }
            if (excluded) continue;
            if (matched_count >= scratch->matched.size()) {
                return {{}, DecisionReason::kBudgetExceeded};
            }
            scratch->matched[matched_count++] = {
                candidate.selector_id, selector.specificity,
                candidate.action_ids.data(),
                static_cast<std::uint16_t>(candidate.action_ids.size())};
        }
    }
    return {std::span<const MatchedSelector>(scratch->matched.data(), matched_count),
            matched_count == 0 ? DecisionReason::kNoMatch
                               : DecisionReason::kMatched};
}

Decision ActionEvaluator::EvaluateOperand(const MatchSet& matches) const noexcept {
    Decision output;
    output.reason = matches.reason;
    output.plan_generation = plan_.plan_generation;
    output.capability_generation = plan_.capability_generation;
    if (matches.reason != DecisionReason::kMatched) return output;

    const PlanAction* winner = nullptr;
    std::uint16_t winner_specificity = 0;
    for (const MatchedSelector& matched : matches.matches) {
        for (std::uint16_t index = 0; index < matched.action_count; ++index) {
            const ActionId action_id = matched.action_ids[index];
            if (action_id >= plan_.actions.size()) {
                output.reason = DecisionReason::kRuntimeUnavailable;
                return output;
            }
            const PlanAction& action = plan_.actions[action_id];
            if (!action.active) continue;
            if (action.kind == RuntimeActionKind::kObserve) {
                output.effects |= kEffectObserve;
                continue;
            }
            if (action.kind == RuntimeActionKind::kExport) {
                output.effects |= kEffectExport;
                continue;
            }
            if (winner == nullptr
                || Better(action, matched.specificity,
                          *winner, winner_specificity)) {
                winner = &action;
                winner_specificity = matched.specificity;
                output.selector_id = matched.selector_id;
            }
        }
    }
    if (winner == nullptr) {
        output.reason = output.effects == kEffectNone
            ? DecisionReason::kNoMatch : DecisionReason::kMatched;
        return output;
    }
    output.rule_id = winner->rule_id;
    output.target = winner->target;
    output.domain = winner->domain;
    if (winner->kind == RuntimeActionKind::kDeny) {
        output.primary = PrimaryDisposition::kDeny;
        output.reason = DecisionReason::kDenied;
    } else {
        output.primary = PrimaryDisposition::kRedirect;
        output.reason = DecisionReason::kMatched;
    }
    return output;
}

Decision ActionEvaluator::Evaluate(
    const OperationContext& context,
    std::span<const MatchSet> operand_matches) const noexcept {
    Decision combined;
    combined.plan_generation = plan_.plan_generation;
    combined.capability_generation = plan_.capability_generation;
    if (operand_matches.size() != context.operands.size()
        || operand_matches.empty() || operand_matches.size() > 2) {
        combined.reason = DecisionReason::kRuntimeUnavailable;
        return combined;
    }
    bool has_redirect = false;
    bool has_pass = false;
    ExecutionDomain redirect_domain = ExecutionDomain::kAppPath;
    for (const MatchSet& match : operand_matches) {
        const Decision current = EvaluateOperand(match);
        combined.effects |= current.effects;
        if (current.primary == PrimaryDisposition::kDeny) return current;
        if (current.reason == DecisionReason::kBudgetExceeded
            || current.reason == DecisionReason::kInvalidPathEncoding
            || current.reason == DecisionReason::kRuntimeUnavailable) {
            return current;
        }
        if (current.primary == PrimaryDisposition::kRedirect) {
            if (has_redirect && current.domain != redirect_domain) {
                combined.reason = DecisionReason::kRuntimeUnavailable;
                return combined;
            }
            if (!has_redirect) {
                combined = current;
                redirect_domain = current.domain;
            }
            has_redirect = true;
        } else {
            has_pass = true;
        }
    }
    if (operand_matches.size() == 2 && has_redirect && has_pass) {
        combined.primary = PrimaryDisposition::kPass;
        combined.reason = DecisionReason::kRuntimeUnavailable;
        return combined;
    }
    if (!has_redirect) {
        combined.primary = PrimaryDisposition::kPass;
        combined.reason = combined.effects == kEffectNone
            ? DecisionReason::kNoMatch : DecisionReason::kMatched;
    }
    return combined;
}

OperationPlan BuildOperationPlan(const OperationContext& context,
                                 std::span<const MatchSet> matches,
                                 const ActionEvaluator& evaluator) noexcept {
    OperationPlan output;
    if (matches.size() != context.operands.size() || matches.empty()
        || matches.size() > output.operands.size()) {
        output.reason = DecisionReason::kRuntimeUnavailable;
        output.error_number = EINVAL;
        return output;
    }
    output.operand_count = static_cast<std::uint8_t>(matches.size());
    bool has_redirect = false;
    bool has_pass = false;
    std::optional<ExecutionDomain> domain;
    for (std::size_t index = 0; index < matches.size(); ++index) {
        const Decision decision = evaluator.EvaluateOperand(matches[index]);
        output.effects |= decision.effects;
        output.plan_generation = decision.plan_generation;
        output.operands[index].disposition = decision.primary;
        output.operands[index].rule_id = decision.rule_id;
        output.operands[index].selector_id = decision.selector_id;
        if (decision.primary == PrimaryDisposition::kDeny) {
            output.reason = DecisionReason::kDenied;
            output.error_number = EACCES;
            return output;
        }
        if (decision.reason == DecisionReason::kBudgetExceeded
            || decision.reason == DecisionReason::kInvalidPathEncoding
            || decision.reason == DecisionReason::kRuntimeUnavailable) {
            output.reason = decision.reason;
            return output;
        }
        if (decision.primary != PrimaryDisposition::kRedirect) {
            has_pass = true;
            continue;
        }
        has_redirect = true;
        if (domain.has_value() && *domain != decision.domain) {
            output.reason = DecisionReason::kRuntimeUnavailable;
            output.error_number = EXDEV;
            return output;
        }
        domain = decision.domain;
        const std::string_view tail = context.operands[index].relative_path;
        const std::size_t required = decision.target.size() + 1 + tail.size();
        if (required >= output.operands[index].target.size()) {
            output.reason = DecisionReason::kUnsafeTarget;
            output.error_number = ENAMETOOLONG;
            return output;
        }
        std::copy(decision.target.begin(), decision.target.end(),
                  output.operands[index].target.begin());
        output.operands[index].target[decision.target.size()] = '/';
        std::copy(tail.begin(), tail.end(),
                  output.operands[index].target.begin()
                      + static_cast<std::ptrdiff_t>(decision.target.size() + 1));
        output.operands[index].target_size = static_cast<std::uint16_t>(required);
    }
    if (matches.size() == 2 && has_redirect && has_pass) {
        output.reason = DecisionReason::kRuntimeUnavailable;
        output.error_number = EXDEV;
        return output;
    }
    if (matches.size() == 2 && has_redirect
        && output.operands[0].target_path()
            == output.operands[1].target_path()) {
        output.reason = DecisionReason::kCollision;
        output.error_number = EEXIST;
        return output;
    }
    output.accepted = true;
    output.reason = has_redirect ? DecisionReason::kMatched
                                 : DecisionReason::kNoMatch;
    return output;
}

void AdmitPatternPlan(PatternPlan* plan,
                      const CapabilitySnapshot& snapshot) noexcept {
    if (plan == nullptr) return;
    plan->capability_generation = snapshot.capability_generation;
    for (PlanAction& action : plan->actions) {
        const ActionRequirement requirement{
            static_cast<AdmissionDomain>(action.domain),
            action.required_capabilities,
            action.required_operations,
            true,
        };
        action.admission = AdmitAction(requirement, snapshot,
                                       plan->plan_generation);
        action.active = action.admission.active();
    }
}

}  // namespace pathguard::pattern
