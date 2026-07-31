#pragma once

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pathguard/policy_action_router.h"

namespace pathguard::storage_path_adapter {

struct PolicyScope {
    int32_t caller_uid = -1;
    uint32_t user_id = 0;
    uint32_t package_index = 0;
};

struct LogicalPath {
    const char* prefix = nullptr;
    size_t prefix_size = 0;
    const char* root = nullptr;
    size_t root_size = 0;
    const char* relative = nullptr;
    size_t relative_size = 0;
    uint32_t user_id = 0;
};

enum class RewriteDisposition : uint8_t { kPass, kDeny, kRedirect };

struct RewriteResult {
    RewriteDisposition disposition = RewriteDisposition::kPass;
    policy_action_router::Reason reason = policy_action_router::Reason::kNoMatch;
    int error_number = 0;
    uint64_t rule_id = 0;
    uint32_t matcher_invocations = 0;
    uint8_t collision_mode = 0;
    uint8_t reverse_mode = 0;
    uint32_t user_id = 0;
    uint64_t content_generation = 0;
    uint64_t plan_generation = 0;
};

inline bool SafePathComponents(const char* path, size_t size) {
    if (path == nullptr || size == 0 || size > 4095) return false;
    size_t component = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i != size && path[i] != '/') continue;
        const size_t length = i - component;
        if (length == 0 || length > 255
            || (length == 1 && path[component] == '.')
            || (length == 2 && path[component] == '.'
                && path[component + 1] == '.')) return false;
        component = i + 1;
    }
    return true;
}

inline bool ParseUnsigned(const char* begin, const char* end, uint32_t* value) {
    if (begin == nullptr || end == nullptr || value == nullptr || begin == end) return false;
    uint64_t output = 0;
    for (const char* current = begin; current < end; ++current) {
        if (*current < '0' || *current > '9') return false;
        output = output * 10 + static_cast<unsigned>(*current - '0');
        if (output > UINT32_MAX) return false;
    }
    *value = static_cast<uint32_t>(output);
    return true;
}

inline bool ParseLogicalPath(const char* path, LogicalPath* output) {
    if (path == nullptr || output == nullptr || path[0] != '/') return false;
    const char* user_begin = nullptr;
    const char* after_user = nullptr;
    if (strncmp(path, "/storage/emulated/", 18) == 0) {
        user_begin = path + 18;
        after_user = strchr(user_begin, '/');
    } else if (strncmp(path, "/data/media/", 12) == 0) {
        user_begin = path + 12;
        after_user = strchr(user_begin, '/');
    } else if (strncmp(path, "/mnt/user/", 10) == 0) {
        user_begin = path + 10;
        const char* first_end = strchr(user_begin, '/');
        if (first_end == nullptr || strncmp(first_end, "/emulated/", 10) != 0) return false;
        uint32_t mount_user = 0;
        if (!ParseUnsigned(user_begin, first_end, &mount_user)) return false;
        user_begin = first_end + 10;
        after_user = strchr(user_begin, '/');
        uint32_t storage_user = 0;
        if (after_user == nullptr
            || !ParseUnsigned(user_begin, after_user, &storage_user)
            || storage_user != mount_user) return false;
    } else {
        return false;
    }
    uint32_t user_id = 0;
    if (after_user == nullptr || !ParseUnsigned(user_begin, after_user, &user_id)) {
        return false;
    }
    const char* root = after_user + 1;
    const char* root_end = strchr(root, '/');
    if (root[0] == '\0' || root_end == nullptr || root_end == root
        || root_end[1] == '\0') return false;
    output->prefix = path;
    output->prefix_size = static_cast<size_t>(root - path);
    output->root = root;
    output->root_size = static_cast<size_t>(root_end - root);
    output->relative = root_end + 1;
    output->relative_size = strlen(output->relative);
    output->user_id = user_id;
    return output->relative_size != 0
        && SafePathComponents(output->root, output->root_size)
        && SafePathComponents(output->relative, output->relative_size);
}

inline bool AppendTarget(const LogicalPath& logical,
                         const policy_v6_view::StringRef& target,
                         size_t relative_offset,
                         char* output, size_t capacity) {
    if (output == nullptr || capacity == 0 || target.empty()
        || relative_offset > logical.relative_size) return false;
    size_t used = 0;
    auto append = [&](const char* data, size_t size) {
        if (size >= capacity - used) return false;
        memcpy(output + used, data, size);
        used += size;
        output[used] = '\0';
        return true;
    };
    if (!append(logical.prefix, logical.prefix_size)) return false;
    for (uint32_t i = 0; i < target.size;) {
        if (i + 6 <= target.size && memcmp(target.data + i, "{user}", 6) == 0) {
            char user[16]{};
            const int written = snprintf(user, sizeof(user), "%u", logical.user_id);
            if (written <= 0 || !append(user, static_cast<size_t>(written))) return false;
            i += 6;
        } else {
            if (!append(target.data + i, 1)) return false;
            ++i;
        }
    }
    const size_t relative_size = logical.relative_size - relative_offset;
    return relative_size == 0
        || (append("/", 1)
            && append(logical.relative + relative_offset, relative_size));
}

inline RewriteResult Rewrite(
        const policy_v6_view::PolicyV6View& policy,
        const PolicyScope* scopes, uint32_t scope_count, int32_t caller_uid,
        const char* absolute_path, AdmissionDomain domain,
        OperationMask operation, uint8_t object_type,
        const CapabilitySnapshot& capabilities,
        policy_pattern_runtime::MatchScratch* scratch,
        char* output, size_t capacity) {
    RewriteResult result;
    LogicalPath logical;
    if (!ParseLogicalPath(absolute_path, &logical)) return result;
    const PolicyScope* selected = nullptr;
    for (uint32_t i = 0; i < scope_count; ++i) {
        if (scopes[i].caller_uid == caller_uid
            && scopes[i].user_id == logical.user_id) {
            selected = &scopes[i];
            break;
        }
    }
    if (selected == nullptr) return result;
    policy_v6_view::PackageRef package;
    if (!policy.PackageAt(selected->package_index, &package)) {
        result.reason = policy_action_router::Reason::kRuntimeUnavailable;
        return result;
    }
    const policy_action_router::Request request{
        package, logical.root, logical.root_size,
        logical.relative, logical.relative_size, object_type, domain, operation,
    };
    CapabilitySnapshot scoped_capabilities = capabilities;
    if (scoped_capabilities.plan_generation == 0) {
        scoped_capabilities.plan_generation = package.plan_generation;
    }
    const policy_action_router::Result routed = policy_action_router::Route(
        policy, request, scoped_capabilities, scratch);
    result.reason = routed.reason;
    result.rule_id = routed.rule_id;
    result.matcher_invocations = routed.matcher_invocations;
    result.collision_mode = routed.collision_mode;
    result.reverse_mode = routed.reverse_mode;
    result.user_id = logical.user_id;
    result.content_generation = policy.content_generation();
    result.plan_generation = package.plan_generation;
    if (routed.disposition == policy_action_router::Disposition::kDeny) {
        result.disposition = RewriteDisposition::kDeny;
        result.error_number = EACCES;
    } else if (routed.disposition == policy_action_router::Disposition::kRedirect) {
        if (!AppendTarget(logical, routed.target, routed.relative_offset,
                          output, capacity)) {
            result.reason = policy_action_router::Reason::kRuntimeUnavailable;
            return result;
        }
        result.disposition = RewriteDisposition::kRedirect;
    }
    return result;
}

}  // namespace pathguard::storage_path_adapter
