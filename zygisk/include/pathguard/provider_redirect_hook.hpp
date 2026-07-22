#pragma once

#include <jni.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>

#include "zygisk.hpp"

namespace pathguard::provider_redirect {

enum class CallerMode : uint8_t {
    kBinderUid = 0,
    kSystemMedia = 1,
};

struct Rule {
    int32_t caller_uid;
    uint32_t user_id;
    char visible_path[PATH_MAX];
    char backing_path[PATH_MAX];
};

bool Install(zygisk::Api* api, JNIEnv* env, const Rule* rules,
             uint32_t rule_count, CallerMode caller_mode);

}  // namespace pathguard::provider_redirect
