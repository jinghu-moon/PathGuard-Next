#include <cstring>
#include <filesystem>
#include <fstream>

#include "pathguard/audit_broker.h"
#include "pathguard/route_audit.h"
#include "test_assert.h"

namespace {

pathguard::audit::Record MakeRecord(
        pathguard::audit::Operation operation, const char* source,
        const char* target, const char* previous = "") {
    pathguard::audit::Record record;
    record.operation = operation;
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
    request.record.confidence =
        pathguard::audit_protocol::Confidence::kInodeMetadata;
    request.record.identity.device = 1;
    request.record.identity.inode = 2;
    std::strcpy(request.record.logical_source, source);
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

    const auto file = std::filesystem::temp_directory_path()
        / "pathguard-private-audit-test.wal";
    std::filesystem::remove(file);
    FileJournal file_journal(file.string());
    Store file_store(&file_journal);
    assert(file_store.Observe(created) == Error::kNone);
    const auto valid_size = std::filesystem::file_size(file);
    {
        std::ofstream torn(file, std::ios::binary | std::ios::app);
        torn.write("torn", 4);
    }
    Store repaired(&file_journal);
    assert(repaired.Recover() == Error::kNone);
    assert(repaired.current_count() == 1);
    assert(std::filesystem::file_size(file) == valid_size);
    std::filesystem::remove(file);
    return 0;
}
