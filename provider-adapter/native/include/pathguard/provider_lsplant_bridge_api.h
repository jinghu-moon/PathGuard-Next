#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATHGUARD_LSPLANT_BRIDGE_API_VERSION UINT16_C(1)

enum PathGuardLsplantProviderKind {
    PATHGUARD_LSPLANT_PROVIDER_UNKNOWN = 0,
    PATHGUARD_LSPLANT_PROVIDER_DOCUMENTS = 1,
    PATHGUARD_LSPLANT_PROVIDER_MEDIA = 2,
};

struct PathGuardLsplantBridgeResultV1 {
    uint16_t version;
    uint16_t size;
    uint8_t provider_kind;
    uint8_t library_loaded;
    uint8_t lsplant_initialized;
    uint8_t hooker_dex_loaded;
    uint64_t resolved_methods;
    uint64_t installed_hooks;
    uint64_t backup_methods;
    uint64_t self_tested_hooks;
    int32_t bridge_errno;
};

typedef int (*PathGuardLsplantInitializeV1)(
    void* jni_env, struct PathGuardLsplantBridgeResultV1* result);

typedef int (*PathGuardLsplantInstallPassthroughV1)(
    void* jni_env, uint8_t provider_kind,
    const uint8_t* hooker_dex, size_t hooker_dex_size,
    struct PathGuardLsplantBridgeResultV1* result);

typedef int (*PathGuardLsplantWaitPassthroughV1)(
    uint32_t timeout_ms, struct PathGuardLsplantBridgeResultV1* result);

#ifdef __cplusplus
}
#endif
