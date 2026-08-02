#pragma once

#include <cstdint>

#include "pathguard/provider_adapter_profile.h"

namespace pathguard {

inline constexpr std::uint16_t kProviderMappingContractVersion = 1;

enum class ProviderMappingOperation : std::uint8_t {
    kUnknown,
    kQuery,
    kCreate,
    kOpenRead,
    kOpenWrite,
    kRename,
    kDelete,
    kDirectoryQuery,
    kMediaScan,
    kReverseLookup,
};

enum class ProviderMappingDisposition : std::uint8_t {
    kPass,
    kRewrite,
    kUnsupported,
    kAmbiguousReverse,
    kFailOpen,
};

enum class ProviderMappingReason : std::uint8_t {
    kReady,
    kNoRoute,
    kInvalidOperation,
    kProfileMismatch,
    kOperationUnsupported,
    kRuntimeUnavailable,
    kBindingInvalid,
    kReverseMissing,
    kReverseAmbiguous,
    kReverseError,
    kReverseMismatch,
};

struct ProviderMappingRequestV1 {
    std::uint16_t version = kProviderMappingContractVersion;
    ProviderMappingOperation operation = ProviderMappingOperation::kUnknown;
    ProviderAdapterProfileMatchV1 profile_match;
    OperationMask supported_operations = 0;
    const ProviderRouteBindingV1* binding = nullptr;
    provenance::ResolveResult reverse;
    bool runtime_available = false;
};

struct ProviderMappingDecisionV1 {
    ProviderMappingDisposition disposition = ProviderMappingDisposition::kPass;
    ProviderMappingReason reason = ProviderMappingReason::kNoRoute;
    OperationMask required_operation = 0;
    ProviderRouteBindingReason binding_reason =
        ProviderRouteBindingReason::kInvalidContext;

    bool rewrite() const noexcept {
        return disposition == ProviderMappingDisposition::kRewrite;
    }
};

OperationMask ProviderMappingOperationMask(
    ProviderMappingOperation operation) noexcept;

ProviderMappingDecisionV1 EvaluateProviderMapping(
    const ProviderMappingRequestV1& request) noexcept;

}  // namespace pathguard
