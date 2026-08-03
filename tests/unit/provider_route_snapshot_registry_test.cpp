#include "pathguard/provider_route_snapshot_registry.h"
#include "test_assert.h"

namespace {

pathguard::ProviderRouteBindingV1 CompleteBinding() {
    pathguard::ProviderRouteBindingV1 binding;
    binding.context = {10437, 0, 1, 7, 41, 2, 3};
    binding.visible_source_path = "/storage/emulated/0/Pictures/source/a.jpg";
    binding.backing_target_path = "/storage/emulated/0/Pictures/target/a.jpg";
    binding.provider_uri = "content://media/external_primary/images/media/42";
    binding.stable_document_id = "primary:Pictures/source/a.jpg";
    binding.fd_identity.kind = pathguard::provenance::IdentityKind::kFileHandle;
    binding.fd_identity.volume = "emulated:0";
    binding.fd_identity.handle = {1, 2, 3, 4};
    binding.fd_identity.handle_type = 1;
    binding.reverse_record.scope.caller_uid = binding.context.caller_uid;
    binding.reverse_record.scope.user_id = binding.context.user_id;
    binding.reverse_record.scope.identity_epoch = 9;
    binding.reverse_record.key.storage_root_id = "emulated:0";
    binding.reverse_record.key.target_relative_path = "Pictures/target/a.jpg";
    binding.reverse_record.identity = binding.fd_identity;
    binding.reverse_record.logical_source_path = binding.visible_source_path;
    binding.reverse_record.rule_id = binding.context.rule_id;
    binding.reverse_record.created_plan_generation = binding.context.plan_generation;
    binding.reverse_record.bound_plan_generation = binding.context.plan_generation;
    binding.reverse_record.commit_sequence = 1;
    return binding;
}

}  // namespace

int main() {
    using namespace pathguard;

    const auto binding = CompleteBinding();
    ProviderRouteSnapshotBindingV1 entry{41, binding};
    ProviderRouteSnapshotReverseV1 reverse{
        97,
        {provenance::ResolveStatus::kUnique, provenance::Error::kNone,
         binding.reverse_record},
    };
    ProviderRouteSnapshotRegistryV1 registry(7, {entry}, {reverse});
    assert(registry.ready());
    assert(registry.generation() == 7);

    const auto forward = registry.Lookup(7, 41, 0);
    assert(forward.resolved());
    assert(forward.binding->stable_document_id == binding.stable_document_id);
    assert(forward.reverse == nullptr);

    const auto reverse_lookup = registry.Lookup(7, 41, 97);
    assert(reverse_lookup.resolved());
    assert(reverse_lookup.reverse != nullptr);
    assert(reverse_lookup.reverse->record == binding.reverse_record);

    assert(!registry.Lookup(6, 41, 97).resolved());
    assert(!registry.Lookup(7, 0, 97).resolved());
    assert(!registry.Lookup(7, 42, 97).resolved());
    assert(!registry.Lookup(7, 41, 98).resolved());

    ProviderRouteSnapshotRegistryV1 zero_generation(0, {entry}, {reverse});
    assert(!zero_generation.ready());
    ProviderRouteSnapshotRegistryV1 duplicate_binding(7, {entry, entry}, {reverse});
    assert(!duplicate_binding.ready());
    ProviderRouteSnapshotReverseV1 duplicate_reverse = reverse;
    ProviderRouteSnapshotRegistryV1 duplicate_reverse_id(
        7, {entry}, {reverse, duplicate_reverse});
    assert(!duplicate_reverse_id.ready());

    const std::string visible = binding.visible_source_path;
    const std::string backing = binding.backing_target_path;
    const std::string uri = binding.provider_uri;
    const std::string document_id = binding.stable_document_id;
    const std::string volume = binding.fd_identity.volume;
    const std::string storage_root =
        binding.reverse_record.key.storage_root_id;
    const std::string relative =
        binding.reverse_record.key.target_relative_path;
    PathGuardProviderRouteBindingV1 wire{};
    wire.version = PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_VERSION;
    wire.size = sizeof(wire);
    wire.reverse_mode = PATHGUARD_PROVIDER_ROUTE_REVERSE_PROVENANCE;
    wire.identity_kind = PATHGUARD_PROVIDER_ROUTE_IDENTITY_FILE_HANDLE;
    wire.object_type = 1;
    wire.identity_handle_type = binding.fd_identity.handle_type;
    wire.binding_id = 41;
    wire.reverse_record_id = 97;
    wire.caller_uid = binding.context.caller_uid;
    wire.user_id = binding.context.user_id;
    wire.package_index = binding.context.package_index;
    wire.plan_generation = binding.context.plan_generation;
    wire.rule_id = binding.context.rule_id;
    wire.identity_epoch = binding.reverse_record.scope.identity_epoch;
    wire.content_generation = binding.reverse_record.content_generation;
    wire.created_plan_generation = binding.context.plan_generation;
    wire.bound_plan_generation = binding.context.plan_generation;
    wire.commit_sequence = binding.reverse_record.commit_sequence;
    const auto bytes = [](const auto& value) {
        return PathGuardProviderRouteBytesV1{
            reinterpret_cast<const std::uint8_t*>(value.data()),
            static_cast<std::uint32_t>(value.size()),
        };
    };
    wire.visible_source_path = bytes(visible);
    wire.backing_target_path = bytes(backing);
    wire.provider_uri = bytes(uri);
    wire.stable_document_id = bytes(document_id);
    wire.identity_volume = bytes(volume);
    wire.identity_handle = {
        binding.fd_identity.handle.data(),
        static_cast<std::uint32_t>(binding.fd_identity.handle.size()),
    };
    wire.storage_root_id = bytes(storage_root);
    wire.target_relative_path = bytes(relative);
    PathGuardProviderRouteSnapshotV1 wire_snapshot{
        PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_VERSION,
        sizeof(PathGuardProviderRouteSnapshotV1), 1, 7, &wire,
    };
    auto decoded = DecodeProviderRouteSnapshotV1(wire_snapshot);
    assert(decoded != nullptr && decoded->ready());
    const auto decoded_lookup = decoded->Lookup(7, 41, 97);
    assert(decoded_lookup.resolved());
    assert(decoded_lookup.binding->visible_source_path == visible);
    assert(decoded_lookup.reverse != nullptr);
    assert(decoded_lookup.reverse->record == binding.reverse_record);

    wire.identity_handle.size =
        PATHGUARD_PROVIDER_ROUTE_IDENTITY_HANDLE_MAX + 1;
    assert(DecodeProviderRouteSnapshotV1(wire_snapshot) == nullptr);
    wire.identity_handle.size =
        static_cast<std::uint32_t>(binding.fd_identity.handle.size());
    wire_snapshot.generation = 0;
    assert(DecodeProviderRouteSnapshotV1(wire_snapshot) == nullptr);

    const auto static_binding = [] {
        ProviderRouteBindingV1 value;
        value.context = {10437, 0, 1, 7, 43, 0, 0};
        value.reverse_mode = ProviderRouteReverseMode::kStaticUnique;
        value.namespace_id = "abcdefghijklmnopqrstuvwxyz";
        value.visible_source_path =
            "/storage/emulated/0/Pictures/source";
        value.backing_target_path =
            "/storage/emulated/0/Download/images/_pg/v1/"
            "ns_abcdefghijklmnopqrstuvwxyz";
        value.visible_source_root = value.visible_source_path;
        value.backing_target_root = value.backing_target_path;
        value.stable_document_id = "primary:Pictures/source";
        return value;
    }();
    PathGuardProviderRouteBindingV1 static_wire{};
    static_wire.version = PATHGUARD_PROVIDER_ROUTE_SNAPSHOT_VERSION;
    static_wire.size = sizeof(static_wire);
    static_wire.reverse_mode =
        PATHGUARD_PROVIDER_ROUTE_REVERSE_STATIC_UNIQUE;
    static_wire.binding_id = 43;
    static_wire.caller_uid = static_binding.context.caller_uid;
    static_wire.user_id = static_binding.context.user_id;
    static_wire.package_index = static_binding.context.package_index;
    static_wire.plan_generation = static_binding.context.plan_generation;
    static_wire.rule_id = static_binding.context.rule_id;
    static_wire.visible_source_path = bytes(
        static_binding.visible_source_path);
    static_wire.backing_target_path = bytes(
        static_binding.backing_target_path);
    static_wire.stable_document_id = bytes(
        static_binding.stable_document_id);
    static_wire.namespace_id = bytes(static_binding.namespace_id);
    static_wire.visible_source_root = bytes(
        static_binding.visible_source_root);
    static_wire.backing_target_root = bytes(
        static_binding.backing_target_root);
    wire_snapshot.generation = 7;
    wire_snapshot.bindings = &static_wire;
    auto static_decoded = DecodeProviderRouteSnapshotV1(wire_snapshot);
    assert(static_decoded != nullptr && static_decoded->ready());
    const auto static_lookup = static_decoded->Lookup(7, 43, 0);
    assert(static_lookup.resolved());
    assert(static_lookup.reverse == nullptr);
    assert(static_lookup.binding->reverse_mode
           == ProviderRouteReverseMode::kStaticUnique);
    assert(static_lookup.binding->namespace_id
           == static_binding.namespace_id);
    return 0;
}
