#pragma once

#include <jni.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>

#include "pathguard/action_admission.h"
#include "pathguard/provider_redirect_lifecycle.hpp"
#include "pathguard/storage_path_adapter.h"
#include "zygisk.hpp"

namespace pathguard::provider_redirect {

enum class IdentityMode : uint8_t {
    kProcessUid,
    kBinderCallerUid,
};

struct PolicyConfig {
    const uint8_t* policy_data = nullptr;
    size_t policy_size = 0;
    const storage_path_adapter::PolicyScope* scopes = nullptr;
    uint32_t scope_count = 0;
    AdmissionDomain domain = AdmissionDomain::kAppPath;
    IdentityMode identity_mode = IdentityMode::kProcessUid;
};

InstallResult Install(zygisk::Api* api, JNIEnv* env,
                      const PolicyConfig& config);

}  // namespace pathguard::provider_redirect
