#pragma once

#include <cstdint>

#include "pathguard/action_admission.h"
#include "pathguard/pattern_runtime.h"

namespace pathguard::complete_vfs {

enum class ApplyStatus : std::uint8_t {
    kApplied,
    kUnsupported,
    kInvalidPlan,
    kBackendFailure,
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual OperationMask operations() const = 0;
    virtual bool Apply(const pattern::OperationPlan& plan) = 0;
};

inline ApplyStatus Apply(const pattern::OperationPlan& plan,
                         const CapabilitySnapshot& capabilities,
                         OperationMask required,
                         Backend* backend) {
    if (!plan.accepted) return ApplyStatus::kInvalidPlan;
    const ActionAdmission admission = AdmitAction({
        AdmissionDomain::kCompleteVfs,
        kCapabilityFuseCompletePath,
        required,
        true,
    }, capabilities, plan.plan_generation);
    if (!admission.active() || backend == nullptr
        || (backend->operations() & required) != required) {
        return ApplyStatus::kUnsupported;
    }
    return backend->Apply(plan) ? ApplyStatus::kApplied
                                : ApplyStatus::kBackendFailure;
}

}  // namespace pathguard::complete_vfs
