#pragma once

#include <stdint.h>

#include "pathguard/capabilities.h"

namespace pathguard {

inline constexpr uint16_t kProviderContractProbeVersion = 1;

enum class ProviderContractKind : uint8_t {
    kUnknown = 0,
    kDocuments = 1,
    kMediaStore = 2,
};

using ProviderContractChecks = uint32_t;

inline constexpr ProviderContractChecks kProviderCheckAbiProfile = UINT32_C(1) << 0;
inline constexpr ProviderContractChecks kProviderCheckCallerUid = UINT32_C(1) << 1;
inline constexpr ProviderContractChecks kProviderCheckQueryMapping = UINT32_C(1) << 2;
inline constexpr ProviderContractChecks kProviderCheckCreateInsert = UINT32_C(1) << 3;
inline constexpr ProviderContractChecks kProviderCheckStableDocumentId = UINT32_C(1) << 4;
inline constexpr ProviderContractChecks kProviderCheckOpenFdIdentity = UINT32_C(1) << 5;
inline constexpr ProviderContractChecks kProviderCheckRenameDelete = UINT32_C(1) << 6;
inline constexpr ProviderContractChecks kProviderCheckReverseMapping = UINT32_C(1) << 7;
inline constexpr ProviderContractChecks kProviderCheckRestartRecovery = UINT32_C(1) << 8;

inline constexpr ProviderContractChecks kProviderContractRequiredChecksV1 =
    kProviderCheckAbiProfile
    | kProviderCheckCallerUid
    | kProviderCheckQueryMapping
    | kProviderCheckCreateInsert
    | kProviderCheckStableDocumentId
    | kProviderCheckOpenFdIdentity
    | kProviderCheckRenameDelete
    | kProviderCheckReverseMapping
    | kProviderCheckRestartRecovery;

enum class ProviderContractReason : uint8_t {
    kActive,
    kInvalidProbe,
    kAbiProfileMissing,
    kCheckFailed,
    kCheckMissing,
    kOperationMissing,
};

struct ProviderContractProbeV1 {
    uint16_t version = kProviderContractProbeVersion;
    ProviderContractKind kind = ProviderContractKind::kUnknown;
    uint64_t provider_build_id = 0;
    uint64_t adapter_profile_id = 0;
    ProviderContractChecks passed_checks = 0;
    ProviderContractChecks failed_checks = 0;
    OperationMask observed_operations = 0;
    int32_t probe_errno = 0;
};

struct ProviderContractObservationV1 {
    CapabilityBits capabilities = 0;
    OperationMask operations = 0;
    ProviderContractChecks missing_checks = 0;
    ProviderContractChecks failed_checks = 0;
    OperationMask missing_operations = 0;
    int32_t probe_errno = 0;
    ProviderContractReason reason = ProviderContractReason::kInvalidProbe;

    constexpr bool active() const noexcept {
        return reason == ProviderContractReason::kActive;
    }
};

constexpr ProviderContractObservationV1 ObserveProviderContract(
        const ProviderContractProbeV1& probe) noexcept {
    ProviderContractObservationV1 output;
    output.probe_errno = probe.probe_errno;
    output.operations = probe.observed_operations & kProviderCompositeOperationsV1;
    output.missing_operations = kProviderCompositeOperationsV1
        & ~output.operations;
    output.failed_checks = probe.failed_checks & kProviderContractRequiredChecksV1;
    output.missing_checks = kProviderContractRequiredChecksV1
        & ~(probe.passed_checks | probe.failed_checks);

    if (probe.version != kProviderContractProbeVersion
        || (probe.kind != ProviderContractKind::kDocuments
            && probe.kind != ProviderContractKind::kMediaStore)
        || (probe.passed_checks & probe.failed_checks) != 0) {
        output.reason = ProviderContractReason::kInvalidProbe;
        return output;
    }
    if ((probe.passed_checks & kProviderCheckCallerUid) != 0) {
        output.capabilities |= kCapabilityProviderCallerUid;
    }
    if (probe.provider_build_id == 0 || probe.adapter_profile_id == 0
        || (probe.passed_checks & kProviderCheckAbiProfile) == 0) {
        output.reason = ProviderContractReason::kAbiProfileMissing;
        return output;
    }
    if (output.failed_checks != 0 || probe.probe_errno != 0) {
        output.reason = ProviderContractReason::kCheckFailed;
        return output;
    }
    if (output.missing_checks != 0) {
        output.reason = ProviderContractReason::kCheckMissing;
        return output;
    }
    if (output.missing_operations != 0) {
        output.reason = ProviderContractReason::kOperationMissing;
        return output;
    }
    output.capabilities |= kCapabilityProviderQueryInsertMapping;
    output.reason = ProviderContractReason::kActive;
    return output;
}

struct ProviderContractPairObservationV1 {
    ProviderContractObservationV1 documents;
    ProviderContractObservationV1 media;
    CapabilityBits capabilities = 0;
    OperationMask operations = 0;

    constexpr bool active() const noexcept {
        return documents.active() && media.active()
            && capabilities
                == (kCapabilityProviderCallerUid
                    | kCapabilityProviderQueryInsertMapping);
    }
};

constexpr ProviderContractPairObservationV1 ObserveProviderContractPair(
        const ProviderContractProbeV1& documents,
        const ProviderContractProbeV1& media) noexcept {
    ProviderContractPairObservationV1 output;
    output.documents = ObserveProviderContract(documents);
    output.media = ObserveProviderContract(media);
    if (documents.kind != ProviderContractKind::kDocuments) {
        output.documents.reason = ProviderContractReason::kInvalidProbe;
    }
    if (media.kind != ProviderContractKind::kMediaStore) {
        output.media.reason = ProviderContractReason::kInvalidProbe;
    }
    output.capabilities = output.documents.capabilities
        & output.media.capabilities;
    output.operations = output.documents.operations & output.media.operations;
    return output;
}

}  // namespace pathguard
