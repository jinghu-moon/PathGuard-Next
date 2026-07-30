#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pathguard/action_admission.h"
#include "pathguard/policy_pattern_runtime.h"

namespace pathguard::policy_action_router {

enum class Disposition : uint8_t { kPass, kDeny, kRedirect };

enum class Reason : uint8_t {
    kNoMatch,
    kMatched,
    kDenied,
    kCapabilityMissing,
    kOperationMissing,
    kRuntimeUnavailable,
    kBudgetExceeded,
    kInvalidPathEncoding,
};

struct Request {
    policy_v6_view::PackageRef package;
    const char* root = nullptr;
    size_t root_size = 0;
    const char* relative_path = nullptr;
    size_t relative_size = 0;
    uint8_t object_type = 1;
    AdmissionDomain domain = AdmissionDomain::kAppPath;
    OperationMask operation = 0;
};

struct Result {
    Disposition disposition = Disposition::kPass;
    Reason reason = Reason::kNoMatch;
    uint64_t rule_id = 0;
    uint32_t selector_id = 0;
    policy_v6_view::StringRef target;
    ActionAdmission admission;
    uint16_t specificity = 0;
    uint32_t matcher_invocations = 0;
    uint8_t collision_mode = 0;
    uint8_t reverse_mode = 0;
};

inline bool SameBytes(const policy_v6_view::StringRef& value,
                      const char* bytes, size_t size) {
    return value.Equals(bytes, size);
}

inline bool LiteralSelectorMatches(const policy_v6_view::StringRef& selector,
                                   const Request& request) {
    if (selector.size < request.root_size
        || memcmp(selector.data, request.root, request.root_size) != 0
        || (selector.size != request.root_size
            && selector.data[request.root_size] != '/')) return false;
    if (selector.size == request.root_size) return true;
    const size_t suffix_size = selector.size - request.root_size - 1;
    if (suffix_size > request.relative_size
        || memcmp(selector.data + request.root_size + 1,
                  request.relative_path, suffix_size) != 0) return false;
    return suffix_size == request.relative_size
        || request.relative_path[suffix_size] == '/';
}

inline bool CandidateCacheMatches(
        const policy_v6_view::PolicyV6View& policy,
        const policy_v6_view::SelectorRef& selector,
        const Request& request) {
    if (selector.first_literal_id != binary_format::kInvalidId) {
        policy_v6_view::StringRef literal;
        if (!policy.StringAt(selector.first_literal_id, &literal)) return false;
        const char* slash = static_cast<const char*>(
            memchr(request.relative_path, '/', request.relative_size));
        const size_t first_size = slash == nullptr ? request.relative_size
            : static_cast<size_t>(slash - request.relative_path);
        if (!literal.Equals(request.relative_path, first_size)) return false;
    } else if (selector.extension_id != binary_format::kInvalidId) {
        policy_v6_view::StringRef extension;
        if (!policy.StringAt(selector.extension_id, &extension)) return false;
        const char* name = request.relative_path;
        for (size_t i = 0; i < request.relative_size; ++i) {
            if (request.relative_path[i] == '/') name = request.relative_path + i + 1;
        }
        const size_t name_size = request.relative_path + request.relative_size - name;
        const char* dot = nullptr;
        for (size_t i = 0; i < name_size; ++i) if (name[i] == '.') dot = name + i;
        if (dot == nullptr || !extension.Equals(dot + 1,
                static_cast<size_t>(request.relative_path + request.relative_size
                                    - dot - 1))) return false;
    }
    return true;
}

inline Reason MapMatchError(policy_pattern_runtime::MatchResult result) {
    if (result == policy_pattern_runtime::MatchResult::kBudgetExceeded) {
        return Reason::kBudgetExceeded;
    }
    if (result == policy_pattern_runtime::MatchResult::kInvalidPathEncoding) {
        return Reason::kInvalidPathEncoding;
    }
    return Reason::kRuntimeUnavailable;
}

inline Result Route(const policy_v6_view::PolicyV6View& policy,
                    const Request& request,
                    const CapabilitySnapshot& capabilities,
                    policy_pattern_runtime::MatchScratch* scratch) {
    Result output;
    if (!policy.valid() || request.root == nullptr || request.root_size == 0
        || request.relative_path == nullptr || request.relative_size == 0
        || scratch == nullptr) {
        output.reason = Reason::kRuntimeUnavailable;
        return output;
    }
    int32_t winner_priority = 0;
    uint8_t winner_kind = 0xff;
    bool has_winner = false;
    bool matched_inactive = false;
    for (uint32_t local = 0; local < request.package.selector_count; ++local) {
        policy_v6_view::SelectorRef selector;
        policy_v6_view::StringRef selector_root;
        const uint32_t selector_id = request.package.first_selector + local;
        if (!policy.SelectorAt(selector_id, &selector)
            || !policy.StringAt(selector.root_id, &selector_root)) {
            output.reason = Reason::kRuntimeUnavailable;
            return output;
        }
        if (selector.object_type != 0 && selector.object_type != request.object_type) {
            continue;
        }
        bool selector_matches = false;
        uint16_t specificity = 0;
        if (selector.match_kind == 0) {
            selector_matches = LiteralSelectorMatches(selector_root, request);
            specificity = static_cast<uint16_t>(selector_root.size > UINT16_MAX
                ? UINT16_MAX : selector_root.size);
        } else {
            if (!SameBytes(selector_root, request.root, request.root_size)
                || !CandidateCacheMatches(policy, selector, request)) continue;
            ++output.matcher_invocations;
            const auto base = policy_pattern_runtime::MatchPattern(
                policy, selector.base_pattern_id, request.relative_path,
                request.relative_size, scratch);
            if (base != policy_pattern_runtime::MatchResult::kMatch) {
                if (base != policy_pattern_runtime::MatchResult::kNoMatch) {
                    output.reason = MapMatchError(base);
                    return output;
                }
                continue;
            }
            selector_matches = true;
            specificity = policy_pattern_runtime::PatternSpecificity(
                policy, selector.base_pattern_id);
            for (uint32_t i = 0; i < selector.except_count; ++i) {
                uint32_t except_pattern = 0;
                if (!policy.SelectorExceptPatternAt(selector, i, &except_pattern)) {
                    output.reason = Reason::kRuntimeUnavailable;
                    return output;
                }
                ++output.matcher_invocations;
                const auto excluded = policy_pattern_runtime::MatchPattern(
                    policy, except_pattern, request.relative_path,
                    request.relative_size, scratch);
                if (excluded == policy_pattern_runtime::MatchResult::kMatch) {
                    selector_matches = false;
                    break;
                }
                if (excluded != policy_pattern_runtime::MatchResult::kNoMatch) {
                    output.reason = MapMatchError(excluded);
                    return output;
                }
            }
        }
        if (!selector_matches) continue;
        for (uint32_t i = 0; i < selector.action_count; ++i) {
            policy_v6_view::ActionRef action;
            if (!policy.ActionAt(selector.first_action + i, &action)) {
                output.reason = Reason::kRuntimeUnavailable;
                return output;
            }
            if (action.domain != static_cast<uint8_t>(request.domain)
                || (action.kind != 0 && action.kind != 1)) continue;
            ActionRequirement requirement{
                request.domain,
                action.required_capabilities,
                action.required_operations | request.operation,
                true,
            };
            const ActionAdmission admission = AdmitAction(
                requirement, capabilities, request.package.plan_generation);
            if (!admission.active()) {
                matched_inactive = true;
                output.admission = admission;
                continue;
            }
            const bool better_precedence = has_winner
                && action.kind == 0 && winner_kind != 0;
            const bool same_precedence_better = has_winner
                && action.kind == winner_kind
                && (action.priority > winner_priority
                    || (action.priority == winner_priority
                        && (specificity > output.specificity
                            || (specificity == output.specificity
                                && action.rule_id < output.rule_id))));
            if (!has_winner || better_precedence || same_precedence_better) {
                has_winner = true;
                winner_priority = action.priority;
                winner_kind = action.kind;
                output.disposition = action.kind == 0
                    ? Disposition::kDeny : Disposition::kRedirect;
                output.reason = action.kind == 0 ? Reason::kDenied : Reason::kMatched;
                output.rule_id = action.rule_id;
                output.selector_id = selector_id;
                output.specificity = specificity;
                output.admission = admission;
                output.collision_mode = action.collision;
                output.reverse_mode = action.reverse;
                if (action.kind == 1 && !policy.StringAt(action.target_id, &output.target)) {
                    output = {};
                    output.reason = Reason::kRuntimeUnavailable;
                    return output;
                }
                if (action.kind == 0) break;
            }
        }
    }
    if (!has_winner && matched_inactive) {
        output.reason = output.admission.reason == ActionAdmissionReason::kOperationMissing
            ? Reason::kOperationMissing : Reason::kCapabilityMissing;
    }
    return output;
}

}  // namespace pathguard::policy_action_router
