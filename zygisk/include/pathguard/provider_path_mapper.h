#pragma once

#include <stddef.h>
#include <stdint.h>

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace pathguard::provider_redirect {

struct PathRule {
    int32_t caller_uid = -1;
    uint32_t user_id = 0;
    char visible_path[PATH_MAX]{};
    char backing_path[PATH_MAX]{};
};

bool RewriteAbsolutePath(const PathRule* rules, uint32_t rule_count,
                         int32_t caller_uid, const char* path,
                         char* output, size_t capacity);

bool RestoreAbsolutePath(const PathRule* rules, uint32_t rule_count,
                         int32_t caller_uid, const char* path,
                         char* output, size_t capacity);

}  // namespace pathguard::provider_redirect
