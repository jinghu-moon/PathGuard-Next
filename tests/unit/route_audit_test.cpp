#include <cstring>
#include <filesystem>
#include <fstream>

#include "pathguard/audit_broker.h"
#include "pathguard/route_audit.h"
#include "test_assert.h"

namespace {

class AmbiguousJournal final : public pathguard::audit::Journal {
public:
    pathguard::audit::Error Append(
            const pathguard::audit::Record& record) override {
        records_.push_back(record);
        if (fail_after_append_) {
            fail_after_append_ = false;
            return pathguard::audit::Error::kCommitFailed;
        }
        return pathguard::audit::Error::kNone;
    }

    pathguard::audit::Error Replay(
            std::vector<pathguard::audit::Record>* records) override {
        if (records == nullptr) return pathguard::audit::Error::kCorrupt;
        if (fail_replay_) return pathguard::audit::Error::kCorrupt;
        *records = records_;
        return pathguard::audit::Error::kNone;
    }

    void FailAfterAppend() noexcept { fail_after_append_ = true; }
    void FailReplay() noexcept { fail_replay_ = true; }

private:
    bool fail_after_append_ = false;
    bool fail_replay_ = false;
    std::vector<pathguard::audit::Record> records_;
};

pathguard::audit::Record MakeRecord(
        pathguard::audit::Operation operation, const char* source,
        const char* target, const char* previous = "") {
    pathguard::audit::Record record;
    record.operation = operation;
    record.identity_phase = pathguard::audit::IdentityPhase::kInitial;
    record.caller_uid = 10358;
    record.user_id = 0;
    record.rule_id = 41;
    record.content_generation = 2;
    record.plan_generation = 3;
    record.observed_realtime_ns = 100;
    record.observed_boottime_ns = 50;
    record.logical_source_path = source;
    record.target_path = target;
    record.previous_target_path = previous;
    record.identity.device = 9;
    record.identity.inode = 10;
    record.identity.size = 11;
    record.identity.mode = 0100644;
    record.identity.modified_seconds = 20;
    record.identity.changed_seconds = 21;
    record.confidence = record.identity.confidence();
    return record;
}

pathguard::audit_protocol::Request Wire(
        pathguard::audit_protocol::Operation operation,
        const char* source, const char* target) {
    pathguard::audit_protocol::Request request;
    request.command = pathguard::audit_protocol::Command::kObserve;
    request.record.caller_uid = 10358;
    request.record.user_id = 0;
    request.record.rule_id = 42;
    request.record.content_generation = 2;
    request.record.plan_generation = 3;
    request.record.observed_realtime_ns = 1000;
    request.record.observed_boottime_ns = 900;
    request.record.operation = operation;
    request.record.identity_phase =
        pathguard::audit_protocol::IdentityPhase::kInitial;
    request.record.confidence =
        pathguard::audit_protocol::Confidence::kInodeMetadata;
    request.record.identity.device = 1;
    request.record.identity.inode = 2;
    std::strcpy(request.record.logical_source, source);
    std::strcpy(request.record.target_path, target);
    return request;
}

pathguard::audit_protocol::Request SettleWire(
        const char* target, std::uint64_t inode, std::uint64_t size) {
    pathguard::audit_protocol::Request request;
    request.command = pathguard::audit_protocol::Command::kSettle;
    request.record.caller_uid = 10358;
    request.record.observed_realtime_ns = 1100;
    request.record.observed_boottime_ns = 1000;
    request.record.operation = pathguard::audit_protocol::Operation::kUpsert;
    request.record.identity_phase =
        pathguard::audit_protocol::IdentityPhase::kSettled;
    request.record.confidence =
        pathguard::audit_protocol::Confidence::kInodeMetadata;
    request.record.identity.device = 1;
    request.record.identity.inode = inode;
    request.record.identity.size = size;
    request.record.identity.mode = 0100644;
    std::strcpy(request.record.target_path, target);
    return request;
}

}  // namespace

int main() {
    using namespace pathguard::audit;
    MemoryJournal journal;
    Store store(&journal);
    assert(store.Recover() == Error::kNone);

    auto created = MakeRecord(Operation::kUpsert,
                          "/storage/emulated/0/Pictures/a.jpg",
                          "/storage/emulated/0/Download/redirect/a.jpg");
    assert(created.confidence == Confidence::kInodeMetadata);
    assert(store.Observe(created) == Error::kNone);
    assert(store.current_count() == 1);
    assert(store.Find(created.target_path)->logical_source_path
           == created.logical_source_path);
    assert(store.Find(created.target_path)->identity_phase
           == IdentityPhase::kInitial);

    MemoryJournal settle_journal;
    Store settle_store(&settle_journal);
    assert(settle_store.Recover() == Error::kNone);
    assert(settle_store.Observe(created) == Error::kNone);
    auto final_identity = created.identity;
    final_identity.size = 4096;
    assert(settle_store.Settle(
               created.target_path, created.caller_uid, 200, 100,
               final_identity) == Error::kNone);
    assert(settle_store.Find(created.target_path)->identity_phase
           == IdentityPhase::kSettled);
    assert(settle_store.Find(created.target_path)->identity.size == 4096);
    auto stale_initial = created;
    stale_initial.observed_realtime_ns = 150;
    stale_initial.observed_boottime_ns = 75;
    assert(settle_store.Observe(stale_initial) == Error::kNone);
    assert(settle_store.Find(created.target_path)->identity_phase
           == IdentityPhase::kSettled);
    assert(settle_store.Find(created.target_path)->identity.size == 4096);
    Store settle_replayed(&settle_journal);
    assert(settle_replayed.Recover() == Error::kNone);
    assert(settle_replayed.Find(created.target_path)->identity_phase
           == IdentityPhase::kSettled);
    assert(settle_replayed.Find(created.target_path)->identity.size == 4096);
    auto wrong_identity = final_identity;
    wrong_identity.inode = 99;
    assert(settle_store.Settle(
               created.target_path, created.caller_uid, 300, 200,
               wrong_identity) == Error::kNone);
    assert(settle_store.Find(created.target_path)->identity.inode
           == created.identity.inode);
    assert(settle_store.Settle(
               "/storage/emulated/0/Download/redirect/missing.jpg",
               created.caller_uid, 300, 200, final_identity) == Error::kNone);
    assert(settle_store.current_count() == 1);
    ObjectIdentity path_only;
    assert(settle_store.Settle(
               created.target_path, created.caller_uid, 300, 200,
               path_only) == Error::kInvalidRecord);

    auto renamed = MakeRecord(Operation::kRename,
                          "/storage/emulated/0/Pictures/b.jpg",
                          "/storage/emulated/0/Download/redirect/b.jpg",
                          created.target_path.c_str());
    assert(store.Observe(renamed) == Error::kNone);
    assert(!store.Find(created.target_path).has_value());
    assert(store.Find(renamed.target_path).has_value());

    journal.FailNextAppend(Error::kUnavailable);
    auto failed = MakeRecord(Operation::kUpsert,
                         "/storage/emulated/0/Pictures/failed.jpg",
                         "/storage/emulated/0/Download/redirect/failed.jpg");
    assert(store.Observe(failed) == Error::kUnavailable);
    assert(!store.Find(failed.target_path).has_value());

    auto removed = MakeRecord(Operation::kDelete, renamed.logical_source_path.c_str(),
                          renamed.target_path.c_str());
    assert(store.Observe(removed) == Error::kNone);
    assert(store.current_count() == 0);

    Store replayed(&journal);
    assert(replayed.Recover() == Error::kNone);
    assert(replayed.current_count() == 0);

    AmbiguousJournal ambiguous_journal;
    Store ambiguous_store(&ambiguous_journal);
    assert(ambiguous_store.Recover() == Error::kNone);
    auto ambiguous_first = MakeRecord(
        Operation::kUpsert, "/storage/emulated/0/Pictures/ambiguous-a.jpg",
        "/storage/emulated/0/Download/redirect/ambiguous-a.jpg");
    ambiguous_journal.FailAfterAppend();
    assert(ambiguous_store.Observe(ambiguous_first) == Error::kCommitFailed);
    assert(ambiguous_store.Find(ambiguous_first.target_path).has_value());
    auto ambiguous_second = MakeRecord(
        Operation::kUpsert, "/storage/emulated/0/Pictures/ambiguous-b.jpg",
        "/storage/emulated/0/Download/redirect/ambiguous-b.jpg");
    assert(ambiguous_store.Observe(ambiguous_second) == Error::kNone);
    Store ambiguous_replayed(&ambiguous_journal);
    assert(ambiguous_replayed.Recover() == Error::kNone);
    assert(ambiguous_replayed.current_count() == 2);

    AmbiguousJournal poisoned_journal;
    Store poisoned_store(&poisoned_journal);
    assert(poisoned_store.Recover() == Error::kNone);
    poisoned_journal.FailAfterAppend();
    poisoned_journal.FailReplay();
    assert(poisoned_store.Observe(ambiguous_first) == Error::kCommitFailed);
    assert(poisoned_store.Observe(ambiguous_second) == Error::kUnavailable);

    MemoryJournal ordering_journal;
    Store ordering_store(&ordering_journal);
    assert(ordering_store.Recover() == Error::kNone);
    auto deleted_original = MakeRecord(
        Operation::kUpsert, "/storage/emulated/0/Pictures/deleted.jpg",
        "/storage/emulated/0/Download/redirect/deleted.jpg");
    deleted_original.observed_realtime_ns = 100;
    deleted_original.observed_boottime_ns = 100;
    assert(ordering_store.Observe(deleted_original) == Error::kNone);
    auto deleted = MakeRecord(
        Operation::kDelete, deleted_original.logical_source_path.c_str(),
        deleted_original.target_path.c_str());
    deleted.observed_realtime_ns = 300;
    deleted.observed_boottime_ns = 300;
    assert(ordering_store.Observe(deleted) == Error::kNone);
    auto stale_after_delete = deleted_original;
    stale_after_delete.identity.inode = 77;
    stale_after_delete.observed_realtime_ns = 200;
    stale_after_delete.observed_boottime_ns = 200;
    assert(ordering_store.Observe(stale_after_delete) == Error::kNone);
    assert(!ordering_store.Find(deleted_original.target_path).has_value());

    auto rename_original = MakeRecord(
        Operation::kUpsert, "/storage/emulated/0/Pictures/rename-old.jpg",
        "/storage/emulated/0/Download/redirect/rename-old.jpg");
    rename_original.observed_realtime_ns = 100;
    rename_original.observed_boottime_ns = 100;
    assert(ordering_store.Observe(rename_original) == Error::kNone);
    auto ordered_rename = MakeRecord(
        Operation::kRename,
        "/storage/emulated/0/Pictures/rename-new.jpg",
        "/storage/emulated/0/Download/redirect/rename-new.jpg",
        rename_original.target_path.c_str());
    ordered_rename.observed_realtime_ns = 300;
    ordered_rename.observed_boottime_ns = 300;
    assert(ordering_store.Observe(ordered_rename) == Error::kNone);
    auto stale_after_rename = rename_original;
    stale_after_rename.observed_realtime_ns = 200;
    stale_after_rename.observed_boottime_ns = 200;
    assert(ordering_store.Observe(stale_after_rename) == Error::kNone);
    assert(!ordering_store.Find(rename_original.target_path).has_value());
    assert(ordering_store.Find(ordered_rename.target_path).has_value());

    auto newer_source = MakeRecord(
        Operation::kUpsert, "/storage/emulated/0/Pictures/newer-source.jpg",
        "/storage/emulated/0/Download/redirect/newer-source.jpg");
    newer_source.observed_realtime_ns = 500;
    newer_source.observed_boottime_ns = 500;
    assert(ordering_store.Observe(newer_source) == Error::kNone);
    auto stale_rename = MakeRecord(
        Operation::kRename,
        "/storage/emulated/0/Pictures/stale-target.jpg",
        "/storage/emulated/0/Download/redirect/stale-target.jpg",
        newer_source.target_path.c_str());
    stale_rename.observed_realtime_ns = 400;
    stale_rename.observed_boottime_ns = 400;
    assert(ordering_store.Observe(stale_rename) == Error::kNone);
    assert(ordering_store.Find(newer_source.target_path).has_value());
    assert(!ordering_store.Find(stale_rename.target_path).has_value());

    Store ordering_replayed(&ordering_journal);
    assert(ordering_replayed.Recover() == Error::kNone);
    assert(!ordering_replayed.Find(deleted_original.target_path).has_value());
    assert(!ordering_replayed.Find(rename_original.target_path).has_value());
    assert(ordering_replayed.Find(ordered_rename.target_path).has_value());
    assert(ordering_replayed.Find(newer_source.target_path).has_value());
    assert(!ordering_replayed.Find(stale_rename.target_path).has_value());

    MemoryJournal reboot_journal;
    Store before_reboot(&reboot_journal);
    auto before_reboot_record = MakeRecord(
        Operation::kUpsert, "/storage/emulated/0/Pictures/reboot.jpg",
        "/storage/emulated/0/Download/redirect/reboot.jpg");
    before_reboot_record.observed_boottime_ns = 1000;
    assert(before_reboot.Observe(before_reboot_record) == Error::kNone);
    Store after_reboot(&reboot_journal);
    assert(after_reboot.Recover() == Error::kNone);
    auto after_reboot_record = before_reboot_record;
    after_reboot_record.observed_realtime_ns = 200;
    after_reboot_record.observed_boottime_ns = 10;
    after_reboot_record.identity.inode = 99;
    after_reboot_record.confidence = after_reboot_record.identity.confidence();
    assert(after_reboot.Observe(after_reboot_record) == Error::kNone);
    assert(after_reboot.Find(after_reboot_record.target_path)->identity.inode == 99);

    MemoryJournal broker_journal;
    Store broker_store(&broker_journal);
    Broker broker(&broker_store);
    auto request = Wire(
        pathguard::audit_protocol::Operation::kUpsert,
        "/storage/emulated/0/Pictures/wire.jpg",
        "/storage/emulated/0/Download/redirect/wire.jpg");
    pathguard::audit_protocol::Response response;
    assert(broker.Handle(request, &response));
    assert(response.error == pathguard::audit_protocol::Error::kNone);
    request = {};
    request.command = pathguard::audit_protocol::Command::kSnapshotInfo;
    assert(broker.Handle(request, &response));
    assert(response.snapshot_count == 1);
    request.command = pathguard::audit_protocol::Command::kSnapshotRecord;
    request.snapshot_index = 0;
    assert(broker.Handle(request, &response));
    assert(std::strcmp(response.record.logical_source,
                       "/storage/emulated/0/Pictures/wire.jpg") == 0);
    assert(response.record.identity_phase
           == pathguard::audit_protocol::IdentityPhase::kInitial);

    auto settle_missing = SettleWire(
        "/storage/emulated/0/Download/redirect/missing-wire.jpg", 2, 100);
    assert(broker.Handle(settle_missing, &response));
    assert(response.error == pathguard::audit_protocol::Error::kNone);
    assert(response.snapshot_count == 1);
    auto settle_mismatch = SettleWire(
        "/storage/emulated/0/Download/redirect/wire.jpg", 99, 200);
    assert(broker.Handle(settle_mismatch, &response));
    assert(response.error == pathguard::audit_protocol::Error::kNone);
    assert(broker_store.Find(
               "/storage/emulated/0/Download/redirect/wire.jpg")
               ->identity_phase == IdentityPhase::kInitial);
    auto settle = SettleWire(
        "/storage/emulated/0/Download/redirect/wire.jpg", 2, 300);
    assert(broker.Handle(settle, &response));
    assert(response.error == pathguard::audit_protocol::Error::kNone);
    assert(broker_store.Find(
               "/storage/emulated/0/Download/redirect/wire.jpg")
               ->identity_phase == IdentityPhase::kSettled);
    assert(broker_store.Find(
               "/storage/emulated/0/Download/redirect/wire.jpg")
               ->identity.size == 300);

    auto malformed_settle = settle;
    malformed_settle.record.identity_phase =
        pathguard::audit_protocol::IdentityPhase::kInitial;
    assert(!broker.Handle(malformed_settle, &response));
    malformed_settle = settle;
    std::strcpy(malformed_settle.record.logical_source,
                "/storage/emulated/0/Pictures/wire.jpg");
    assert(!broker.Handle(malformed_settle, &response));
    malformed_settle = settle;
    std::strcpy(malformed_settle.record.target_path,
                "/mnt/user/0/emulated/0/Download/redirect/wire.jpg");
    assert(!broker.Handle(malformed_settle, &response));

    auto malformed = Wire(
        pathguard::audit_protocol::Operation::kUpsert,
        "/storage/emulated/0/Pictures/malformed.jpg",
        "/storage/emulated/0/Download/redirect/malformed.jpg");
    malformed.magic = 0;
    assert(!broker.Handle(malformed, &response));
    malformed = Wire(
        pathguard::audit_protocol::Operation::kUpsert,
        "/storage/emulated/0/Pictures/malformed.jpg",
        "/storage/emulated/0/Download/redirect/malformed.jpg");
    malformed.version = 0;
    assert(!broker.Handle(malformed, &response));
    malformed = Wire(
        pathguard::audit_protocol::Operation::kUpsert,
        "/storage/emulated/0/Pictures/malformed.jpg",
        "/storage/emulated/0/Download/redirect/malformed.jpg");
    malformed.reserved = 1;
    assert(!broker.Handle(malformed, &response));
    malformed = Wire(
        pathguard::audit_protocol::Operation::kUpsert,
        "/storage/emulated/0/Pictures/malformed.jpg",
        "/storage/emulated/0/Download/redirect/malformed.jpg");
    malformed.record.sequence = 1;
    assert(!broker.Handle(malformed, &response));
    malformed = Wire(
        pathguard::audit_protocol::Operation::kUpsert,
        "/storage/emulated/0/Pictures/malformed.jpg",
        "/storage/emulated/0/Download/redirect/malformed.jpg");
    malformed.record.identity_phase =
        static_cast<pathguard::audit_protocol::IdentityPhase>(0);
    assert(broker.Handle(malformed, &response));
    assert(response.error
           == pathguard::audit_protocol::Error::kInvalidRecord);
    malformed = Wire(
        pathguard::audit_protocol::Operation::kUpsert,
        "/storage/emulated/0/Pictures/malformed.jpg",
        "/storage/emulated/0/Download/redirect/malformed.jpg");
    malformed.record.identity.handle_size =
        pathguard::audit_protocol::kHandleCapacity + 1;
    assert(!broker.Handle(malformed, &response));
    malformed = Wire(
        pathguard::audit_protocol::Operation::kUpsert,
        "/storage/emulated/0/Pictures/malformed.jpg",
        "/storage/emulated/0/Download/redirect/malformed.jpg");
    malformed.record.reserved[0] = 1;
    assert(!broker.Handle(malformed, &response));

    const auto file = std::filesystem::temp_directory_path()
        / "pathguard-private-audit-test.wal";
    std::filesystem::remove(file);
    FileJournal file_journal(file.string());
    Store file_store(&file_journal);
    assert(file_store.Observe(created) == Error::kNone);
    const auto first_frame_size = std::filesystem::file_size(file);
    auto second_file_record = MakeRecord(
        Operation::kUpsert, "/storage/emulated/0/Pictures/file-b.jpg",
        "/storage/emulated/0/Download/redirect/file-b.jpg");
    assert(file_store.Observe(second_file_record) == Error::kNone);
    const auto two_frame_size = std::filesystem::file_size(file);
    assert(two_frame_size > first_frame_size);
    std::filesystem::resize_file(
        file, first_frame_size + (two_frame_size - first_frame_size) / 2);
    Store payload_repaired(&file_journal);
    assert(payload_repaired.Recover() == Error::kNone);
    assert(payload_repaired.current_count() == 1);
    assert(std::filesystem::file_size(file) == first_frame_size);

    {
        std::ofstream torn(file, std::ios::binary | std::ios::app);
        torn.write("torn", 4);
    }
    Store repaired(&file_journal);
    assert(repaired.Recover() == Error::kNone);
    assert(repaired.current_count() == 1);
    assert(std::filesystem::file_size(file) == first_frame_size);

    {
        std::fstream corrupt(file, std::ios::binary | std::ios::in
                                      | std::ios::out);
        assert(corrupt.good());
        corrupt.seekg(-1, std::ios::end);
        char value = 0;
        corrupt.read(&value, 1);
        value ^= 0x5a;
        corrupt.seekp(-1, std::ios::end);
        corrupt.write(&value, 1);
    }
    const auto corrupt_size = std::filesystem::file_size(file);
    Store corrupt_store(&file_journal);
    assert(corrupt_store.Recover() == Error::kCorrupt);
    assert(std::filesystem::file_size(file) == corrupt_size);
    std::filesystem::remove(file);
    return 0;
}
