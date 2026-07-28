#pragma once

#include <jni.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>

#include "pathguard/provider_redirect_lifecycle.hpp"
#include "zygisk.hpp"

namespace pathguard::provider_redirect {

struct Rule {
    int32_t caller_uid;
    uint32_t user_id;
    char visible_path[PATH_MAX];
    char backing_path[PATH_MAX];
};

InstallResult Install(zygisk::Api* api, JNIEnv* env, const Rule* rules,
                      uint32_t rule_count);

}  // namespace pathguard::provider_redirect
