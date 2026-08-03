#include "pathguard/provider_mapping.h"

namespace pathguard {

namespace {

ProviderRouteBindingObservationV1 ValidateMappingBinding(
        const ProviderMappingRequestV1& request,
        const ProviderRouteBindingV1& binding) noexcept {
    if (!binding.context.valid()) {
        return {ProviderRouteBindingReason::kInvalidContext};
    }
    if (!ValidProviderAbsolutePath(binding.visible_source_path)
        || !ValidProviderAbsolutePath(binding.backing_target_path)
        || binding.visible_source_path == binding.backing_target_path) {
        return {ProviderRouteBindingReason::kInvalidPathMapping};
    }
    if (binding.stable_document_id.empty()) {
        return {ProviderRouteBindingReason::kExternalIdentityMissing};
    }
    const bool content_identity = request.identifier_kind
        == ProviderJavaIdentifierKind::kContentUri;
    const bool requires_uri = request.operation
            != ProviderMappingOperation::kReverseLookup
        && (request.identifier_kind == ProviderJavaIdentifierKind::kNone
            || content_identity);
    if (requires_uri
        && binding.provider_uri.compare(0, 10, "content://") != 0) {
        return {ProviderRouteBindingReason::kExternalIdentityMissing};
    }
    if (binding.reverse_mode == ProviderRouteReverseMode::kStaticUnique) {
        if (!ValidProviderAbsolutePath(binding.visible_source_root)
            || !ValidProviderAbsolutePath(binding.backing_target_root)
            || !namespace_projection::NamespaceTargetMatchesV1(
                binding.backing_target_root, binding.namespace_id)
            || !namespace_projection::SameRelativeTail(
                binding.visible_source_path, binding.visible_source_root,
                binding.backing_target_path, binding.backing_target_root)) {
            return {ProviderRouteBindingReason::kInvalidPathMapping};
        }
        return {ProviderRouteBindingReason::kReady};
    }
    if (binding.reverse_mode != ProviderRouteReverseMode::kProvenance
        || !TargetMatchesRouteKey(binding.backing_target_path,
                                  binding.reverse_record.key)) {
        return {ProviderRouteBindingReason::kInvalidPathMapping};
    }
    if (request.operation != ProviderMappingOperation::kReverseLookup) {
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

bool RequiresCommittedReverse(
        ProviderMappingOperation operation,
        ProviderRouteReverseMode reverse_mode) noexcept {
    return operation == ProviderMappingOperation::kReverseLookup
        && reverse_mode == ProviderRouteReverseMode::kProvenance;
}

bool RelativePath(std::string_view path, std::string_view root,
                  std::string_view* tail) noexcept {
    if (tail == nullptr || path.size() < root.size()
        || path.compare(0, root.size(), root) != 0) {
        return false;
    }
    if (path.size() == root.size()) {
        *tail = {};
        return true;
    }
    if (path[root.size()] != '/') return false;
    *tail = path.substr(root.size() + 1);
    return !tail->empty();
}

bool JoinPath(std::string_view root, std::string_view tail,
              std::string* output) {
    if (output == nullptr || root.empty()) return false;
    output->assign(root);
    if (!tail.empty()) output->append("/").append(tail);
    return output->size() <= 4095;
}

bool StorageAbsolutePath(std::string_view input, std::uint32_t user_id,
                         std::string* output) {
    if (input.empty() || output == nullptr) return false;
    if (input.front() == '/') {
        output->assign(input);
    } else {
        *output = "/storage/emulated/" + std::to_string(user_id) + "/";
        output->append(input);
    }
    return ValidProviderAbsolutePath(*output);
}

bool LogicalDocumentPath(const ProviderJavaDispatchRequestV1& request,
                         std::uint32_t user_id, std::string* output) {
    const std::string_view document = request.identifier.value();
    const std::size_t separator = document.find(':');
    return request.identifier.kind == ProviderJavaIdentifierKind::kDocumentId
        && separator != std::string_view::npos && separator + 1 < document.size()
        && StorageAbsolutePath(document.substr(separator + 1), user_id, output);
}

std::string StableDocumentId(std::string_view visible_path,
                             std::uint32_t user_id) {
    const std::string prefix =
        "/storage/emulated/" + std::to_string(user_id) + "/";
    if (!visible_path.starts_with(prefix)) return {};
    return "primary:" + std::string(visible_path.substr(prefix.size()));
}

}  // namespace

bool MaterializeStaticProviderRouteBinding(
        const ProviderJavaDispatchRequestV1& request,
        const ProviderRouteBindingV1& route_template,
        ProviderRouteBindingV1* output) noexcept {
    if (output == nullptr
        || route_template.reverse_mode
            != ProviderRouteReverseMode::kStaticUnique
        || !route_template.context.valid()
        || !ValidProviderAbsolutePath(route_template.visible_source_root)
        || !ValidProviderAbsolutePath(route_template.backing_target_root)
        || !namespace_projection::NamespaceTargetMatchesV1(
            route_template.backing_target_root,
            route_template.namespace_id)) {
        return false;
    }
    try {
        std::string observed;
        if (!request.file_path.value().empty()) {
            if (!StorageAbsolutePath(request.file_path.value(),
                                     route_template.context.user_id,
                                     &observed)) {
                return false;
            }
        } else if (!LogicalDocumentPath(
                       request, route_template.context.user_id, &observed)) {
            return false;
        }
        std::string_view tail;
        *output = route_template;
        if (RelativePath(observed, route_template.visible_source_root, &tail)) {
            output->visible_source_path = observed;
            if (!JoinPath(route_template.backing_target_root, tail,
                          &output->backing_target_path)) {
                return false;
            }
        } else if (RelativePath(
                       observed, route_template.backing_target_root, &tail)) {
            output->backing_target_path = observed;
            if (!JoinPath(route_template.visible_source_root, tail,
                          &output->visible_source_path)) {
                return false;
            }
        } else {
            return false;
        }
        output->stable_document_id = StableDocumentId(
            output->visible_source_path, output->context.user_id);
        if (request.identifier.kind == ProviderJavaIdentifierKind::kContentUri) {
            output->provider_uri = std::string(request.identifier.value());
        }
        return ValidateProviderRouteBinding(*output).ready();
    } catch (const std::bad_alloc&) {
        *output = {};
        return false;
    }
}

bool BuildProviderVisibleMediaPath(
        const ProviderRouteBindingV1& binding,
        std::string* relative_path,
        std::string* display_name) {
    if (relative_path == nullptr || display_name == nullptr) return false;
    const std::string volume_prefix =
        "/storage/emulated/" + std::to_string(binding.context.user_id) + "/";
    if (!binding.visible_source_path.starts_with(volume_prefix)) return false;
    const std::string_view volume_relative =
        std::string_view(binding.visible_source_path).substr(
            volume_prefix.size());
    const std::size_t separator = volume_relative.rfind('/');
    if (volume_relative.empty()) return false;
    if (separator == std::string_view::npos) {
        relative_path->clear();
        display_name->assign(volume_relative);
    } else {
        relative_path->assign(volume_relative.substr(0, separator + 1));
        display_name->assign(volume_relative.substr(separator + 1));
    }
    return !display_name->empty();
}

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

    const auto binding = ValidateMappingBinding(request, *request.binding);
    output.binding_reason = binding.reason;
    if (!binding.ready()) {
        output.disposition = ProviderMappingDisposition::kFailOpen;
        output.reason = ProviderMappingReason::kBindingInvalid;
        return output;
    }
    if (!RequiresCommittedReverse(
            request.operation, request.binding->reverse_mode)) {
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
    if (request.file_path.value().empty()) return false;
    if (binding.reverse_mode == ProviderRouteReverseMode::kStaticUnique) {
        return true;  // Materialization already proved membership and tail parity.
    }
    return request.file_path.value() == binding.backing_target_path;
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
    output.identifier_kind = request.identifier.kind;
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
