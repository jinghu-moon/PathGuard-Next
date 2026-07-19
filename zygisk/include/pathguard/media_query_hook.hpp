#pragma once

#include <cstddef>

#include <jni.h>

namespace zygisk {
struct Api;
}

namespace pathguard::media_query {

bool Install(zygisk::Api* api, JNIEnv* env, const char* const* deny_paths,
             std::size_t deny_path_count, int uid);

}  // namespace pathguard::media_query
