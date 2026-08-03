#pragma once

#include <jni.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>

#include "pathguard/action_admission.h"
#include "pathguard/provider_redirect_lifecycle.hpp"
#include "pathguard/provenance_protocol.h"
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

// Available after Provider Binder identity hooks are installed. Unknown or
// Provider-process identity is returned as -1 so callers remain fail-open.
int32_t CurrentCallingUid() noexcept;

// Keeps provenance coordination aligned with companion/snapshot availability.
// Forward path routing remains fail-open while this gate is disabled.
void SetProvenanceTransactionsAvailable(bool available) noexcept;

bool BuildProvenanceRecordForRoute(
    const storage_path_adapter::RewriteResult& rewrite,
    int32_t caller_uid, const char* logical_path, const char* target_path,
    provenance_protocol::Record* output) noexcept;

bool CaptureProvenanceIdentity(
    int fd, provenance_protocol::Identity* output) noexcept;

}  // namespace pathguard::provider_redirect
