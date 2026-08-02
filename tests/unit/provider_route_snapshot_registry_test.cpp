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
    return 0;
}
