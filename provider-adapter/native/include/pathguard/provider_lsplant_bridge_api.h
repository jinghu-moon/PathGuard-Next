#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATHGUARD_LSPLANT_BRIDGE_API_VERSION UINT16_C(1)
#define PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_CAPACITY UINT16_C(1024)
#define PATHGUARD_LSPLANT_MAPPING_FILE_PATH_CAPACITY UINT16_C(4096)

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

enum PathGuardLsplantMappingDispatchRole {
    PATHGUARD_LSPLANT_MAPPING_ROLE_FORWARD_DOCUMENT_PATH = 0,
    PATHGUARD_LSPLANT_MAPPING_ROLE_REVERSE_DOCUMENT_ID = 1,
    PATHGUARD_LSPLANT_MAPPING_ROLE_QUERY = 2,
    PATHGUARD_LSPLANT_MAPPING_ROLE_INSERT = 3,
    PATHGUARD_LSPLANT_MAPPING_ROLE_OPEN = 4,
    PATHGUARD_LSPLANT_MAPPING_ROLE_UPDATE = 5,
    PATHGUARD_LSPLANT_MAPPING_ROLE_DELETE = 6,
};

enum PathGuardLsplantMappingResultKind {
    PATHGUARD_LSPLANT_MAPPING_RESULT_FILE = 0,
    PATHGUARD_LSPLANT_MAPPING_RESULT_DOCUMENT_ID = 1,
    PATHGUARD_LSPLANT_MAPPING_RESULT_CURSOR = 2,
    PATHGUARD_LSPLANT_MAPPING_RESULT_URI = 3,
    PATHGUARD_LSPLANT_MAPPING_RESULT_PARCEL_FILE_DESCRIPTOR = 4,
    PATHGUARD_LSPLANT_MAPPING_RESULT_COUNT = 5,
    PATHGUARD_LSPLANT_MAPPING_RESULT_VOID = 6,
};

enum PathGuardLsplantMappingIdentifierKind {
    PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_NONE = 0,
    PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_CONTENT_URI = 1,
    PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_DOCUMENT_ID = 2,
    PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_FILE_PATH = 3,
};

enum PathGuardLsplantMappingBindingState {
    PATHGUARD_LSPLANT_MAPPING_BINDING_NONE = 0,
    PATHGUARD_LSPLANT_MAPPING_BINDING_SNAPSHOT = 1,
};

struct PathGuardLsplantMappingRequestV1 {
    uint16_t version;
    uint16_t size;
    uint8_t method_id;
    uint8_t role;
    uint8_t result_kind;
    uint8_t identifier_kind;
    uint64_t operations;
    uint16_t identifier_size;
    uint16_t file_path_size;
    uint8_t identifier[PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_CAPACITY];
    uint8_t file_path[PATHGUARD_LSPLANT_MAPPING_FILE_PATH_CAPACITY];
};

struct PathGuardLsplantMappingFactsV1 {
    uint16_t version;
    uint16_t size;
    uint8_t profile_matched;
    uint8_t runtime_available;
    uint8_t binding_state;
    uint8_t reserved;
    uint64_t profile_id;
    uint64_t supported_operations;
    uint64_t snapshot_generation;
    uint64_t binding_id;
    uint64_t reverse_record_id;
};

typedef int (*PathGuardLsplantMappingResolverV1)(
    const struct PathGuardLsplantMappingRequestV1* request,
    struct PathGuardLsplantMappingFactsV1* facts,
    void* user_data);

struct PathGuardLsplantMappingRuntimeV1 {
    uint16_t version;
    uint16_t size;
    PathGuardLsplantMappingResolverV1 resolver;
    void* user_data;
};

typedef int (*PathGuardLsplantConfigureMappingV1)(
    const struct PathGuardLsplantMappingRuntimeV1* config);

#ifdef __cplusplus
}
#endif
