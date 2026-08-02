#pragma once

#include <stdint.h>

namespace pathguard {

struct ProviderJavaBridgeStatusV1 {
    uint16_t version = 1;
    uint8_t kind = 0;
    uint8_t reserved = 0;
    uint64_t deployment_profile_id = 0;
    uint64_t provider_profile_id = 0;
    bool build_matched = false;
    bool library_loaded = false;
    bool lsplant_initialized = false;
    bool hooker_dex_loaded = false;
    uint64_t resolved_methods = 0;
    uint64_t installed_hooks = 0;
    uint64_t backup_methods = 0;
    uint64_t self_tested_hooks = 0;
    int32_t bridge_errno = 0;
};

}  // namespace pathguard
