#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pathguard/pattern.h"

namespace pathguard {

enum class PolicyMatchKind : std::uint8_t { kLiteralPrefix, kGlob };
enum class PolicyObjectType : std::uint8_t { kAny, kFile, kDirectory };
enum class PolicyActionKind : std::uint8_t { kDeny, kRedirect, kObserve, kExport };
enum class PolicyExecutionDomain : std::uint8_t {
    kMount,
    kAppPath,
    kProvider,
    kCompleteVfs,
    kEvent,
};
enum class PolicyPreserveMode : std::uint8_t { kNotApplicable, kRelative };
enum class PolicyCollisionMode : std::uint8_t { kNotApplicable, kReject };
enum class PolicyReverseMode : std::uint8_t { kNone, kStaticUnique, kProvenance };

struct PolicySelectorV6 {
    PolicyMatchKind match_kind = PolicyMatchKind::kLiteralPrefix;
    PolicyObjectType object_type = PolicyObjectType::kAny;
    std::string root;
    pattern::PatternProgram base_pattern;
    std::vector<pattern::PatternProgram> except_patterns;

    bool operator==(const PolicySelectorV6&) const = default;
};

struct PolicyActionV6 {
    std::uint32_t selector_index = 0;
    std::uint64_t rule_id = 0;
    std::uint64_t required_capabilities = 0;
    std::uint64_t required_operations = 0;
    std::int32_t priority = 0;
    std::uint32_t options = 0;
    PolicyActionKind kind = PolicyActionKind::kDeny;
    PolicyExecutionDomain domain = PolicyExecutionDomain::kMount;
    PolicyPreserveMode preserve = PolicyPreserveMode::kNotApplicable;
    PolicyCollisionMode collision = PolicyCollisionMode::kNotApplicable;
    PolicyReverseMode reverse = PolicyReverseMode::kNone;
    std::string target;

    bool operator==(const PolicyActionV6&) const = default;
};

struct PolicyPackageV6 {
    std::string package;
    bool all_users = false;
    bool all_processes = true;
    bool provider_enabled = false;
    std::vector<std::uint32_t> users;
    std::vector<std::string> processes;
    std::vector<PolicySelectorV6> selectors;
    std::vector<PolicyActionV6> actions;
    std::uint64_t plan_generation = 0;

    bool operator==(const PolicyPackageV6&) const = default;
};

struct PolicyV6 {
    bool allow_legacy_mount = false;
    std::vector<PolicyPackageV6> packages;

    bool operator==(const PolicyV6&) const = default;
};

struct PolicyV6DecodeResult {
    bool ok = false;
    std::string error;
    std::uint64_t content_generation = 0;
};

std::uint64_t ComputePolicyV6PlanGeneration(const PolicyPackageV6& package,
                                            bool allow_legacy_mount);
std::uint64_t ComputePolicyV6ContentGeneration(const PolicyV6& policy);
bool EncodePolicyV6(const PolicyV6& policy, std::vector<std::uint8_t>* output,
                    std::string* error);
PolicyV6DecodeResult DecodePolicyV6(const std::vector<std::uint8_t>& input,
                                    PolicyV6* policy);

}  // namespace pathguard
