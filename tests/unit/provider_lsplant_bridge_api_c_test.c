#include <stddef.h>
#include <stdint.h>

#include "pathguard/provider_lsplant_bridge_api.h"

_Static_assert(PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_CAPACITY == 1024);
_Static_assert(PATHGUARD_LSPLANT_MAPPING_FILE_PATH_CAPACITY == 4096);
_Static_assert(offsetof(struct PathGuardLsplantMappingRequestV1, operations) == 8);
_Static_assert(sizeof(struct PathGuardLsplantMappingRequestV1) == 5144);
_Static_assert(sizeof(struct PathGuardLsplantMappingFactsV1) == 48);
_Static_assert(sizeof(((struct PathGuardLsplantMappingRequestV1*)0)->operations)
               == sizeof(uint64_t));
_Static_assert(sizeof(((struct PathGuardLsplantMappingFactsV1*)0)->binding_id)
               == sizeof(uint64_t));
_Static_assert(PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_VERSION == 2);
_Static_assert(sizeof(((struct PathGuardProviderRouteBindingV1*)0)->binding_id)
               == sizeof(uint64_t));
_Static_assert(sizeof(((struct PathGuardProviderRouteBindingV1*)0)
                      ->identity_handle_type) == sizeof(int32_t));

static int resolve_for_c_parser(
        const struct PathGuardLsplantMappingRequestV1* request,
        struct PathGuardLsplantMappingFactsV1* facts,
        void* user_data) {
    (void)user_data;
    if (request == NULL || facts == NULL) return 0;
    facts->profile_matched = 0;
    facts->runtime_available = 0;
    facts->binding_state = PATHGUARD_LSPLANT_MAPPING_BINDING_NONE;
    return 1;
}

int pathguard_provider_lsplant_bridge_api_c_test(void) {
    struct PathGuardLsplantMappingRuntimeV1 runtime = {
        PATHGUARD_LSPLANT_BRIDGE_API_VERSION,
        (uint16_t)sizeof(struct PathGuardLsplantMappingRuntimeV1),
        resolve_for_c_parser,
        NULL,
        NULL,
        NULL,
        NULL,
    };
    PathGuardLsplantPublishMappingV1 publish = NULL;
    return runtime.resolver == NULL
        || runtime.version != PATHGUARD_LSPLANT_BRIDGE_API_VERSION
        || publish != NULL;
}
