#include "pathguard/provider_adapter_profile.h"
#include "test_assert.h"

namespace {

pathguard::ProviderBuildIdentityV1 AliothMediaIdentity() {
    pathguard::ProviderBuildIdentityV1 identity;
    identity.kind = pathguard::ProviderContractKind::kMediaStore;
    identity.sdk = 33;
    identity.version_code = 33;
    identity.apex_version_code = 339990000;
    identity.apk_sha256 = {
        0xf8, 0xf7, 0x1e, 0xae, 0xdd, 0x78, 0xa1, 0xbb,
        0x0c, 0x3b, 0xb3, 0xd8, 0x14, 0x05, 0xf2, 0x52,
        0x92, 0x21, 0xfe, 0x63, 0x58, 0xca, 0x5f, 0xe4,
        0xce, 0x74, 0xa5, 0xc3, 0x85, 0x3c, 0xa9, 0xed,
    };
    return identity;
}

pathguard::ProviderBuildIdentityV1 AliothDocumentsIdentity() {
    pathguard::ProviderBuildIdentityV1 identity;
    identity.kind = pathguard::ProviderContractKind::kDocuments;
    identity.sdk = 33;
    identity.version_code = 33;
    identity.apk_sha256 = {
        0x44, 0xa4, 0x2e, 0xee, 0xf3, 0x64, 0xa1, 0xbd,
        0x53, 0x8e, 0x75, 0xc3, 0x55, 0x3e, 0x45, 0xc9,
        0xad, 0xfb, 0xd0, 0x4a, 0x4b, 0x7a, 0xf2, 0xdc,
        0xfa, 0xa3, 0xf7, 0x6b, 0xb4, 0x48, 0x85, 0x6e,
    };
    return identity;
}

pathguard::ProviderAdapterProfileV1 AliothMediaProfile() {
    pathguard::ProviderAdapterProfileV1 profile;
    profile.profile_id = 0x616c696f74682d6d;
    profile.build = AliothMediaIdentity();
    profile.supported_checks = pathguard::kProviderContractRequiredChecksV1;
    profile.supported_operations = pathguard::kProviderCompositeOperationsV1;
    return profile;
}

pathguard::ProviderAdapterProfileV1 AliothDocumentsProfile() {
    pathguard::ProviderAdapterProfileV1 profile;
    profile.profile_id = 0x616c696f74682d64;
    profile.build = AliothDocumentsIdentity();
    profile.supported_checks = pathguard::kProviderContractRequiredChecksV1;
    profile.supported_operations = pathguard::kProviderCompositeOperationsV1;
    return profile;
}

pathguard::ProviderRouteBindingV1 CompleteBinding() {
    pathguard::ProviderRouteBindingV1 binding;
    binding.context = {10437, 0, 1, 7, 41, 2, 3};
    binding.visible_source_path = "/storage/emulated/0/Pictures/source/a.jpg";
    binding.backing_target_path = "/storage/emulated/0/Pictures/target/a.jpg";
    binding.provider_uri = "content://media/external_primary/images/media/42";
    binding.stable_document_id = "primary:Pictures/source/a.jpg";
    binding.fd_identity.kind = pathguard::provenance::IdentityKind::kFileHandle;
    binding.fd_identity.volume = "emulated:0";
    binding.fd_identity.handle = {1, 2, 3, 4};
    binding.reverse_record.scope.caller_uid = binding.context.caller_uid;
    binding.reverse_record.scope.user_id = binding.context.user_id;
    binding.reverse_record.scope.identity_epoch = 9;
    binding.reverse_record.key.storage_root_id = "emulated:0";
    binding.reverse_record.key.target_relative_path = "Pictures/target/a.jpg";
    binding.reverse_record.identity = binding.fd_identity;
    binding.reverse_record.logical_source_path = binding.visible_source_path;
    binding.reverse_record.rule_id = binding.context.rule_id;
    binding.reverse_record.content_generation = 6;
    binding.reverse_record.created_plan_generation = binding.context.plan_generation;
    binding.reverse_record.bound_plan_generation = binding.context.plan_generation;
    binding.reverse_record.commit_sequence = 1;
    return binding;
}

}  // namespace

int main() {
    using namespace pathguard;

    const auto profile = AliothMediaProfile();
    const auto identity = AliothMediaIdentity();
    auto match = MatchProviderAdapterProfile(profile, identity);
    assert(match.matched());
    assert(match.profile_id == profile.profile_id);

    auto wrong_hash = identity;
    wrong_hash.apk_sha256[1] = 1;
    match = MatchProviderAdapterProfile(profile, wrong_hash);
    assert(!match.matched());
    assert(match.reason == ProviderAdapterProfileReason::kBuildMismatch);

    auto sdk_only = identity;
    sdk_only.apk_sha256 = {};
    match = MatchProviderAdapterProfile(profile, sdk_only);
    assert(match.reason == ProviderAdapterProfileReason::kInvalidBuildIdentity);

    auto incomplete = profile;
    incomplete.supported_operations &= ~kOperationProviderQuery;
    match = MatchProviderAdapterProfile(incomplete, identity);
    assert(match.reason == ProviderAdapterProfileReason::kContractIncomplete);

    const ProviderAdapterProfileV1 profiles[] = {profile};
    auto selected = SelectProviderAdapterProfile(profiles, 1, identity);
    assert(selected.matched());
    auto unknown_build = identity;
    ++unknown_build.version_code;
    selected = SelectProviderAdapterProfile(profiles, 1, unknown_build);
    assert(selected.reason == ProviderAdapterProfileReason::kNoMatchingProfile);

    ProviderAdapterDeploymentProfileV1 deployment;
    deployment.deployment_profile_id = 0x616c696f74682d31;
    deployment.documents = AliothDocumentsProfile();
    deployment.media = AliothMediaProfile();
    auto deployment_match = MatchProviderAdapterDeployment(
        deployment, AliothDocumentsIdentity(), AliothMediaIdentity());
    assert(deployment_match.matched());
    assert(deployment_match.deployment_profile_id
           == deployment.deployment_profile_id);
    auto wrong_documents = AliothDocumentsIdentity();
    wrong_documents.apk_sha256[3] ^= 1;
    deployment_match = MatchProviderAdapterDeployment(
        deployment, wrong_documents, AliothMediaIdentity());
    assert(!deployment_match.matched());
    assert(deployment_match.reason
           == ProviderAdapterDeploymentReason::kDocumentsMismatch);
    deployment_match = MatchProviderAdapterDeployment(
        deployment, AliothDocumentsIdentity(), AliothDocumentsIdentity());
    assert(deployment_match.reason
           == ProviderAdapterDeploymentReason::kMediaMismatch);

    auto binding = CompleteBinding();
    auto conformance = ValidateProviderRouteBinding(binding);
    assert(conformance.ready());

    binding.fd_identity.handle.clear();
    conformance = ValidateProviderRouteBinding(binding);
    assert(conformance.reason == ProviderRouteBindingReason::kWeakFdIdentity);

    binding = CompleteBinding();
    binding.reverse_record.logical_source_path = binding.backing_target_path;
    conformance = ValidateProviderRouteBinding(binding);
    assert(conformance.reason == ProviderRouteBindingReason::kReverseMismatch);

    binding = CompleteBinding();
    binding.stable_document_id.clear();
    conformance = ValidateProviderRouteBinding(binding);
    assert(conformance.reason == ProviderRouteBindingReason::kExternalIdentityMissing);

    binding = CompleteBinding();
    binding.backing_target_path = binding.visible_source_path;
    conformance = ValidateProviderRouteBinding(binding);
    assert(conformance.reason == ProviderRouteBindingReason::kInvalidPathMapping);
    return 0;
}
