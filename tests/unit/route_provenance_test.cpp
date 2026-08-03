#include <filesystem>
#include <fstream>

#include "pathguard/route_provenance.h"
#include "test_assert.h"

namespace {
pathguard::provenance::ObjectIdentity Identity(std::uint8_t value) {
    pathguard::provenance::ObjectIdentity result;
    result.volume = "primary";
    result.handle = {value, static_cast<std::uint8_t>(value + 1)};
    result.handle_type = 7;
    return result;
}
pathguard::provenance::RouteRecord Record(const char* source, const char* target,
                                          std::uint8_t identity) {
    pathguard::provenance::RouteRecord result;
    result.key = {"primary", target};
    result.scope = {10358, 0, 7, "org.localsend.localsend_app"};
    result.identity = Identity(identity);
    result.logical_source_path = source;
    result.rule_id = 41;
    result.content_generation = 2;
    result.created_plan_generation = 3;
    result.bound_plan_generation = 3;
    return result;
}
}

int main() {
    using namespace pathguard::provenance;
    MemoryRouteJournal journal;
    RouteProvenanceStore store(&journal);
    assert(store.Recover() == Error::kNone);
    const TransactionId tx{1, 2};
    auto record = Record("Pictures/a.jpg", "Download/redirect/a.jpg", 4);
    assert(store.Prepare(tx, Operation::kCreate, record) == Error::kNone);
    assert(store.Prepare({1, 3}, Operation::kCreate, record) == Error::kRouteBusy);
    assert(store.Materialize(tx, record.identity) == Error::kNone);
    assert(store.Commit(tx) == Error::kNone);
    assert(store.Commit(tx) == Error::kNone);
    assert(store.Abort(tx) == Error::kTransactionConflict);
    assert(store.committed_count() == 1);

    auto committed = store.ResolveReverse(
        record.scope, record.key, record.identity, 3);
    assert(committed.status == ResolveStatus::kUnique);
    assert(store.BindExternalIdentity(
               {1, 4}, record.scope, record.key, record.identity, 3,
               "content://media/external/images/media/42", "image:42")
           == Error::kNone);
    committed = store.ResolveReverse(
        record.scope, record.key, record.identity, 3);
    assert(committed.record->provider_uri
           == "content://media/external/images/media/42");
    assert(committed.record->stable_document_id == "image:42");
    assert(store.BindExternalIdentity(
               {1, 5}, record.scope, record.key, Identity(99), 3,
               "content://media/external/images/media/43", "image:43")
           == Error::kIdentityMismatch);

    auto other_scope = record;
    other_scope.scope.caller_uid = 10002;
    other_scope.logical_source_path = "DCIM/a.jpg";
    other_scope.identity = Identity(12);
    assert(store.Prepare({9, 9}, Operation::kCreate, other_scope)
           == Error::kRouteBusy);
    assert(store.committed_count() == 1);
    auto reverse = store.ResolveReverse(record.scope, record.key, record.identity, 3);
    assert(reverse.status == ResolveStatus::kUnique);
    reverse = store.ResolveReverse(record.scope, record.key, Identity(9), 3);
    assert(reverse.status == ResolveStatus::kAmbiguous);

    RouteProvenanceStore replayed(&journal);
    assert(replayed.Recover() == Error::kNone);
    assert(replayed.committed_count() == 1);
    auto replayed_record = replayed.ResolveReverse(
        record.scope, record.key, record.identity, 3);
    assert(replayed_record.record->provider_uri
           == "content://media/external/images/media/42");
    assert(replayed_record.record->identity.handle_type == 7);

    MemoryRouteJournal abandoned_journal;
    RouteProvenanceStore abandoned_writer(&abandoned_journal);
    auto abandoned = Record(
        "Pictures/abandoned.jpg", "Download/redirect/abandoned.jpg", 13);
    assert(abandoned_writer.Prepare({11, 12}, Operation::kCreate, abandoned)
           == Error::kNone);
    assert(abandoned_writer.Materialize({11, 12}, abandoned.identity)
           == Error::kNone);
    assert(abandoned_writer.pending_count() == 1);
    RouteProvenanceStore abandoned_recovered(&abandoned_journal);
    assert(abandoned_recovered.Recover() == Error::kNone);
    assert(abandoned_recovered.pending_count() == 0);
    assert(abandoned_recovered.committed_count() == 0);
    assert(abandoned_recovered.Prepare(
               {11, 13}, Operation::kCreate, abandoned) == Error::kNone);

    MemoryRouteJournal failure_journal;
    RouteProvenanceStore failure_store(&failure_journal);
    ProvenanceCoordinator coordinator(&failure_store);
    failure_journal.FailNextAppend(Error::kUnavailable);
    bool created = false;
    auto result = coordinator.Create({3, 4}, Record("Pictures/b.jpg", "Download/redirect/b.jpg", 5),
        [&] { created = true; return 0; }, [] { return std::optional{Identity(5)}; },
        [](const ObjectIdentity&) { return true; });
    assert(!created);
    assert(result.provenance_error == Error::kUnavailable);

    result = coordinator.Create({5, 6}, Record("Pictures/c.jpg", "Download/redirect/c.jpg", 6),
        [] { return 0; }, [] { return std::optional{Identity(6)}; },
        [](const ObjectIdentity&) { return true; });
    assert(result.committed);

    auto source = Record("Pictures/source.jpg", "Download/redirect/source.jpg", 10);
    assert(failure_store.Prepare({10, 1}, Operation::kCreate, source) == Error::kNone);
    assert(failure_store.Materialize({10, 1}, source.identity) == Error::kNone);
    assert(failure_store.Commit({10, 1}) == Error::kNone);
    const auto owned_source = failure_store.ResolveReverse(
        source.scope, source.key, source.identity, 3);
    assert(owned_source.status == ResolveStatus::kUnique);
    source = *owned_source.record;
    auto destination = source;
    destination.key.target_relative_path = "Download/redirect/moved.jpg";
    destination.logical_source_path = "Pictures/moved.jpg";
    result = coordinator.Rename({10, 2}, source, destination,
        [] { return 0; }, [] { return std::optional{Identity(10)}; },
        [](const ObjectIdentity&) { return true; });
    assert(result.committed);
    assert(failure_store.ResolveReverse(source.scope, source.key, source.identity, 3).status
           == ResolveStatus::kNone);
    const auto owned_destination = failure_store.ResolveReverse(
        destination.scope, destination.key, destination.identity, 3);
    assert(owned_destination.status == ResolveStatus::kUnique);
    destination = *owned_destination.record;
    bool degraded = false;
    result = coordinator.Delete({10, 3}, destination, [] { return 0; },
        [&] { degraded = true; });
    assert(result.committed && !degraded);
    assert(failure_store.ResolveReverse(destination.scope, destination.key,
                                        destination.identity, 3).status
           == ResolveStatus::kNone);

    const auto file = std::filesystem::temp_directory_path() / "pathguard-provenance-test.wal";
    std::filesystem::remove(file);
    FileRouteJournal file_journal(file.string());
    RouteProvenanceStore file_store(&file_journal);
    auto persisted = Record("Download/a.txt", "Vault/a.txt", 8);
    assert(file_store.Prepare({7, 8}, Operation::kCreate, persisted) == Error::kNone);
    assert(file_store.Materialize({7, 8}, persisted.identity) == Error::kNone);
    assert(file_store.Commit({7, 8}) == Error::kNone);
    RouteProvenanceStore reopened(&file_journal);
    assert(reopened.Recover() == Error::kNone);
    assert(reopened.committed_count() == 1);
    const auto valid_size = std::filesystem::file_size(file);
    {
        std::ofstream torn(file, std::ios::binary | std::ios::app);
        torn.write("torn", 4);
    }
    RouteProvenanceStore repaired(&file_journal);
    assert(repaired.Recover() == Error::kNone);
    assert(repaired.committed_count() == 1);
    assert(std::filesystem::file_size(file) == valid_size);
    std::filesystem::remove(file);
    return 0;
}
