#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/mount_backend.h"
#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/document.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules_contract.h"

namespace pathguard::rules {

struct NormalizedPath {
    std::string bytes;
    std::vector<std::uint16_t> component_offsets;

    bool operator==(const NormalizedPath&) const = default;
};

struct ResolvedDenyRule {
    RuleId id = 0;
    NormalizedPath path;
};

struct ResolvedRedirectRule {
    RuleId id = 0;
    NormalizedPath source;
    NormalizedPath target;
};

struct ResolvedAppPolicy {
    std::string package;
    bool enabled = true;
    std::vector<std::int32_t> users;
    std::vector<std::string> processes;
    bool file_picker = false;
    std::vector<ResolvedDenyRule> deny;
    std::vector<ResolvedRedirectRule> redirects;
};

struct ResolvedPolicy {
    bool allow_legacy_mount = false;
    std::vector<ResolvedAppPolicy> apps;
};

struct CanonicalRedirectRule {
    NormalizedPath source;
    NormalizedPath target;

    bool operator==(const CanonicalRedirectRule&) const = default;
};

struct CanonicalAppPolicy {
    std::string package;
    std::vector<std::int32_t> users;
    std::vector<std::string> processes;
    bool file_picker = false;
    std::vector<CanonicalRedirectRule> redirects;

    bool operator==(const CanonicalAppPolicy&) const = default;
};

struct CanonicalPolicy {
    bool allow_legacy_mount = false;
    std::vector<CanonicalAppPolicy> apps;

    bool operator==(const CanonicalPolicy&) const = default;
};

struct PolicyRequirements {
    MountActionMask mount_actions = 0;
    bool provider = false;
    bool topology = false;
};

struct PolicyBlob {
    std::vector<std::uint8_t> bytes;
    std::uint64_t content_generation = 0;
};

struct DeviceSnapshot {
    MountBackendCapabilities mount;
    bool provider_supported = false;
    bool topology_supported = false;
    std::uint64_t capability_generation = 0;
    std::uint64_t topology_generation = 0;
};

struct AdmissionResult {
    bool admitted = false;
    MountBackendKind backend = MountBackendKind::kUnsupported;
    MountBackendReason reason = MountBackendReason::kCapabilityMissing;
    std::uint64_t content_generation = 0;
    std::uint64_t capability_generation = 0;
    std::uint64_t topology_generation = 0;
};

struct RulesBuildResult {
    std::optional<ResolvedPolicy> resolved;
    std::optional<CanonicalPolicy> canonical;
    PolicyRequirements requirements;
    std::optional<PolicyBlob> blob;
    CompileStatistics statistics;
    std::vector<Diagnostic> diagnostics;
    OriginMap origins;

    bool ok() const;
};

std::optional<NormalizedPath> NormalizeRulePath(
    std::string_view input, const RulesLimits& limits,
    CompileStatistics* statistics = nullptr);
bool IsSameOrAncestor(const NormalizedPath& ancestor,
                      const NormalizedPath& path);

RulesBuildResult CompileRules(const SourceBuffer& source,
                              const RulesLimits& limits);
AdmissionResult AdmitPolicy(const CanonicalPolicy& policy,
                            const PolicyRequirements& requirements,
                            const DeviceSnapshot& snapshot);
bool VerifyPolicyBlob(const CanonicalPolicy& policy, const PolicyBlob& blob);
bool VerifyPolicyBytes(const std::vector<std::uint8_t>& bytes,
                       std::uint64_t expected_content_generation);

}  // namespace pathguard::rules
