#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pathguard::exporting {

inline constexpr std::uint32_t kRecoverySnapshotVersion = 1;
inline constexpr std::size_t kMaxRecoveryRecords = 4096;
inline constexpr std::size_t kMaxRecoveryPathBytes = 4096;
inline constexpr std::size_t kMaxRecoveryHandleBytes = 128;

enum class EventKind : std::uint8_t { kCloseWrite, kMovedTo, kRename, kOverflow };
enum class IdentityKind : std::uint8_t { kInvalid, kFid, kProbedStat };
enum class TransferMode : std::uint8_t { kCopy, kMove, kTrash };
enum class EnqueueResult : std::uint8_t { kQueued, kDuplicate, kFull, kInvalid };
enum class TaskState : std::uint8_t { kQueued, kRunning, kComplete, kFailed };
enum class RetryResult : std::uint8_t { kQueued, kNotFound, kNotFailed, kFull };
enum class TransferStage : std::uint8_t {
    kNone,
    kOpenSource,
    kCreateTemporary,
    kCopyData,
    kSyncTarget,
    kRenameTarget,
    kRemoveSource,
};

class FileIdentity final {
public:
    FileIdentity() = default;

    static FileIdentity Fid(std::uint64_t fsid,
                            std::uint64_t mount_identity,
                            std::string file_handle);
    static FileIdentity ProbedStat(std::uint64_t device,
                                   std::uint64_t inode,
                                   std::uint64_t change_time_ns,
                                   bool capability_verified);
    bool valid() const;
    IdentityKind kind() const { return kind_; }
    std::uint64_t primary() const { return first_; }
    std::uint64_t secondary() const { return second_; }
    std::uint64_t change_time_ns() const { return third_; }
    const std::string& file_handle() const { return file_handle_; }
    auto operator<=>(const FileIdentity&) const = default;

private:
    IdentityKind kind_ = IdentityKind::kInvalid;
    std::uint64_t first_ = 0;
    std::uint64_t second_ = 0;
    std::uint64_t third_ = 0;
    std::string file_handle_;
};

class TaskKey final {
public:
    TaskKey() = default;

    static TaskKey Fid(std::uint64_t fsid,
                       std::uint64_t mount_identity,
                       std::string file_handle,
                       std::uint64_t generation,
                       EventKind event_kind,
                       std::uint64_t event_window);
    // This fallback is valid only after the platform adapter has probed the
    // filesystem identity capability. Callers cannot silently assume it.
    static TaskKey ProbedStat(std::uint64_t device,
                              std::uint64_t inode,
                              std::uint64_t change_time_ns,
                              std::uint64_t generation,
                              EventKind event_kind,
                              std::uint64_t event_window,
                              bool capability_verified);
    bool valid() const;
    bool MatchesEvent(EventKind event_kind) const { return event_kind_ == event_kind; }
    const FileIdentity& identity() const { return identity_; }
    std::uint64_t generation() const { return generation_; }
    EventKind event_kind() const { return event_kind_; }
    std::uint64_t event_window() const { return event_window_; }
    auto operator<=>(const TaskKey&) const = default;

private:
    FileIdentity identity_;
    std::uint64_t generation_ = 0;
    EventKind event_kind_ = EventKind::kCloseWrite;
    std::uint64_t event_window_ = 0;
};

struct ExportTask {
    TaskKey key;
    std::string source;
    std::string target;
    TransferMode mode = TransferMode::kCopy;
    bool media_scan = false;
};

struct TransferResult {
    bool complete = false;
    TransferStage failure_stage = TransferStage::kNone;

    static constexpr TransferResult Complete() { return {true, TransferStage::kNone}; }
    static constexpr TransferResult Failed(TransferStage stage) {
        return {false, stage};
    }
    bool valid() const {
        return complete ? failure_stage == TransferStage::kNone
                        : failure_stage != TransferStage::kNone;
    }
};

struct RecoveryRecord {
    ExportTask task;
    TaskState state = TaskState::kQueued;
    TransferStage failure_stage = TransferStage::kNone;
};

struct RecoverySnapshot {
    std::uint32_t version = kRecoverySnapshotVersion;
    bool rescan_required = false;
    std::vector<RecoveryRecord> records;
};

enum class StoreStatus : std::uint8_t { kOk, kEmpty, kIoError, kCorrupt };
enum class RestoreResult : std::uint8_t {
    kRestored,
    kEmpty,
    kInvalidSnapshot,
    kCapacityExceeded,
    kStoreFailed,
};

class RecoveryStore {
public:
    virtual ~RecoveryStore() = default;
    virtual StoreStatus Save(const RecoverySnapshot& snapshot) noexcept = 0;
    virtual StoreStatus Load(RecoverySnapshot* snapshot) noexcept = 0;
};

// A versioned, checksummed snapshot installed with write+sync+atomic rename.
// The parent directory is synced on POSIX so a successful Save survives a
// daemon restart or power loss at the rename boundary.
class FileRecoveryStore final : public RecoveryStore {
public:
    explicit FileRecoveryStore(std::string path) : path_(std::move(path)) {}
    StoreStatus Save(const RecoverySnapshot& snapshot) noexcept override;
    StoreStatus Load(RecoverySnapshot* snapshot) noexcept override;

private:
    std::string path_;
};

// Filesystem export uses a temporary file in the target directory. This keeps
// the final install atomic even when source and target are on different file
// systems. Copy keeps the source; move/trash remove it only after install.
class FilesystemTransferExecutor final {
public:
    TransferResult Transfer(const ExportTask& task) const noexcept;
};

struct WorkerMetrics {
    std::uint64_t queued = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t overflow = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t retried = 0;
    std::uint64_t rescans = 0;
    std::uint64_t recovered = 0;
    std::uint64_t recovery_failed = 0;
    std::uint64_t checkpoint_failed = 0;
};

class ExportWorker final {
public:
    // Transfer implementations must not throw; Android production builds use
    // -fno-exceptions. A failed copy/sync/rename stage never becomes complete.
    using Transfer = std::function<TransferResult(const ExportTask&)>;

    explicit ExportWorker(std::size_t capacity)
        : capacity_(capacity > kMaxRecoveryRecords
              ? kMaxRecoveryRecords : capacity) {}
    EnqueueResult Enqueue(ExportTask task);
    bool RunNext(const Transfer& transfer);
    RetryResult Retry(const TaskKey& key);
    void ObserveOverflow();
    bool AcknowledgeRescan();
    RecoverySnapshot CaptureSnapshot() const;
    StoreStatus Checkpoint(RecoveryStore* store);
    RestoreResult Restore(const RecoverySnapshot& snapshot);
    RestoreResult Recover(RecoveryStore* store);
    TaskState State(const TaskKey& key) const;
    TransferStage FailureStage(const TaskKey& key) const;
    bool Contains(const TaskKey& key) const { return states_.contains(key); }
    std::size_t pending() const { return queue_.size(); }
    bool rescan_required() const { return rescan_required_; }
    const WorkerMetrics& metrics() const { return metrics_; }

private:
    bool EvictOneCompleted();

    std::size_t capacity_ = 0;
    std::deque<TaskKey> queue_;
    std::map<TaskKey, ExportTask> tasks_;
    std::map<TaskKey, TaskState> states_;
    std::map<TaskKey, TransferStage> failure_stages_;
    WorkerMetrics metrics_;
    bool rescan_required_ = false;
};

enum class EventPollResult : std::uint8_t { kEvent, kDrained, kFailed };
enum class IngestResult : std::uint8_t {
    kQueued,
    kDuplicate,
    kFull,
    kOverflow,
    kInvalid,
    kDrained,
    kSourceFailed,
};

struct ExportEvent {
    EventKind kind = EventKind::kCloseWrite;
    ExportTask task;
    std::uint64_t sequence = 0;
};

class EventSource {
public:
    virtual ~EventSource() = default;
    // Sources normalize platform metadata into a validated task. A production
    // fanotify adapter remains responsible for capability and pidfd checks.
    virtual EventPollResult Poll(ExportEvent* event) noexcept = 0;
};

struct IngestMetrics {
    std::uint64_t close_write = 0;
    std::uint64_t moved_to = 0;
    std::uint64_t rename = 0;
    std::uint64_t overflow = 0;
    std::uint64_t invalid = 0;
    std::uint64_t source_failed = 0;
};

class ExportEventIngestor final {
public:
    explicit ExportEventIngestor(ExportWorker* worker) : worker_(worker) {}
    IngestResult Ingest(ExportEvent event);
    IngestResult PumpOne(EventSource* source);
    const IngestMetrics& metrics() const { return metrics_; }

private:
    ExportWorker* worker_ = nullptr;
    IngestMetrics metrics_;
};

}  // namespace pathguard::exporting
