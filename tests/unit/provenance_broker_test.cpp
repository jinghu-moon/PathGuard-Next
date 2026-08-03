#include <cstring>

#include "pathguard/provenance_broker.h"
#include "test_assert.h"

namespace {

using namespace pathguard;

provenance_protocol::Record Record(int uid, const char* source,
                                  const char* target) {
    provenance_protocol::Record output;
    output.caller_uid = uid;
    output.user_id = 0;
    output.identity_epoch = 7;
    output.rule_id = 41;
    output.content_generation = 2;
    output.created_plan_generation = 3;
    output.bound_plan_generation = 3;
    std::strcpy(output.storage_root, "primary");
    std::strcpy(output.logical_source, source);
    std::strcpy(output.target_relative, target);
    return output;
}

void Materialize(provenance_protocol::Record* record, std::uint64_t inode) {
    record->identity.kind =
        provenance_protocol::IdentityKind::kStatxBirthTime;
    record->identity.inode = inode;
    record->identity.birth_seconds = 100;
    record->identity.birth_nanoseconds = 5;
    record->identity.object_type = 1;
    std::strcpy(record->identity.volume, "dev:0:42");
}

}  // namespace

int main() {
    using namespace pathguard;
    using namespace pathguard::provenance;
    MemoryRouteJournal journal;
    RouteProvenanceStore store(&journal);
    assert(store.Recover() == Error::kNone);
    ProvenanceBroker broker(&store);
    provenance_protocol::Request request;
    request.command = provenance_protocol::Command::kPrepareCreate;
    request.transaction_high = 1;
    request.transaction_low = 2;
    request.record = Record(10358, "Pictures/a.jpg", "Download/redirect/a.jpg");
    provenance_protocol::Response response;
    assert(broker.Handle(request, &response));
    assert(response.provenance_error == provenance_protocol::Error::kNone);

    request.command = provenance_protocol::Command::kMaterialize;
    Materialize(&request.record, 90);
    assert(broker.Handle(request, &response));
    request.command = provenance_protocol::Command::kCommit;
    assert(broker.Handle(request, &response));
    assert(store.committed_count() == 1);

    request = {};
    request.command = provenance_protocol::Command::kSnapshotInfo;
    assert(broker.Handle(request, &response));
    assert(response.provenance_error == provenance_protocol::Error::kNone);
    assert(response.snapshot_generation != 0);
    assert(response.snapshot_count == 1);
    const auto snapshot_generation = response.snapshot_generation;

    request = {};
    request.command = provenance_protocol::Command::kSnapshotRecord;
    request.transaction_low = 0;
    assert(broker.Handle(request, &response));
    assert(response.snapshot_generation == snapshot_generation);
    assert(response.snapshot_count == 1);
    assert(response.snapshot_index == 0);
    assert(response.record.commit_sequence != 0);
    assert(response.record.identity.inode == request.record.identity.inode
           || response.record.identity.inode != 0);
    const auto committed_record = response.record;

    request = {};
    request.command = provenance_protocol::Command::kBindExternalIdentity;
    request.transaction_high = 8;
    request.transaction_low = 9;
    request.record = committed_record;
    std::strcpy(request.record.provider_uri,
                "content://media/external/images/media/90");
    std::strcpy(request.record.stable_document_id, "image:90");
    assert(broker.Handle(request, &response));
    assert(response.provenance_error == provenance_protocol::Error::kNone);

    request = {};
    request.command = provenance_protocol::Command::kSnapshotRecord;
    assert(broker.Handle(request, &response));
    assert(std::strcmp(response.record.provider_uri,
                       "content://media/external/images/media/90") == 0);
    assert(std::strcmp(response.record.stable_document_id, "image:90") == 0);

    request = {};
    request.record = Record(10358, "Pictures/a.jpg", "Download/redirect/a.jpg");
    Materialize(&request.record, 90);
    request.command = provenance_protocol::Command::kResolve;
    request.transaction_high = request.transaction_low = 0;
    assert(broker.Handle(request, &response));
    assert(response.resolve_status == provenance_protocol::ResolveStatus::kUnique);
    assert(std::strcmp(response.logical_source, "Pictures/a.jpg") == 0);

    request = {};
    request.command = provenance_protocol::Command::kPrepareCreate;
    request.transaction_high = 10;
    request.transaction_low = 11;
    request.record = Record(
        10358, "Pictures/b.jpg", "Download/redirect/b.jpg");
    assert(broker.Handle(request, &response));
    request.command = provenance_protocol::Command::kMaterialize;
    request.record.identity.kind =
        provenance_protocol::IdentityKind::kFileHandle;
    request.record.identity.handle_type = 7;
    request.record.identity.handle_size = 4;
    request.record.identity.object_type = 1;
    std::memcpy(request.record.identity.handle, "\x01\x02\x03\x04", 4);
    std::strcpy(request.record.identity.volume, "dev:259:19");
    assert(broker.Handle(request, &response));
    request.command = provenance_protocol::Command::kCommit;
    assert(broker.Handle(request, &response));
    request = {};
    request.command = provenance_protocol::Command::kSnapshotRecord;
    request.transaction_low = 1;
    assert(broker.Handle(request, &response));
    assert(response.record.identity.kind
           == provenance_protocol::IdentityKind::kFileHandle);
    assert(response.record.identity.handle_type == 7);
    assert(response.record.identity.handle_size == 4);
    assert(std::memcmp(response.record.identity.handle,
                       "\x01\x02\x03\x04", 4) == 0);

    auto same_target_other_uid = request;
    same_target_other_uid.command = provenance_protocol::Command::kPrepareCreate;
    same_target_other_uid.transaction_high = 3;
    same_target_other_uid.transaction_low = 4;
    same_target_other_uid.record = Record(
        10002, "DCIM/a.jpg", "Download/redirect/a.jpg");
    assert(broker.Handle(same_target_other_uid, &response));
    assert(response.provenance_error
           == provenance_protocol::Error::kRouteBusy);

    auto malformed = request;
    malformed.magic = 0;
    assert(!broker.Handle(malformed, &response));
    malformed = request;
    malformed.command = provenance_protocol::Command::kResolve;
    malformed.record = Record(
        10358, "Pictures/a.jpg", "Download/redirect/a.jpg");
    Materialize(&malformed.record, 90);
    std::memset(malformed.record.logical_source, 'x',
                sizeof(malformed.record.logical_source));
    assert(!broker.Handle(malformed, &response));
    malformed = request;
    malformed.command = provenance_protocol::Command::kMaterialize;
    malformed.record.identity.kind =
        provenance_protocol::IdentityKind::kFileHandle;
    malformed.record.identity.handle_size =
        provenance_protocol::kIdentityHandleCapacity + 1;
    assert(!broker.Handle(malformed, &response));
    return 0;
}
