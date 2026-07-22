#include "pathguard/provider_path_mapper.h"

#include <stdio.h>
#include <string.h>

namespace pathguard::provider_redirect {
namespace {

bool LogicalSuffix(const char* path, uint32_t user_id, const char** suffix,
                   char* root, size_t root_size) {
    char candidate[128]{};
    const char* formats[] = {
        "/storage/emulated/%u/",
        "/mnt/user/%u/emulated/%u/",
        "/data/media/%u/",
    };
    for (size_t index = 0; index < sizeof(formats) / sizeof(formats[0]); ++index) {
        const int written = index == 1
            ? snprintf(candidate, sizeof(candidate), formats[index], user_id, user_id)
            : snprintf(candidate, sizeof(candidate), formats[index], user_id);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(candidate)) continue;
        const size_t length = static_cast<size_t>(written);
        if (strncmp(path, candidate, length) != 0) continue;
        if (length + 1 > root_size) return false;
        memcpy(root, candidate, length + 1);
        *suffix = path + length;
        return true;
    }
    return false;
}

bool MatchesLogicalPath(const char* suffix, const char* visible, const char** tail) {
    const size_t length = strlen(visible);
    if (strncmp(suffix, visible, length) != 0) return false;
    if (suffix[length] != '\0' && suffix[length] != '/') return false;
    *tail = suffix + length;
    return true;
}

bool HasSafeTail(const char* tail) {
    const char* component = tail;
    while (*component != '\0') {
        while (*component == '/') ++component;
        if (*component == '\0') break;
        const char* end = strchr(component, '/');
        const size_t length = end == nullptr
            ? strlen(component) : static_cast<size_t>(end - component);
        if ((length == 1 && component[0] == '.')
            || (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        component = end == nullptr ? component + length : end + 1;
    }
    return true;
}

}  // namespace

bool RewriteAbsolutePath(const PathRule* rules, uint32_t rule_count,
                         int32_t caller_uid, const char* path,
                         char* output, size_t capacity) {
    if (rules == nullptr || output == nullptr || capacity == 0
        || (caller_uid < 10000 && caller_uid != -1)
        || path == nullptr || path[0] != '/') return false;
    const PathRule* selected = nullptr;
    const char* selected_tail = nullptr;
    char selected_root[128]{};
    size_t selected_length = 0;
    for (uint32_t index = 0; index < rule_count; ++index) {
        const PathRule& rule = rules[index];
        if (rule.caller_uid != -1 && rule.caller_uid != caller_uid) continue;
        const char* suffix = nullptr;
        char root[128]{};
        if (!LogicalSuffix(path, rule.user_id, &suffix, root, sizeof(root))) continue;
        const char* tail = nullptr;
        if (!MatchesLogicalPath(suffix, rule.visible_path, &tail)) continue;
        if (!HasSafeTail(tail)) return false;
        const size_t visible_length = strlen(rule.visible_path);
        if (selected == nullptr || visible_length > selected_length) {
            selected = &rule;
            selected_tail = tail;
            selected_length = visible_length;
            strcpy(selected_root, root);
        }
    }
    if (selected == nullptr) return false;
    const int written = snprintf(output, capacity, "%s%s%s",
                                 selected_root, selected->backing_path, selected_tail);
    return written > 0 && static_cast<size_t>(written) < capacity
        && strcmp(path, output) != 0;
}

bool RestoreAbsolutePath(const PathRule* rules, uint32_t rule_count,
                         int32_t caller_uid, const char* path,
                         char* output, size_t capacity) {
    if (rules == nullptr || output == nullptr || capacity == 0
        || (caller_uid < 10000 && caller_uid != -1)
        || path == nullptr || path[0] != '/') return false;
    const PathRule* selected = nullptr;
    const char* selected_tail = nullptr;
    char selected_root[128]{};
    size_t selected_length = 0;
    for (uint32_t index = 0; index < rule_count; ++index) {
        const PathRule& rule = rules[index];
        if (rule.caller_uid != -1 && rule.caller_uid != caller_uid) continue;
        const char* suffix = nullptr;
        char root[128]{};
        if (!LogicalSuffix(path, rule.user_id, &suffix, root, sizeof(root))) continue;
        const char* tail = nullptr;
        if (!MatchesLogicalPath(suffix, rule.backing_path, &tail)) continue;
        if (!HasSafeTail(tail)) return false;
        const size_t backing_length = strlen(rule.backing_path);
        if (selected == nullptr || backing_length > selected_length) {
            selected = &rule;
            selected_tail = tail;
            selected_length = backing_length;
            strcpy(selected_root, root);
        }
    }
    if (selected == nullptr) return false;
    const int written = snprintf(output, capacity, "%s%s%s",
                                 selected_root, selected->visible_path, selected_tail);
    return written > 0 && static_cast<size_t>(written) < capacity
        && strcmp(path, output) != 0;
}

}  // namespace pathguard::provider_redirect
