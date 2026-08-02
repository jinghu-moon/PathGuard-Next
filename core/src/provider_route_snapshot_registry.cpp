#include "pathguard/provider_route_snapshot_registry.h"

#include <algorithm>

namespace pathguard {

namespace {

template <typename Entry, typename Id>
bool HasUniqueNonZeroIds(const std::vector<Entry>& entries, Id id) noexcept {
    return std::all_of(entries.begin(), entries.end(), [id](const Entry& entry) {
        return (entry.*id) != 0;
    }) && std::adjacent_find(
        entries.begin(), entries.end(), [id](const Entry& left, const Entry& right) {
            return (left.*id) == (right.*id);
        }) == entries.end();
}

}  // namespace

ProviderRouteSnapshotRegistryV1::ProviderRouteSnapshotRegistryV1(
        std::uint64_t generation,
        std::vector<ProviderRouteSnapshotBindingV1> bindings,
        std::vector<ProviderRouteSnapshotReverseV1> reverse_records)
    : generation_(generation),
      bindings_(std::move(bindings)),
      reverse_records_(std::move(reverse_records)) {
    std::sort(bindings_.begin(), bindings_.end(),
              [](const auto& left, const auto& right) {
                  return left.binding_id < right.binding_id;
              });
    std::sort(reverse_records_.begin(), reverse_records_.end(),
              [](const auto& left, const auto& right) {
                  return left.reverse_record_id < right.reverse_record_id;
              });
    ready_ = generation_ != 0
        && HasUniqueNonZeroIds(bindings_, &ProviderRouteSnapshotBindingV1::binding_id)
        && HasUniqueNonZeroIds(reverse_records_,
            &ProviderRouteSnapshotReverseV1::reverse_record_id);
}

ProviderRouteSnapshotLookupV1 ProviderRouteSnapshotRegistryV1::Lookup(
        std::uint64_t generation, std::uint64_t binding_id,
        std::uint64_t reverse_record_id) const noexcept {
    if (!ready_ || generation == 0 || generation != generation_
        || binding_id == 0) {
        return {};
    }
    const auto binding = std::lower_bound(
        bindings_.begin(), bindings_.end(), binding_id,
        [](const ProviderRouteSnapshotBindingV1& entry, std::uint64_t id) {
            return entry.binding_id < id;
        });
    if (binding == bindings_.end() || binding->binding_id != binding_id) {
        return {};
    }
    if (reverse_record_id == 0) return {&binding->binding, nullptr};

    const auto reverse = std::lower_bound(
        reverse_records_.begin(), reverse_records_.end(), reverse_record_id,
        [](const ProviderRouteSnapshotReverseV1& entry, std::uint64_t id) {
            return entry.reverse_record_id < id;
        });
    if (reverse == reverse_records_.end()
        || reverse->reverse_record_id != reverse_record_id) {
        return {};
    }
    return {&binding->binding, &reverse->result};
}

}  // namespace pathguard
