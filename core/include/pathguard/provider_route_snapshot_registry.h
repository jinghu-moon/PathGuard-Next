#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "pathguard/provider_adapter_profile.h"
#include "pathguard/provider_route_snapshot_abi.h"

namespace pathguard {

struct ProviderRouteSnapshotBindingV1 {
    std::uint64_t binding_id = 0;
    ProviderRouteBindingV1 binding;
};

struct ProviderRouteSnapshotReverseV1 {
    std::uint64_t reverse_record_id = 0;
    provenance::ResolveResult result;
};

struct ProviderRouteSnapshotLookupV1 {
    const ProviderRouteBindingV1* binding = nullptr;
    const provenance::ResolveResult* reverse = nullptr;

    bool resolved() const noexcept { return binding != nullptr; }
};

// Built before hook installation, then read without synchronization by callbacks.
class ProviderRouteSnapshotRegistryV1 final {
public:
    ProviderRouteSnapshotRegistryV1(
        std::uint64_t generation,
        std::vector<ProviderRouteSnapshotBindingV1> bindings,
        std::vector<ProviderRouteSnapshotReverseV1> reverse_records);

    bool ready() const noexcept { return ready_; }
    std::uint64_t generation() const noexcept { return generation_; }
    ProviderRouteSnapshotLookupV1 Lookup(
        std::uint64_t generation, std::uint64_t binding_id,
        std::uint64_t reverse_record_id) const noexcept;

private:
    std::uint64_t generation_ = 0;
    bool ready_ = false;
    std::vector<ProviderRouteSnapshotBindingV1> bindings_;
    std::vector<ProviderRouteSnapshotReverseV1> reverse_records_;
};

std::unique_ptr<ProviderRouteSnapshotRegistryV1>
DecodeProviderRouteSnapshotV1(
    const PathGuardProviderRouteSnapshotV1& snapshot) noexcept;

}  // namespace pathguard
