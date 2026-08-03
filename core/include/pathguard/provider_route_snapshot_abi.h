#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_VERSION UINT16_C(2)
#define PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_MAX_BINDINGS UINT32_C(4096)
#define PATHGUARD_PROVIDER_ROUTE_PATH_MAX UINT32_C(4095)
#define PATHGUARD_PROVIDER_ROUTE_IDENTIFIER_MAX UINT32_C(1023)
#define PATHGUARD_PROVIDER_ROUTE_VOLUME_MAX UINT32_C(63)
#define PATHGUARD_PROVIDER_ROUTE_IDENTITY_HANDLE_MAX UINT32_C(128)

struct PathGuardProviderRouteBytesV1 {
    const uint8_t* data;
    uint32_t size;
};

enum PathGuardProviderRouteIdentityKindV1 {
    PATHGUARD_PROVIDER_ROUTE_IDENTITY_NONE = 0,
    PATHGUARD_PROVIDER_ROUTE_IDENTITY_FILE_HANDLE = 1,
    PATHGUARD_PROVIDER_ROUTE_IDENTITY_STATX_BIRTH_TIME = 2,
};

enum PathGuardProviderRouteReverseModeV1 {
    PATHGUARD_PROVIDER_ROUTE_REVERSE_STATIC_UNIQUE = 1,
    PATHGUARD_PROVIDER_ROUTE_REVERSE_PROVENANCE = 2,
};

struct PathGuardProviderRouteBindingV1 {
    uint16_t version;
    uint16_t size;
    uint8_t reverse_mode;
    uint8_t identity_kind;
    uint8_t object_type;
    uint8_t reserved;
    int32_t identity_handle_type;
    uint64_t binding_id;
    uint64_t reverse_record_id;
    int32_t caller_uid;
    uint32_t user_id;
    uint32_t package_index;
    uint32_t birth_nanoseconds;
    uint64_t plan_generation;
    uint64_t rule_id;
    uint64_t identity_epoch;
    uint64_t content_generation;
    uint64_t created_plan_generation;
    uint64_t bound_plan_generation;
    uint64_t commit_sequence;
    uint64_t inode;
    int64_t birth_seconds;
    struct PathGuardProviderRouteBytesV1 visible_source_path;
    struct PathGuardProviderRouteBytesV1 backing_target_path;
    struct PathGuardProviderRouteBytesV1 provider_uri;
    struct PathGuardProviderRouteBytesV1 stable_document_id;
    struct PathGuardProviderRouteBytesV1 identity_volume;
    struct PathGuardProviderRouteBytesV1 identity_handle;
    struct PathGuardProviderRouteBytesV1 storage_root_id;
    struct PathGuardProviderRouteBytesV1 target_relative_path;
    struct PathGuardProviderRouteBytesV1 namespace_id;
    struct PathGuardProviderRouteBytesV1 visible_source_root;
    struct PathGuardProviderRouteBytesV1 backing_target_root;
};

struct PathGuardProviderRouteSnapshotV1 {
    uint16_t version;
    uint16_t size;
    uint32_t binding_count;
    uint64_t generation;
    const struct PathGuardProviderRouteBindingV1* bindings;
};

#ifdef __cplusplus
}
#endif
