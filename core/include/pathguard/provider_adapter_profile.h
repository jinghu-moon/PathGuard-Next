#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "pathguard/provider_contract.h"
#include "pathguard/provider_route_context.h"
#include "pathguard/route_provenance.h"

namespace pathguard {

inline constexpr std::uint16_t kProviderAdapterProfileVersion = 1;
using ProviderApkDigest = std::array<std::uint8_t, 32>;

struct ProviderBuildIdentityV1 {
    ProviderContractKind kind = ProviderContractKind::kUnknown;
    std::uint32_t sdk = 0;
    std::uint64_t version_code = 0;
    std::uint64_t apex_version_code = 0;
    ProviderApkDigest apk_sha256{};

    bool valid() const noexcept {
        if ((kind != ProviderContractKind::kDocuments
             && kind != ProviderContractKind::kMediaStore)
            || sdk == 0 || version_code == 0) {
            return false;
        }
        for (const std::uint8_t byte : apk_sha256) {
            if (byte != 0) return true;
        }
        return false;
    }

    bool operator==(const ProviderBuildIdentityV1&) const = default;
};

struct ProviderAdapterProfileV1 {
    std::uint16_t version = kProviderAdapterProfileVersion;
    std::uint64_t profile_id = 0;
    ProviderBuildIdentityV1 build;
    ProviderContractChecks supported_checks = 0;
    OperationMask supported_operations = 0;
};

enum class ProviderAdapterProfileReason : std::uint8_t {
    kMatched,
    kInvalidProfile,
    kInvalidBuildIdentity,
    kContractIncomplete,
    kBuildMismatch,
    kNoMatchingProfile,
};

struct ProviderAdapterProfileMatchV1 {
    std::uint64_t profile_id = 0;
    ProviderAdapterProfileReason reason =
        ProviderAdapterProfileReason::kInvalidProfile;

    bool matched() const noexcept {
        return reason == ProviderAdapterProfileReason::kMatched;
    }
};

inline ProviderAdapterProfileMatchV1 MatchProviderAdapterProfile(
        const ProviderAdapterProfileV1& profile,
        const ProviderBuildIdentityV1& observed) noexcept {
    ProviderAdapterProfileMatchV1 output;
    if (!observed.valid()) {
        output.reason = ProviderAdapterProfileReason::kInvalidBuildIdentity;
        return output;
    }
    if (profile.version != kProviderAdapterProfileVersion
        || profile.profile_id == 0 || !profile.build.valid()) {
        output.reason = ProviderAdapterProfileReason::kInvalidProfile;
        return output;
    }
    if ((profile.supported_checks & kProviderContractRequiredChecksV1)
            != kProviderContractRequiredChecksV1
        || (profile.supported_operations & kProviderCompositeOperationsV1)
            != kProviderCompositeOperationsV1) {
        output.reason = ProviderAdapterProfileReason::kContractIncomplete;
        return output;
    }
    if (profile.build != observed) {
        output.reason = ProviderAdapterProfileReason::kBuildMismatch;
        return output;
    }
    output.profile_id = profile.profile_id;
    output.reason = ProviderAdapterProfileReason::kMatched;
    return output;
}

inline ProviderAdapterProfileMatchV1 SelectProviderAdapterProfile(
        const ProviderAdapterProfileV1* profiles, std::size_t profile_count,
        const ProviderBuildIdentityV1& observed) noexcept {
    if (!observed.valid()) {
        return {0, ProviderAdapterProfileReason::kInvalidBuildIdentity};
    }
    if (profiles == nullptr || profile_count == 0) {
        return {0, ProviderAdapterProfileReason::kNoMatchingProfile};
    }
    for (std::size_t i = 0; i < profile_count; ++i) {
        const auto match = MatchProviderAdapterProfile(profiles[i], observed);
        if (match.matched()) return match;
    }
    return {0, ProviderAdapterProfileReason::kNoMatchingProfile};
}

struct ProviderAdapterDeploymentProfileV1 {
    std::uint16_t version = kProviderAdapterProfileVersion;
    std::uint64_t deployment_profile_id = 0;
    ProviderAdapterProfileV1 documents;
    ProviderAdapterProfileV1 media;
};

enum class ProviderAdapterDeploymentReason : std::uint8_t {
    kMatched,
    kInvalidProfile,
    kDocumentsMismatch,
    kMediaMismatch,
};

struct ProviderAdapterDeploymentMatchV1 {
    std::uint64_t deployment_profile_id = 0;
    ProviderAdapterProfileMatchV1 documents;
    ProviderAdapterProfileMatchV1 media;
    ProviderAdapterDeploymentReason reason =
        ProviderAdapterDeploymentReason::kInvalidProfile;

    bool matched() const noexcept {
        return reason == ProviderAdapterDeploymentReason::kMatched;
    }
};

inline ProviderAdapterDeploymentMatchV1 MatchProviderAdapterDeployment(
        const ProviderAdapterDeploymentProfileV1& profile,
        const ProviderBuildIdentityV1& observed_documents,
        const ProviderBuildIdentityV1& observed_media) noexcept {
    ProviderAdapterDeploymentMatchV1 output;
    if (profile.version != kProviderAdapterProfileVersion
        || profile.deployment_profile_id == 0
        || profile.documents.build.kind != ProviderContractKind::kDocuments
        || profile.media.build.kind != ProviderContractKind::kMediaStore) {
        return output;
    }
    output.documents = MatchProviderAdapterProfile(
        profile.documents, observed_documents);
    if (!output.documents.matched()) {
        output.reason = ProviderAdapterDeploymentReason::kDocumentsMismatch;
        return output;
    }
    output.media = MatchProviderAdapterProfile(profile.media, observed_media);
    if (!output.media.matched()) {
        output.reason = ProviderAdapterDeploymentReason::kMediaMismatch;
        return output;
    }
    output.deployment_profile_id = profile.deployment_profile_id;
    output.reason = ProviderAdapterDeploymentReason::kMatched;
    return output;
}

struct ProviderRouteBindingV1 {
    ProviderRouteContext context;
    std::string visible_source_path;
    std::string backing_target_path;
    std::string provider_uri;
    std::string stable_document_id;
    provenance::ObjectIdentity fd_identity;
    provenance::RouteRecord reverse_record;
};

enum class ProviderRouteBindingReason : std::uint8_t {
    kReady,
    kInvalidContext,
    kInvalidPathMapping,
    kExternalIdentityMissing,
    kWeakFdIdentity,
    kReverseMismatch,
};

struct ProviderRouteBindingObservationV1 {
    ProviderRouteBindingReason reason =
        ProviderRouteBindingReason::kInvalidContext;

    bool ready() const noexcept {
        return reason == ProviderRouteBindingReason::kReady;
    }
};

inline bool ValidProviderAbsolutePath(const std::string& path) noexcept {
    if (path.size() < 2 || path.size() > 4095 || path.front() != '/') {
        return false;
    }
    std::size_t component_begin = 1;
    for (std::size_t i = 1; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') continue;
        const std::size_t size = i - component_begin;
        if (size == 0 || size > 255
            || (size == 1 && path[component_begin] == '.')
            || (size == 2 && path[component_begin] == '.'
                && path[component_begin + 1] == '.')) {
            return false;
        }
        component_begin = i + 1;
    }
    return true;
}

inline bool TargetMatchesRouteKey(
        const std::string& backing_target_path,
        const provenance::RouteKey& key) noexcept {
    if (key.storage_root_id.empty() || key.target_relative_path.empty()
        || key.target_relative_path.front() == '/') {
        return false;
    }
    const std::size_t relative_size = key.target_relative_path.size();
    return backing_target_path.size() > relative_size
        && backing_target_path[backing_target_path.size() - relative_size - 1]
            == '/'
        && backing_target_path.compare(
            backing_target_path.size() - relative_size, relative_size,
            key.target_relative_path) == 0;
}

inline ProviderRouteBindingObservationV1 ValidateProviderRouteBinding(
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
    if (binding.provider_uri.compare(0, 10, "content://") != 0
        || binding.stable_document_id.empty()) {
        return {ProviderRouteBindingReason::kExternalIdentityMissing};
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

}  // namespace pathguard
