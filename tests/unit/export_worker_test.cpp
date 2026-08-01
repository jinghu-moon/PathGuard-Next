#include "pathguard/export_worker.h"
#include "test_assert.h"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <utility>

namespace {

namespace exporting = pathguard::exporting;

exporting::ExportTask FidTask(std::uint64_t generation,
                              std::string source,
                              std::string target,
                              std::uint64_t event_window = 1) {
    exporting::ExportTask task;
    task.key = exporting::TaskKey::Fid(
        11, 22, std::string("handle-") + source, generation,
        exporting::EventKind::kCloseWrite, event_window);
    task.source = std::move(source);
    task.target = std::move(target);
    return task;
}

exporting::ExportTask FallbackTask(std::uint64_t inode,
                                   std::uint64_t generation) {
    exporting::ExportTask task;
    task.key = exporting::TaskKey::ProbedStat(
        1, inode, inode * 10, generation,
        exporting::EventKind::kRename, 1, true);
    task.source = "Pictures/fallback.jpg";
    task.target = "Download/export/fallback.jpg";
    return task;
}

class FakeEventSource final : public exporting::EventSource {
public:
    exporting::EventPollResult Poll(exporting::ExportEvent* event) noexcept override {
        if (fail) return exporting::EventPollResult::kFailed;
        if (events.empty()) return exporting::EventPollResult::kDrained;
        *event = std::move(events.front());
        events.pop_front();
        return exporting::EventPollResult::kEvent;
    }

    std::deque<exporting::ExportEvent> events;
    bool fail = false;
};

class FakeRecoveryStore final : public exporting::RecoveryStore {
public:
    exporting::StoreStatus Save(
        const exporting::RecoverySnapshot& next) noexcept override {
        if (save_status != exporting::StoreStatus::kOk) return save_status;
        snapshot = next;
        has_snapshot = true;
        return exporting::StoreStatus::kOk;
    }

    exporting::StoreStatus Load(
        exporting::RecoverySnapshot* output) noexcept override {
        if (load_status != exporting::StoreStatus::kOk) return load_status;
        if (!has_snapshot) return exporting::StoreStatus::kEmpty;
        *output = snapshot;
        return exporting::StoreStatus::kOk;
    }

    exporting::RecoverySnapshot snapshot;
    exporting::StoreStatus save_status = exporting::StoreStatus::kOk;
    exporting::StoreStatus load_status = exporting::StoreStatus::kOk;
    bool has_snapshot = false;
};

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    using namespace exporting;

    const ExportTask close_task = FidTask(
        3, "Pictures/a.jpg", "Download/export/a.jpg");
    const ExportTask rename_task = FallbackTask(4, 3);
    assert(!TaskKey::ProbedStat(
        1, 5, 50, 3, EventKind::kCloseWrite, 1, false).valid());
    assert(close_task.key.identity().kind() == IdentityKind::kFid);
    assert(close_task.key.generation() == 3);
    assert(close_task.key.event_kind() == EventKind::kCloseWrite);

    ExportWorker windowed(2);
    assert(windowed.Enqueue(close_task) == EnqueueResult::kQueued);
    assert(windowed.Enqueue(FidTask(
        3, "Pictures/a.jpg", "Download/export/a.jpg", 2))
           == EnqueueResult::kQueued);

    ExportWorker worker(2);
    ExportEventIngestor ingestor(&worker);
    FakeEventSource source;
    source.events.push_back({EventKind::kCloseWrite, close_task, 20});
    source.events.push_back({EventKind::kRename, rename_task, 10});
    source.events.push_back({EventKind::kCloseWrite, close_task, 30});
    source.events.push_back({EventKind::kOverflow, {}, 40});

    assert(ingestor.PumpOne(&source) == IngestResult::kQueued);
    assert(ingestor.PumpOne(&source) == IngestResult::kQueued);
    assert(ingestor.PumpOne(&source) == IngestResult::kDuplicate);
    assert(ingestor.PumpOne(&source) == IngestResult::kOverflow);
    assert(ingestor.PumpOne(&source) == IngestResult::kDrained);
    assert(worker.pending() == 2);
    assert(worker.rescan_required());
    assert(ingestor.metrics().close_write == 2);
    assert(ingestor.metrics().rename == 1);
    assert(ingestor.metrics().overflow == 1);

    assert(worker.RunNext([](const ExportTask&) {
        return TransferResult::Complete();
    }));
    assert(worker.State(close_task.key) == TaskState::kComplete);
    assert(worker.FailureStage(close_task.key) == TransferStage::kNone);
    assert(worker.RunNext([](const ExportTask&) {
        return TransferResult::Failed(TransferStage::kSyncTarget);
    }));
    assert(worker.State(rename_task.key) == TaskState::kFailed);
    assert(worker.FailureStage(rename_task.key) == TransferStage::kSyncTarget);

    FakeRecoveryStore failed_state_store;
    assert(worker.Checkpoint(&failed_state_store) == StoreStatus::kOk);
    ExportWorker failed_state_recovered(2);
    assert(failed_state_recovered.Recover(&failed_state_store)
           == RestoreResult::kRestored);
    assert(failed_state_recovered.State(rename_task.key) == TaskState::kFailed);
    assert(failed_state_recovered.FailureStage(rename_task.key)
           == TransferStage::kSyncTarget);

    const ExportTask queued_task = FidTask(
        4, "Pictures/c.jpg", "Download/export/c.jpg");
    assert(worker.Enqueue(queued_task) == EnqueueResult::kQueued);
    assert(!worker.Contains(close_task.key));
    assert(worker.Retry(rename_task.key) == RetryResult::kQueued);

    FakeRecoveryStore store;
    assert(worker.Checkpoint(&store) == StoreStatus::kOk);
    ExportWorker recovered(2);
    assert(recovered.Recover(&store) == RestoreResult::kRestored);
    assert(!recovered.Contains(close_task.key));
    assert(recovered.State(rename_task.key) == TaskState::kQueued);
    assert(recovered.State(queued_task.key) == TaskState::kQueued);
    assert(recovered.pending() == 2);
    assert(recovered.rescan_required());
    assert(recovered.metrics().recovered == 2);

    RecoverySnapshot interrupted = recovered.CaptureSnapshot();
    interrupted.records.front().state = TaskState::kRunning;
    ExportWorker replayed(3);
    assert(replayed.Restore(interrupted) == RestoreResult::kRestored);
    assert(replayed.State(interrupted.records.front().task.key)
           == TaskState::kQueued);

    RecoverySnapshot corrupt = interrupted;
    corrupt.records.push_back(corrupt.records.front());
    assert(replayed.Restore(corrupt) == RestoreResult::kInvalidSnapshot);
    assert(replayed.pending() == 2);

    RecoverySnapshot wrong_version = interrupted;
    ++wrong_version.version;
    assert(replayed.Restore(wrong_version) == RestoreResult::kInvalidSnapshot);
    assert(replayed.pending() == 2);

    ExportWorker too_small(1);
    assert(too_small.Restore(worker.CaptureSnapshot())
           == RestoreResult::kCapacityExceeded);
    assert(too_small.pending() == 0);

    FakeEventSource failed_source;
    failed_source.fail = true;
    assert(ingestor.PumpOne(&failed_source) == IngestResult::kSourceFailed);
    assert(ingestor.metrics().source_failed == 1);
    assert(ingestor.Ingest({EventKind::kMovedTo, {}, 0})
           == IngestResult::kInvalid);
    assert(ingestor.Ingest({EventKind::kMovedTo, close_task, 50})
           == IngestResult::kInvalid);
    assert(ingestor.metrics().invalid == 2);

    FakeRecoveryStore failed_store;
    failed_store.save_status = StoreStatus::kIoError;
    assert(worker.Checkpoint(&failed_store) == StoreStatus::kIoError);
    assert(worker.metrics().checkpoint_failed == 1);

    FakeRecoveryStore corrupt_store;
    corrupt_store.load_status = StoreStatus::kCorrupt;
    ExportWorker corrupt_recovery(2);
    assert(corrupt_recovery.Recover(&corrupt_store)
           == RestoreResult::kInvalidSnapshot);
    assert(corrupt_recovery.metrics().recovery_failed == 1);

    ExportWorker full(1);
    assert(full.Enqueue(close_task) == EnqueueResult::kQueued);
    assert(full.Enqueue(rename_task) == EnqueueResult::kFull);
    assert(full.rescan_required());
    assert(full.metrics().overflow == 1);

    ExportWorker bounded(8);
    ExportTask first_completed;
    ExportTask last_completed;
    for (std::uint64_t window = 1; window <= 128; ++window) {
        ExportTask task = FidTask(
            7, "Pictures/window.jpg", "Download/export/window.jpg", window);
        if (window == 1) first_completed = task;
        last_completed = task;
        assert(bounded.Enqueue(task) == EnqueueResult::kQueued);
        assert(bounded.RunNext([](const ExportTask&) {
            return TransferResult::Complete();
        }));
        assert(bounded.CaptureSnapshot().records.size() <= 8);
    }
    assert(!bounded.Contains(first_completed.key));
    assert(bounded.Contains(last_completed.key));
    assert(bounded.CaptureSnapshot().records.size() == 8);
    FakeRecoveryStore bounded_store;
    assert(bounded.Checkpoint(&bounded_store) == StoreStatus::kOk);
    assert(bounded_store.snapshot.records.size() == 8);

    ExportWorker recovery_limited(kMaxRecoveryRecords + 1);
    for (std::uint64_t window = 1;
         window <= kMaxRecoveryRecords + 1; ++window) {
        assert(recovery_limited.Enqueue(FidTask(
            8, "Pictures/recovery.jpg", "Download/export/recovery.jpg", window))
               == EnqueueResult::kQueued);
        assert(recovery_limited.RunNext([](const ExportTask&) {
            return TransferResult::Complete();
        }));
    }
    assert(recovery_limited.CaptureSnapshot().records.size()
           == kMaxRecoveryRecords);
    FakeRecoveryStore recovery_limited_store;
    assert(recovery_limited.Checkpoint(&recovery_limited_store)
           == StoreStatus::kOk);

    ExportWorker failed_bounded(2);
    assert(failed_bounded.Enqueue(close_task) == EnqueueResult::kQueued);
    assert(failed_bounded.Enqueue(rename_task) == EnqueueResult::kQueued);
    assert(failed_bounded.RunNext([](const ExportTask&) {
        return TransferResult::Failed(TransferStage::kCopyData);
    }));
    assert(failed_bounded.RunNext([](const ExportTask&) {
        return TransferResult::Failed(TransferStage::kSyncTarget);
    }));
    assert(failed_bounded.Enqueue(queued_task) == EnqueueResult::kFull);
    assert(failed_bounded.CaptureSnapshot().records.size() == 2);

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("pathguard-export-worker-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directories(root / "source"));
    assert(fs::create_directories(root / "target"));
    const fs::path snapshot_path = root / "recovery.bin";
    FileRecoveryStore file_store(snapshot_path.string());
    assert(file_store.Save(worker.CaptureSnapshot()) == StoreStatus::kOk);
    RecoverySnapshot invalid_save = worker.CaptureSnapshot();
    invalid_save.records.front().state = TaskState::kFailed;
    invalid_save.records.front().failure_stage = TransferStage::kNone;
    assert(file_store.Save(invalid_save) == StoreStatus::kCorrupt);
    ExportWorker file_recovered(2);
    assert(file_recovered.Recover(&file_store) == RestoreResult::kRestored);
    assert(file_recovered.pending() == 2);

    {
        std::fstream snapshot(snapshot_path,
                              std::ios::in | std::ios::out | std::ios::binary);
        assert(snapshot.good());
        snapshot.seekg(12);
        char checksum = 0;
        snapshot.read(&checksum, 1);
        checksum ^= 0x5a;
        snapshot.seekp(12);
        snapshot.write(&checksum, 1);
    }
    RecoverySnapshot rejected_snapshot;
    assert(file_store.Load(&rejected_snapshot) == StoreStatus::kCorrupt);

    const fs::path copy_source = root / "source" / "copy.txt";
    const fs::path copy_target = root / "target" / "copy.txt";
    {
        std::ofstream output(copy_source, std::ios::binary);
        output << "pathguard-export-copy";
    }
    ExportTask copy_task = FidTask(8, copy_source.string(), copy_target.string());
    copy_task.mode = TransferMode::kCopy;
    const FilesystemTransferExecutor executor;
    assert(executor.Transfer(copy_task).complete);
    assert(fs::exists(copy_source));
    assert(ReadFile(copy_source) == ReadFile(copy_target));
    assert(executor.Transfer(copy_task).failure_stage == TransferStage::kRenameTarget);

    const fs::path move_source = root / "source" / "move.jpg";
    const fs::path move_target = root / "target" / "move.jpg";
    {
        std::ofstream output(move_source, std::ios::binary);
        output << "pathguard-export-move";
    }
    ExportTask move_task = FidTask(9, move_source.string(), move_target.string());
    move_task.mode = TransferMode::kMove;
    assert(executor.Transfer(move_task).complete);
    assert(!fs::exists(move_source));
    assert(ReadFile(move_target) == "pathguard-export-move");

    ExportTask missing_task = FidTask(
        10, (root / "missing.txt").string(),
        (root / "target" / "missing.txt").string());
    assert(executor.Transfer(missing_task).failure_stage
           == TransferStage::kOpenSource);
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    assert(!cleanup_error);
    return 0;
}
