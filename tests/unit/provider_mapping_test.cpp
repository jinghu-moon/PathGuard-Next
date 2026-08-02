#include "pathguard/provider_mapping.h"
#include "test_assert.h"

namespace {

pathguard::ProviderRouteBindingV1 CompleteBinding() {
    pathguard::ProviderRouteBindingV1 binding;
    binding.context = {10437, 0, 1, 7, 41, 2, 3};
    binding.visible_source_path =
        "/storage/emulated/0/Pictures/source/a.jpg";
    binding.backing_target_path =
        "/storage/emulated/0/Pictures/target/a.jpg";
    binding.provider_uri =
        "content://media/external_primary/images/media/42";
    binding.stable_document_id = "primary:Pictures/source/a.jpg";
    binding.fd_identity.kind =
        pathguard::provenance::IdentityKind::kFileHandle;
    binding.fd_identity.volume = "emulated:0";
    binding.fd_identity.handle = {1, 2, 3, 4};
    binding.reverse_record.scope.caller_uid = binding.context.caller_uid;
    binding.reverse_record.scope.user_id = binding.context.user_id;
    binding.reverse_record.scope.identity_epoch = 9;
    binding.reverse_record.key.storage_root_id = "emulated:0";
    binding.reverse_record.key.target_relative_path =
        "Pictures/target/a.jpg";
    binding.reverse_record.identity = binding.fd_identity;
    binding.reverse_record.logical_source_path = binding.visible_source_path;
    binding.reverse_record.rule_id = binding.context.rule_id;
    binding.reverse_record.content_generation = 6;
    binding.reverse_record.created_plan_generation =
        binding.context.plan_generation;
    binding.reverse_record.bound_plan_generation =
        binding.context.plan_generation;
    binding.reverse_record.commit_sequence = 1;
    return binding;
}

pathguard::ProviderMappingRequestV1 CompleteRequest(
        const pathguard::ProviderRouteBindingV1* binding) {
    pathguard::ProviderMappingRequestV1 request;
    request.operation = pathguard::ProviderMappingOperation::kQuery;
    request.profile_match = {
        0x616c696f74682d6d,
        pathguard::ProviderAdapterProfileReason::kMatched,
    };
    request.supported_operations =
        pathguard::kProviderCompositeOperationsV1;
    request.binding = binding;
    request.reverse = {
        pathguard::provenance::ResolveStatus::kUnique,
        pathguard::provenance::Error::kNone,
        binding->reverse_record,
    };
    request.runtime_available = true;
    return request;
}

pathguard::ProviderJavaDispatchRequestV1 JavaRequest(
        pathguard::ProviderJavaDispatchRole role,
        pathguard::OperationMask operations,
        pathguard::ProviderJavaIdentifierKind kind,
        std::string_view identifier) {
    pathguard::ProviderJavaDispatchRequestV1 request;
    request.role = role;
    request.operations = operations;
    request.identifier.kind = kind;
    request.identifier.size = static_cast<std::uint16_t>(identifier.size());
    for (std::size_t index = 0; index < identifier.size(); ++index) {
        request.identifier.bytes[index] = identifier[index];
    }
    return request;
}

}  // namespace

int main() {
    using namespace pathguard;

    auto binding = CompleteBinding();
    auto request = CompleteRequest(&binding);
    auto decision = EvaluateProviderMapping(request);
    assert(decision.rewrite());
    assert(decision.reason == ProviderMappingReason::kReady);
    assert(decision.required_operation == kOperationProviderQuery);

    binding.stable_document_id.clear();
    request = CompleteRequest(&binding);
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kFailOpen);
    assert(decision.reason == ProviderMappingReason::kBindingInvalid);
    assert(decision.binding_reason
           == ProviderRouteBindingReason::kExternalIdentityMissing);

    binding = CompleteBinding();
    binding.provider_uri.clear();
    request = CompleteRequest(&binding);
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kFailOpen);
    assert(decision.binding_reason
           == ProviderRouteBindingReason::kExternalIdentityMissing);

    binding = CompleteBinding();
    binding.backing_target_path = binding.visible_source_path;
    request = CompleteRequest(&binding);
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kFailOpen);
    assert(decision.binding_reason
           == ProviderRouteBindingReason::kInvalidPathMapping);

    binding = CompleteBinding();
    binding.fd_identity.handle.clear();
    binding.reverse_record.identity = binding.fd_identity;
    request = CompleteRequest(&binding);
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kFailOpen);
    assert(decision.binding_reason
           == ProviderRouteBindingReason::kWeakFdIdentity);

    binding = CompleteBinding();
    binding.reverse_record.logical_source_path = binding.backing_target_path;
    request = CompleteRequest(&binding);
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kFailOpen);
    assert(decision.binding_reason
           == ProviderRouteBindingReason::kReverseMismatch);

    binding = CompleteBinding();
    request = CompleteRequest(&binding);
    request.reverse = {
        provenance::ResolveStatus::kAmbiguous,
        provenance::Error::kIdentityMismatch,
        std::nullopt,
    };
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition
           == ProviderMappingDisposition::kAmbiguousReverse);
    assert(decision.reason == ProviderMappingReason::kReverseAmbiguous);

    request = CompleteRequest(&binding);
    request.reverse = {};
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kUnsupported);
    assert(decision.reason == ProviderMappingReason::kReverseMissing);

    request = CompleteRequest(&binding);
    request.profile_match.reason =
        ProviderAdapterProfileReason::kBuildMismatch;
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kUnsupported);
    assert(decision.reason == ProviderMappingReason::kProfileMismatch);

    request = CompleteRequest(&binding);
    request.runtime_available = false;
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kFailOpen);
    assert(decision.reason == ProviderMappingReason::kRuntimeUnavailable);

    request = CompleteRequest(&binding);
    request.binding = nullptr;
    request.reverse = {};
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kPass);
    assert(decision.reason == ProviderMappingReason::kNoRoute);

    request = CompleteRequest(&binding);
    request.supported_operations &= ~kOperationProviderQuery;
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kUnsupported);
    assert(decision.reason == ProviderMappingReason::kOperationUnsupported);

    request = CompleteRequest(&binding);
    auto conflicting = binding.reverse_record;
    conflicting.commit_sequence = 2;
    request.reverse.record = conflicting;
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition
           == ProviderMappingDisposition::kAmbiguousReverse);
    assert(decision.reason == ProviderMappingReason::kReverseMismatch);

    request = CompleteRequest(&binding);
    request.operation = ProviderMappingOperation::kUnknown;
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kUnsupported);
    assert(decision.reason == ProviderMappingReason::kInvalidOperation);

    auto java_request = JavaRequest(
        ProviderJavaDispatchRole::kQuery, kOperationProviderQuery,
        ProviderJavaIdentifierKind::kContentUri, binding.provider_uri);
    assert(ProviderJavaDispatchMappingOperation(java_request)
           == ProviderMappingOperation::kQuery);
    assert(ProviderJavaDispatchMatchesBinding(java_request, binding));
    request = BuildProviderMappingRequest(
        java_request,
        {0x616c696f74682d6d, ProviderAdapterProfileReason::kMatched},
        kProviderCompositeOperationsV1, &binding,
        CompleteRequest(&binding).reverse, true);
    decision = EvaluateProviderMapping(request);
    assert(decision.rewrite());

    java_request.identifier.bytes[10] = 'x';
    request = BuildProviderMappingRequest(
        java_request,
        {0x616c696f74682d6d, ProviderAdapterProfileReason::kMatched},
        kProviderCompositeOperationsV1, &binding,
        CompleteRequest(&binding).reverse, true);
    assert(request.binding == nullptr);
    decision = EvaluateProviderMapping(request);
    assert(decision.disposition == ProviderMappingDisposition::kPass);

    java_request = JavaRequest(
        ProviderJavaDispatchRole::kQuery,
        kOperationProviderQuery | kOperationDirectoryIterate,
        ProviderJavaIdentifierKind::kDocumentId,
        binding.stable_document_id);
    assert(ProviderJavaDispatchMappingOperation(java_request)
           == ProviderMappingOperation::kDirectoryQuery);
    assert(ProviderMappingOperationMask(ProviderMappingOperation::kDirectoryQuery)
           == (kOperationProviderQuery | kOperationDirectoryIterate));

    java_request.role = ProviderJavaDispatchRole::kOpen;
    java_request.operations = kOperationOpenRead | kOperationOpenWrite;
    assert(ProviderJavaDispatchMappingOperation(java_request)
           == ProviderMappingOperation::kOpenReadWrite);
    assert(ProviderMappingOperationMask(ProviderMappingOperation::kOpenReadWrite)
           == (kOperationOpenRead | kOperationOpenWrite));

    java_request.role = ProviderJavaDispatchRole::kUpdate;
    java_request.operations = kOperationMetadataMutation | kOperationRename;
    assert(ProviderJavaDispatchMappingOperation(java_request)
           == ProviderMappingOperation::kMetadataRename);

    java_request.role = ProviderJavaDispatchRole::kDelete;
    java_request.operations = kOperationUnlink;
    assert(ProviderJavaDispatchMappingOperation(java_request)
           == ProviderMappingOperation::kDeleteFile);
    return 0;
}
