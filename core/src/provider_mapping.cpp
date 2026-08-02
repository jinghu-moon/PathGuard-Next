#include "pathguard/provider_mapping.h"

namespace pathguard {

OperationMask ProviderMappingOperationMask(
        ProviderMappingOperation operation) noexcept {
    switch (operation) {
        case ProviderMappingOperation::kQuery:
        case ProviderMappingOperation::kDirectoryQuery:
            return kOperationProviderQuery;
        case ProviderMappingOperation::kCreate:
            return kOperationProviderInsert | kOperationCreate;
        case ProviderMappingOperation::kOpenRead:
            return kOperationOpenRead;
        case ProviderMappingOperation::kOpenWrite:
            return kOperationOpenWrite;
        case ProviderMappingOperation::kRename:
            return kOperationRename;
        case ProviderMappingOperation::kDelete:
            return kOperationUnlink | kOperationRmdir;
        case ProviderMappingOperation::kMediaScan:
            return kOperationMediaScan | kOperationReverseMapping;
        case ProviderMappingOperation::kReverseLookup:
            return kOperationReverseMapping;
        case ProviderMappingOperation::kUnknown:
            return 0;
    }
    return 0;
}

ProviderMappingDecisionV1 EvaluateProviderMapping(
        const ProviderMappingRequestV1& request) noexcept {
    ProviderMappingDecisionV1 output;
    output.required_operation = ProviderMappingOperationMask(request.operation);
    if (request.version != kProviderMappingContractVersion
        || output.required_operation == 0) {
        output.disposition = ProviderMappingDisposition::kUnsupported;
        output.reason = ProviderMappingReason::kInvalidOperation;
        return output;
    }
    if (request.binding == nullptr) {
        return output;
    }
    if (!request.profile_match.matched()) {
        output.disposition = ProviderMappingDisposition::kUnsupported;
        output.reason = ProviderMappingReason::kProfileMismatch;
        return output;
    }
    if ((request.supported_operations & output.required_operation)
            != output.required_operation) {
        output.disposition = ProviderMappingDisposition::kUnsupported;
        output.reason = ProviderMappingReason::kOperationUnsupported;
        return output;
    }
    if (!request.runtime_available) {
        output.disposition = ProviderMappingDisposition::kFailOpen;
        output.reason = ProviderMappingReason::kRuntimeUnavailable;
        return output;
    }

    const auto binding = ValidateProviderRouteBinding(*request.binding);
    output.binding_reason = binding.reason;
    if (!binding.ready()) {
        output.disposition = ProviderMappingDisposition::kFailOpen;
        output.reason = ProviderMappingReason::kBindingInvalid;
        return output;
    }
    if (request.reverse.status == provenance::ResolveStatus::kAmbiguous) {
        output.disposition = ProviderMappingDisposition::kAmbiguousReverse;
        output.reason = ProviderMappingReason::kReverseAmbiguous;
        return output;
    }
    if (request.reverse.error != provenance::Error::kNone) {
        output.disposition = ProviderMappingDisposition::kFailOpen;
        output.reason = ProviderMappingReason::kReverseError;
        return output;
    }
    if (request.reverse.status == provenance::ResolveStatus::kNone) {
        output.disposition = ProviderMappingDisposition::kUnsupported;
        output.reason = ProviderMappingReason::kReverseMissing;
        return output;
    }
    if (!request.reverse.record
        || *request.reverse.record != request.binding->reverse_record) {
        output.disposition = ProviderMappingDisposition::kAmbiguousReverse;
        output.reason = ProviderMappingReason::kReverseMismatch;
        return output;
    }

    output.disposition = ProviderMappingDisposition::kRewrite;
    output.reason = ProviderMappingReason::kReady;
    return output;
}

}  // namespace pathguard
