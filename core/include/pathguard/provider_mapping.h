#pragma once

#include <cstdint>

#include "pathguard/provider_adapter_profile.h"
#include "pathguard/provider_lsplant_bridge.h"

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
    kOpenReadWrite,
    kMetadataMutation,
    kMetadataRename,
    kDeleteFile,
    kDeleteDirectory,
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

struct ProviderMappingRuntimeFactsV1 {
    ProviderAdapterProfileMatchV1 profile_match;
    OperationMask supported_operations = 0;
    const ProviderRouteBindingV1* binding = nullptr;
    provenance::ResolveResult reverse;
    bool runtime_available = false;
};

#ifndef PATHGUARD_PROVIDER_MAPPING_RUNTIME_RESOLVER_V1_DEFINED
#define PATHGUARD_PROVIDER_MAPPING_RUNTIME_RESOLVER_V1_DEFINED 1
using ProviderMappingRuntimeResolverV1 = bool (*) (
    const ProviderJavaDispatchRequestV1& request,
    ProviderMappingRuntimeFactsV1* facts,
    void* user_data) noexcept;
#endif

OperationMask ProviderMappingOperationMask(
    ProviderMappingOperation operation) noexcept;

ProviderMappingDecisionV1 EvaluateProviderMapping(
    const ProviderMappingRequestV1& request) noexcept;

ProviderMappingOperation ProviderJavaDispatchMappingOperation(
    const ProviderJavaDispatchRequestV1& request) noexcept;

bool ProviderJavaDispatchMatchesBinding(
    const ProviderJavaDispatchRequestV1& request,
    const ProviderRouteBindingV1& binding) noexcept;

ProviderMappingRequestV1 BuildProviderMappingRequest(
    const ProviderJavaDispatchRequestV1& request,
    const ProviderAdapterProfileMatchV1& profile_match,
    OperationMask supported_operations,
    const ProviderRouteBindingV1* binding,
    const provenance::ResolveResult& reverse,
    bool runtime_available) noexcept;

ProviderMappingDecisionV1 EvaluateProviderMappingWithResolver(
    const ProviderJavaDispatchRequestV1& request,
    ProviderMappingRuntimeResolverV1 resolver,
    void* user_data) noexcept;

}  // namespace pathguard
