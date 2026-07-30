#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pathguard/pattern_runtime.h"
#include "pathguard/rules/schema_v2.h"

namespace pathguard::rules {

struct PackageIdentityBinding {
    std::string package;
    std::int32_t caller_uid = -1;
    std::uint32_t user_id = 0;
    bool requires_package_attribution = true;
};

struct PatternPlanBuildResult {
    std::optional<pathguard::pattern::PatternPlan> plan;
    std::vector<std::string> errors;

    bool ok() const { return plan.has_value() && errors.empty(); }
};

PatternPlanBuildResult BuildPatternPlan(
    const CanonicalPolicyV2& policy,
    const std::vector<PackageIdentityBinding>& bindings);

}  // namespace pathguard::rules
