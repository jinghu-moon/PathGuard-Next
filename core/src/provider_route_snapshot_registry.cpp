#include "pathguard/provider_route_snapshot_registry.h"

#include <algorithm>
#include <new>
#include <string>

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

bool CopyBytes(const PathGuardProviderRouteBytesV1& input,
               std::uint32_t maximum, bool required,
               std::string* output) {
    if (output == nullptr || input.size > maximum
        || (input.size != 0 && input.data == nullptr)
        || (required && input.size == 0)) {
        return false;
    }
    if (input.size == 0) {
        output->clear();
        return true;
    }
    output->assign(reinterpret_cast<const char*>(input.data), input.size);
    return output->find('\0') == std::string::npos;
}

bool DecodeBinding(const PathGuardProviderRouteBindingV1& input,
                   ProviderRouteSnapshotBindingV1* binding,
                   ProviderRouteSnapshotReverseV1* reverse) {
    if (binding == nullptr || reverse == nullptr
        || input.version != PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_VERSION
        || input.size < sizeof(PathGuardProviderRouteBindingV1)
        || input.reserved != 0 || input.binding_id == 0
        || input.caller_uid < 10000 || input.plan_generation == 0
        || input.rule_id == 0
        || (input.reverse_mode != PATHGUARD_PROVIDER_ROUTE_REVERSE_STATIC_UNIQUE
            && input.reverse_mode
                != PATHGUARD_PROVIDER_ROUTE_REVERSE_PROVENANCE)) {
        return false;
    }
    *binding = {};
    *reverse = {};
    binding->binding_id = input.binding_id;
    auto& output = binding->binding;
    output.context = {
        input.caller_uid, input.user_id, input.package_index,
        input.plan_generation, input.rule_id, 0, 0,
    };
    output.reverse_mode = input.reverse_mode
            == PATHGUARD_PROVIDER_ROUTE_REVERSE_STATIC_UNIQUE
        ? ProviderRouteReverseMode::kStaticUnique
        : ProviderRouteReverseMode::kProvenance;
    if (!CopyBytes(input.visible_source_path,
                   PATHGUARD_PROVIDER_ROUTE_PATH_MAX, true,
                   &output.visible_source_path)
        || !CopyBytes(input.backing_target_path,
                      PATHGUARD_PROVIDER_ROUTE_PATH_MAX, true,
                      &output.backing_target_path)
        || !CopyBytes(input.provider_uri,
                      PATHGUARD_PROVIDER_ROUTE_IDENTIFIER_MAX, false,
                      &output.provider_uri)
        || !CopyBytes(input.stable_document_id,
                      PATHGUARD_PROVIDER_ROUTE_IDENTIFIER_MAX, false,
                      &output.stable_document_id)
        || !ValidProviderAbsolutePath(output.visible_source_path)
        || !ValidProviderAbsolutePath(output.backing_target_path)
        || output.visible_source_path == output.backing_target_path) {
        return false;
    }
    if (output.reverse_mode == ProviderRouteReverseMode::kStaticUnique) {
        if (input.reverse_record_id != 0
            || input.identity_kind != PATHGUARD_PROVIDER_ROUTE_IDENTITY_NONE
            || input.object_type != 0
            || !CopyBytes(input.namespace_id,
                          namespace_projection::kNamespaceIdSize, true,
                          &output.namespace_id)
            || !CopyBytes(input.visible_source_root,
                          PATHGUARD_PROVIDER_ROUTE_PATH_MAX, true,
                          &output.visible_source_root)
            || !CopyBytes(input.backing_target_root,
                          PATHGUARD_PROVIDER_ROUTE_PATH_MAX, true,
                          &output.backing_target_root)
            || !ValidateProviderRouteBinding(output).ready()) {
            return false;
        }
        return true;
    }
    if (input.object_type == 0 || input.namespace_id.size != 0
        || input.visible_source_root.size != 0
        || input.backing_target_root.size != 0) {
        return false;
    }
    if (input.reverse_record_id == 0) return true;
    if ((input.identity_kind != PATHGUARD_PROVIDER_ROUTE_IDENTITY_FILE_HANDLE
         && input.identity_kind
             != PATHGUARD_PROVIDER_ROUTE_IDENTITY_STATX_BIRTH_TIME)
        || input.identity_epoch == 0 || input.commit_sequence == 0
        || input.created_plan_generation != input.plan_generation
        || input.bound_plan_generation != input.plan_generation
        || !CopyBytes(input.identity_volume,
                      PATHGUARD_PROVIDER_ROUTE_VOLUME_MAX, true,
                      &output.fd_identity.volume)
        || !CopyBytes(input.storage_root_id,
                      PATHGUARD_PROVIDER_ROUTE_VOLUME_MAX, true,
                      &output.reverse_record.key.storage_root_id)
        || !CopyBytes(input.target_relative_path,
                      PATHGUARD_PROVIDER_ROUTE_PATH_MAX, true,
                      &output.reverse_record.key.target_relative_path)) {
        return false;
    }
    output.fd_identity.kind = input.identity_kind
            == PATHGUARD_PROVIDER_ROUTE_IDENTITY_FILE_HANDLE
        ? provenance::IdentityKind::kFileHandle
        : provenance::IdentityKind::kStatxBirthTime;
    output.fd_identity.handle_type = input.identity_handle_type;
    output.fd_identity.inode = input.inode;
    output.fd_identity.birth_seconds = input.birth_seconds;
    output.fd_identity.birth_nanoseconds = input.birth_nanoseconds;
    output.fd_identity.object_type = input.object_type;
    if (input.identity_handle.size
            > PATHGUARD_PROVIDER_ROUTE_IDENTITY_HANDLE_MAX
        || (input.identity_handle.size != 0
            && input.identity_handle.data == nullptr)) {
        return false;
    }
    if (input.identity_handle.size != 0) {
        output.fd_identity.handle.assign(
            input.identity_handle.data,
            input.identity_handle.data + input.identity_handle.size);
    }
    if (!output.fd_identity.Strong()) return false;
    output.reverse_record.scope = {
        input.caller_uid, input.user_id, input.identity_epoch, {},
    };
    output.reverse_record.identity = output.fd_identity;
    output.reverse_record.logical_source_path = output.visible_source_path;
    output.reverse_record.rule_id = input.rule_id;
    output.reverse_record.content_generation = input.content_generation;
    output.reverse_record.created_plan_generation =
        input.created_plan_generation;
    output.reverse_record.bound_plan_generation = input.bound_plan_generation;
    output.reverse_record.commit_sequence = input.commit_sequence;
    if (!TargetMatchesRouteKey(output.backing_target_path,
                               output.reverse_record.key)) {
        return false;
    }
    reverse->reverse_record_id = input.reverse_record_id;
    reverse->result = {
        provenance::ResolveStatus::kUnique,
        provenance::Error::kNone,
        output.reverse_record,
    };
    return true;
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

std::unique_ptr<ProviderRouteSnapshotRegistryV1>
DecodeProviderRouteSnapshotV1(
        const PathGuardProviderRouteSnapshotV1& snapshot) noexcept {
    if (snapshot.version != PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_VERSION
        || snapshot.size < sizeof(PathGuardProviderRouteSnapshotV1)
        || snapshot.generation == 0
        || snapshot.binding_count
            > PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_MAX_BINDINGS
        || (snapshot.binding_count != 0 && snapshot.bindings == nullptr)) {
        return nullptr;
    }
    try {
        std::vector<ProviderRouteSnapshotBindingV1> bindings;
        std::vector<ProviderRouteSnapshotReverseV1> reverse_records;
        bindings.reserve(snapshot.binding_count);
        reverse_records.reserve(snapshot.binding_count);
        for (std::uint32_t index = 0; index < snapshot.binding_count; ++index) {
            ProviderRouteSnapshotBindingV1 binding;
            ProviderRouteSnapshotReverseV1 reverse;
            if (!DecodeBinding(snapshot.bindings[index], &binding, &reverse)) {
                return nullptr;
            }
            bindings.push_back(std::move(binding));
            if (reverse.reverse_record_id != 0) {
                reverse_records.push_back(std::move(reverse));
            }
        }
        auto registry = std::make_unique<ProviderRouteSnapshotRegistryV1>(
            snapshot.generation, std::move(bindings),
            std::move(reverse_records));
        return registry->ready() ? std::move(registry) : nullptr;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

}  // namespace pathguard
