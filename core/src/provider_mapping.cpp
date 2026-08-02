#include "pathguard/provider_mapping.h"

namespace pathguard {

namespace {

ProviderRouteBindingObservationV1 ValidateMappingBinding(
        ProviderMappingOperation operation,
        const ProviderRouteBindingV1& binding) noexcept {
    if (!binding.context.valid()) {
        return {ProviderRouteBindingReason::kInvalidContext};
    }
    if (!ValidProviderAbsolutePath(binding.visible_source_path)
        || !ValidProviderAbsolutePath(binding.backing_target_path)
        || binding.visible_source_path == binding.backing_target_path
        || !TargetMatchesRouteKey(binding.backing_target_path,
                                  binding.reverse_record.key)) {
        return {ProviderRouteBindingReason::kInvalidPathMapping};
    }
    if (operation != ProviderMappingOperation::kReverseLookup
        && binding.provider_uri.compare(0, 10, "content://") != 0) {
        return {ProviderRouteBindingReason::kExternalIdentityMissing};
    }
    if (binding.stable_document_id.empty()) {
        return {ProviderRouteBindingReason::kExternalIdentityMissing};
    }
    if (operation != ProviderMappingOperation::kReverseLookup) {
        return {ProviderRouteBindingReason::kReady};
    }
    if (!binding.fd_identity.Strong()) {
        return {ProviderRouteBindingReason::kWeakFdIdentity};
    }
    const provenance::RouteRecord& reverse = binding.reverse_record;
    if (reverse.scope.caller_uid != binding.context.caller_uid
        || reverse.scope.user_id != binding.context.user_id
        || reverse.identity != binding.fd_identity
        || reverse.logical_source_path != binding.visible_source_path
        || reverse.rule_id != binding.context.rule_id
        || reverse.created_plan_generation != binding.context.plan_generation
        || reverse.bound_plan_generation != binding.context.plan_generation
        || reverse.commit_sequence == 0) {
        return {ProviderRouteBindingReason::kReverseMismatch};
    }
    return {ProviderRouteBindingReason::kReady};
}

bool RequiresCommittedReverse(ProviderMappingOperation operation) noexcept {
    return operation == ProviderMappingOperation::kReverseLookup;
}

}  // namespace

OperationMask ProviderMappingOperationMask(
        ProviderMappingOperation operation) noexcept {
    switch (operation) {
        case ProviderMappingOperation::kQuery:
            return kOperationProviderQuery;
        case ProviderMappingOperation::kDirectoryQuery:
            return kOperationProviderQuery | kOperationDirectoryIterate;
        case ProviderMappingOperation::kCreate:
            return kOperationProviderInsert | kOperationCreate;
        case ProviderMappingOperation::kOpenRead:
            return kOperationOpenRead;
        case ProviderMappingOperation::kOpenWrite:
            return kOperationOpenWrite;
        case ProviderMappingOperation::kOpenReadWrite:
            return kOperationOpenRead | kOperationOpenWrite;
        case ProviderMappingOperation::kMetadataMutation:
            return kOperationMetadataMutation;
        case ProviderMappingOperation::kMetadataRename:
            return kOperationMetadataMutation | kOperationRename;
        case ProviderMappingOperation::kRename:
            return kOperationRename;
        case ProviderMappingOperation::kDelete:
            return kOperationUnlink | kOperationRmdir;
        case ProviderMappingOperation::kDeleteFile:
            return kOperationUnlink;
        case ProviderMappingOperation::kDeleteDirectory:
            return kOperationRmdir;
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

    const auto binding = ValidateMappingBinding(
        request.operation, *request.binding);
    output.binding_reason = binding.reason;
    if (!binding.ready()) {
        output.disposition = ProviderMappingDisposition::kFailOpen;
        output.reason = ProviderMappingReason::kBindingInvalid;
        return output;
    }
    if (!RequiresCommittedReverse(request.operation)) {
        output.disposition = ProviderMappingDisposition::kRewrite;
        output.reason = ProviderMappingReason::kReady;
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

ProviderMappingOperation ProviderJavaDispatchMappingOperation(
        const ProviderJavaDispatchRequestV1& request) noexcept {
    if (request.version != kProviderJavaDispatchRequestVersionV1) {
        return ProviderMappingOperation::kUnknown;
    }
    switch (request.role) {
        case ProviderJavaDispatchRole::kForwardDocumentPath:
        case ProviderJavaDispatchRole::kQuery:
            return (request.operations & kOperationDirectoryIterate) != 0
                ? ProviderMappingOperation::kDirectoryQuery
                : ProviderMappingOperation::kQuery;
        case ProviderJavaDispatchRole::kReverseDocumentId:
            return ProviderMappingOperation::kReverseLookup;
        case ProviderJavaDispatchRole::kInsert:
            return ProviderMappingOperation::kCreate;
        case ProviderJavaDispatchRole::kOpen: {
            const OperationMask open = request.operations
                & (kOperationOpenRead | kOperationOpenWrite);
            if (open == (kOperationOpenRead | kOperationOpenWrite)) {
                return ProviderMappingOperation::kOpenReadWrite;
            }
            return open == kOperationOpenRead
                ? ProviderMappingOperation::kOpenRead
                : open == kOperationOpenWrite
                    ? ProviderMappingOperation::kOpenWrite
                    : ProviderMappingOperation::kUnknown;
        }
        case ProviderJavaDispatchRole::kUpdate: {
            const OperationMask update = request.operations
                & (kOperationMetadataMutation | kOperationRename);
            if (update == (kOperationMetadataMutation | kOperationRename)) {
                return ProviderMappingOperation::kMetadataRename;
            }
            return update == kOperationMetadataMutation
                ? ProviderMappingOperation::kMetadataMutation
                : update == kOperationRename
                    ? ProviderMappingOperation::kRename
                    : ProviderMappingOperation::kUnknown;
        }
        case ProviderJavaDispatchRole::kDelete: {
            const OperationMask removal = request.operations
                & (kOperationUnlink | kOperationRmdir);
            if (removal == (kOperationUnlink | kOperationRmdir)) {
                return ProviderMappingOperation::kDelete;
            }
            return removal == kOperationUnlink
                ? ProviderMappingOperation::kDeleteFile
                : removal == kOperationRmdir
                    ? ProviderMappingOperation::kDeleteDirectory
                    : ProviderMappingOperation::kUnknown;
        }
    }
    return ProviderMappingOperation::kUnknown;
}

bool ProviderJavaDispatchMatchesBinding(
        const ProviderJavaDispatchRequestV1& request,
        const ProviderRouteBindingV1& binding) noexcept {
    switch (request.identifier.kind) {
        case ProviderJavaIdentifierKind::kContentUri:
            return request.identifier.value() == binding.provider_uri;
        case ProviderJavaIdentifierKind::kDocumentId:
            return request.identifier.value() == binding.stable_document_id;
        case ProviderJavaIdentifierKind::kFilePath:
        case ProviderJavaIdentifierKind::kNone:
            break;
    }
    return !request.file_path.value().empty()
        && request.file_path.value() == binding.backing_target_path;
}

ProviderMappingRequestV1 BuildProviderMappingRequest(
        const ProviderJavaDispatchRequestV1& request,
        const ProviderAdapterProfileMatchV1& profile_match,
        OperationMask supported_operations,
        const ProviderRouteBindingV1* binding,
        const provenance::ResolveResult& reverse,
        bool runtime_available) noexcept {
    ProviderMappingRequestV1 output;
    output.operation = ProviderJavaDispatchMappingOperation(request);
    output.profile_match = profile_match;
    output.supported_operations = supported_operations;
    output.binding = binding != nullptr
            && ProviderJavaDispatchMatchesBinding(request, *binding)
        ? binding : nullptr;
    output.reverse = reverse;
    output.runtime_available = runtime_available;
    return output;
}

ProviderMappingDecisionV1 EvaluateProviderMappingWithResolver(
        const ProviderJavaDispatchRequestV1& request,
        ProviderMappingRuntimeResolverV1 resolver,
        void* user_data) noexcept {
    if (resolver == nullptr) {
        return {};
    }
    ProviderMappingRuntimeFactsV1 facts;
    if (!resolver(request, &facts, user_data)) {
        return {};
    }
    return EvaluateProviderMapping(BuildProviderMappingRequest(
        request, facts.profile_match, facts.supported_operations,
        facts.binding, facts.reverse, facts.runtime_available));
}

}  // namespace pathguard
