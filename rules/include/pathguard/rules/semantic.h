#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/mount_backend.h"
#include "pathguard/policy_v6.h"
#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules/schema_v2.h"
#include "pathguard/rules_contract.h"

namespace pathguard::rules {

struct NormalizedPath {
    std::string bytes;
    std::vector<std::uint16_t> component_offsets;

    bool operator==(const NormalizedPath&) const = default;
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
    std::optional<CanonicalPolicyV2> canonical_v2;
    std::optional<pathguard::PolicyV6> policy_v6;
    PolicyRequirements requirements;
    std::optional<PolicyBlob> blob;
    CompileStatistics statistics;
    std::vector<Diagnostic> diagnostics;

    bool ok() const;
};

std::optional<NormalizedPath> NormalizeRulePath(
    std::string_view input, const RulesLimits& limits,
    CompileStatistics* statistics = nullptr);
bool IsSameOrAncestor(const NormalizedPath& ancestor,
                      const NormalizedPath& path);

RulesBuildResult CompileRules(const SourceBuffer& source,
                              const RulesLimits& limits);
AdmissionResult AdmitPolicy(const pathguard::PolicyV6& policy,
                            const PolicyRequirements& requirements,
                            const DeviceSnapshot& snapshot);
bool VerifyPolicyBlob(const pathguard::PolicyV6& policy, const PolicyBlob& blob);
bool VerifyPolicyBytes(const std::vector<std::uint8_t>& bytes,
                       std::uint64_t expected_content_generation);

}  // namespace pathguard::rules
