#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pathguard/mount_backend.h"
#include "pathguard/policy_v6_view.h"

namespace pathguard {

inline constexpr uint32_t kMaxLiteralMountPlanActions = 64;
inline constexpr uint32_t kMaxLiteralMountPlanPathBytes = 64 * 1024;

enum class LiteralMountAction : uint8_t { kDeny, kRedirect };

struct LiteralMountPlanEntry {
    LiteralMountAction action = LiteralMountAction::kDeny;
    uint32_t visible_path = 0;
    uint32_t target_path = 0;
};

struct LiteralMountPlan {
    uint64_t content_generation = 0;
    uint64_t plan_generation = 0;
    uint32_t policy_flags = 0;
    uint32_t count = 0;
    uint32_t path_bytes = 1;
    MountActionMask required_actions = 0;
    LiteralMountPlanEntry entries[kMaxLiteralMountPlanActions]{};
    char paths[kMaxLiteralMountPlanPathBytes]{};

    const char* Path(uint32_t offset) const {
        return offset < path_bytes ? paths + offset : nullptr;
    }
};

inline bool IsSafeRelativePolicyPath(const policy_v6_view::StringRef& path) {
    if (path.empty() || path.data[0] == '/') return false;
    uint32_t component_start = 0;
    for (uint32_t i = 0; i <= path.size; ++i) {
        if (i != path.size && path.data[i] != '/') continue;
        const uint32_t length = i - component_start;
        if (length == 0
            || (length == 1 && path.data[component_start] == '.')
            || (length == 2 && path.data[component_start] == '.'
                && path.data[component_start + 1] == '.')) return false;
        component_start = i + 1;
    }
    return true;
}

inline bool StoreLiteralMountPath(LiteralMountPlan* plan,
                                  const policy_v6_view::StringRef& path,
                                  uint32_t* offset) {
    if (plan == nullptr || offset == nullptr
        || path.size >= kMaxLiteralMountPlanPathBytes - plan->path_bytes) {
        return false;
    }
    *offset = plan->path_bytes;
    if (path.size != 0) memcpy(plan->paths + plan->path_bytes, path.data, path.size);
    plan->path_bytes += path.size;
    plan->paths[plan->path_bytes++] = '\0';
    return true;
}

inline bool BuildLiteralMountPlan(
        const policy_v6_view::PolicyV6View& policy,
        const policy_v6_view::PackageRef& package,
        LiteralMountPlan* output) {
    if (!policy.valid() || output == nullptr) return false;
    *output = {};
    output->path_bytes = 1;
    output->content_generation = policy.content_generation();
    output->plan_generation = package.plan_generation;
    output->policy_flags = policy.flags();
    for (uint32_t i = 0; i < package.action_count; ++i) {
        policy_v6_view::ActionRef action;
        if (!policy.ActionAt(package.first_action + i, &action)) return false;
        if (action.domain != 0) continue;
        if (output->count >= kMaxLiteralMountPlanActions
            || action.selector_id < package.first_selector
            || action.selector_id >= package.first_selector + package.selector_count) {
            return false;
        }
        policy_v6_view::SelectorRef selector;
        policy_v6_view::StringRef visible;
        if (!policy.SelectorAt(action.selector_id, &selector)
            || selector.match_kind != 0
            || !policy.StringAt(selector.root_id, &visible)
            || !IsSafeRelativePolicyPath(visible)) return false;
        policy_v6_view::StringRef target;
        const bool deny = action.kind == 0;
        if (!deny && (action.kind != 1
            || !policy.StringAt(action.target_id, &target)
            || !IsSafeRelativePolicyPath(target))) return false;
        LiteralMountPlanEntry& entry = output->entries[output->count++];
        entry.action = deny ? LiteralMountAction::kDeny
                            : LiteralMountAction::kRedirect;
        if (!StoreLiteralMountPath(output, visible, &entry.visible_path)
            || !StoreLiteralMountPath(output, target, &entry.target_path)) return false;
        output->required_actions |= deny ? kMountActionDenyAnchor
                                        : kMountActionRedirect;
    }
    return output->count != 0;
}

}  // namespace pathguard
