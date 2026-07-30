#pragma once

#include <errno.h>
#include <stdint.h>

#include "pathguard/capabilities.h"

namespace pathguard {

enum class ResolverMode : uint8_t { kUnknown, kOpenAt2, kComponentWalk, kUnsupported };

class ResolverProbeCache final {
public:
    void Invalidate(uint64_t topology_generation) noexcept {
        if (topology_generation_ != topology_generation) {
            topology_generation_ = topology_generation;
            mode_ = ResolverMode::kUnknown;
            probe_error_ = 0;
        }
    }
    void ObserveOpenAt2(int error, bool component_walk_available) noexcept {
        if (error == 0) {
            mode_ = ResolverMode::kOpenAt2;
        } else if (error == ENOSYS || error == EINVAL
                   || error == EPERM || error == EACCES) {
            mode_ = component_walk_available
                ? ResolverMode::kComponentWalk : ResolverMode::kUnsupported;
        } else if (error != EAGAIN && error != EXDEV && error != ELOOP) {
            mode_ = ResolverMode::kUnsupported;
        }
        probe_error_ = error;
    }
    ResolverMode mode() const noexcept { return mode_; }
    int probe_error() const noexcept { return probe_error_; }
    CapabilityBits capabilities() const noexcept {
        if (mode_ == ResolverMode::kOpenAt2) return kCapabilityOpenAt2;
        if (mode_ == ResolverMode::kComponentWalk) return kCapabilityComponentFdWalk;
        return 0;
    }
private:
    uint64_t topology_generation_ = 0;
    ResolverMode mode_ = ResolverMode::kUnknown;
    int probe_error_ = 0;
};

}  // namespace pathguard
