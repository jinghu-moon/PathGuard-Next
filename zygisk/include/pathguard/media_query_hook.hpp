#pragma once

#include <stddef.h>

#include <jni.h>

namespace zygisk {
struct Api;
}

namespace pathguard::media_query {

bool Install(zygisk::Api* api, JNIEnv* env, const char* const* deny_paths,
             size_t deny_path_count, int uid);

}  // namespace pathguard::media_query
