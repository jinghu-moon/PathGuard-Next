#pragma once

#include <stdint.h>

#include "pathguard/capabilities.h"

namespace pathguard {

struct ProviderCompositeProbe {
    bool caller_uid = false;
    bool path_io = false;
    bool query = false;
    bool insert = false;
    bool open_fd = false;
    bool rename_delete = false;
    bool reverse_mapping = false;
    OperationMask path_operations = 0;
};

struct ProviderCompositeObservation {
    CapabilityBits capabilities = 0;
    OperationMask operations = 0;
    bool active = false;
};

constexpr ProviderCompositeObservation ObserveProviderComposite(
        const ProviderCompositeProbe& probe) noexcept {
    ProviderCompositeObservation output;
    if (probe.caller_uid) output.capabilities |= kCapabilityProviderCallerUid;
    output.operations = probe.path_operations & kProviderCompositeOperationsV1;
    if (probe.query) output.operations |= kOperationProviderQuery;
    if (probe.insert) output.operations |= kOperationProviderInsert;
    if (probe.open_fd) output.operations |= kOperationOpenRead | kOperationOpenWrite;
    if (probe.rename_delete) output.operations |= kOperationRename
        | kOperationUnlink | kOperationRmdir;
    if (probe.reverse_mapping) output.operations |= kOperationReverseMapping
        | kOperationMediaScan;
    const bool mapping = probe.path_io && probe.query && probe.insert
        && probe.open_fd && probe.rename_delete && probe.reverse_mapping;
    if (mapping) output.capabilities |= kCapabilityProviderQueryInsertMapping;
    output.active = (output.capabilities
        & (kCapabilityProviderCallerUid | kCapabilityProviderQueryInsertMapping))
        == (kCapabilityProviderCallerUid | kCapabilityProviderQueryInsertMapping)
        && (output.operations & kProviderCompositeOperationsV1)
        == kProviderCompositeOperationsV1;
    return output;
}

struct ProviderRouteContext {
    int32_t caller_uid = -1;
    uint32_t user_id = 0;
    uint32_t package_index = 0;
    uint64_t plan_generation = 0;
    uint64_t rule_id = 0;
    uint64_t transaction_high = 0;
    uint64_t transaction_low = 0;

    bool valid() const noexcept {
        return caller_uid >= 10000 && rule_id != 0 && plan_generation != 0;
    }
};

}  // namespace pathguard
