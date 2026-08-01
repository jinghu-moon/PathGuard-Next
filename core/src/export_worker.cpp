#include "pathguard/export_worker.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "pathguard/policy_format.h"

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pathguard::exporting {
namespace {

inline constexpr std::uint32_t kRecoveryMagic = UINT32_C(0x31584750);  // PGX1
inline constexpr std::size_t kRecoveryHeaderSize = 24;
inline constexpr std::size_t kCopyBufferSize = 64 * 1024;

bool ValidStage(TransferStage stage);
bool ValidTask(const ExportTask& task);

void Put16(std::vector<std::uint8_t>* output, std::uint16_t value) {
    output->push_back(static_cast<std::uint8_t>(value));
    output->push_back(static_cast<std::uint8_t>(value >> 8U));
}

void Put32(std::vector<std::uint8_t>* output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output->push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Put64(std::vector<std::uint8_t>* output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output->push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint16_t Read16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0])
        | static_cast<std::uint16_t>(input[1]) << 8U;
}

std::uint32_t Read32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(input[shift / 8]) << shift;
    }
    return value;
}

std::uint64_t Read64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(input[shift / 8]) << shift;
    }
    return value;
}

bool AppendString(std::vector<std::uint8_t>* output, const std::string& value,
                  std::size_t limit) {
    if (value.size() > limit || value.size() > UINT32_MAX) return false;
    Put32(output, static_cast<std::uint32_t>(value.size()));
    output->insert(output->end(), value.begin(), value.end());
    return true;
}

bool ReadString(const std::uint8_t* data, std::size_t size, std::size_t* offset,
                std::size_t limit, std::string* output) {
    if (offset == nullptr || output == nullptr || *offset > size
        || size - *offset < 4) return false;
    const std::uint32_t length = Read32(data + *offset);
    *offset += 4;
    if (length > limit || length > size - *offset) return false;
    output->assign(reinterpret_cast<const char*>(data + *offset), length);
    *offset += length;
    return true;
}

bool SyncFile(std::FILE* file) {
    if (file == nullptr || std::fflush(file) != 0) return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

bool SyncParentDirectory(const std::string& path) {
#if defined(_WIN32)
    (void)path;
    return true;
#else
    const std::size_t slash = path.find_last_of('/');
    const std::string parent = slash == std::string::npos ? "."
        : slash == 0 ? "/" : path.substr(0, slash);
    const int fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
#endif
}

bool AtomicReplace(const std::string& temporary, const std::string& target) {
#if defined(_WIN32)
    return MoveFileExA(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary.c_str(), target.c_str()) == 0
        && SyncParentDirectory(target);
#endif
}

bool AtomicInstallNoReplace(const std::string& temporary,
                            const std::string& target) {
#if defined(_WIN32)
    return MoveFileExA(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (link(temporary.c_str(), target.c_str()) != 0) return false;
    const bool unlinked = unlink(temporary.c_str()) == 0;
    return unlinked && SyncParentDirectory(target);
#endif
}

std::string TemporaryPath(const std::string& target) {
    static std::atomic<std::uint64_t> sequence{0};
#if defined(_WIN32)
    const std::uint64_t process = GetCurrentProcessId();
#else
    const std::uint64_t process = static_cast<std::uint64_t>(getpid());
#endif
    return target + ".pathguard.tmp." + std::to_string(process) + "."
        + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

std::vector<std::uint8_t> EncodeSnapshot(const RecoverySnapshot& snapshot) {
    if (snapshot.version != kRecoverySnapshotVersion
        || snapshot.records.size() > kMaxRecoveryRecords) return {};
    std::vector<std::uint8_t> payload;
    payload.reserve(snapshot.records.size() * 128);
    Put32(&payload, static_cast<std::uint32_t>(snapshot.records.size()));
    payload.push_back(snapshot.rescan_required ? 1 : 0);
    payload.insert(payload.end(), 3, 0);
    for (std::size_t index = 0; index < snapshot.records.size(); ++index) {
        const RecoveryRecord& record = snapshot.records[index];
        if (!ValidTask(record.task) || !ValidStage(record.failure_stage)) return {};
        switch (record.state) {
            case TaskState::kQueued:
            case TaskState::kRunning:
            case TaskState::kComplete:
                if (record.failure_stage != TransferStage::kNone) return {};
                break;
            case TaskState::kFailed:
                if (record.failure_stage == TransferStage::kNone) return {};
                break;
            default: return {};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (snapshot.records[previous].task.key == record.task.key) return {};
        }
        const FileIdentity& identity = record.task.key.identity();
        payload.push_back(static_cast<std::uint8_t>(identity.kind()));
        payload.push_back(static_cast<std::uint8_t>(record.task.key.event_kind()));
        payload.push_back(static_cast<std::uint8_t>(record.task.mode));
        payload.push_back(record.task.media_scan ? 1 : 0);
        payload.push_back(static_cast<std::uint8_t>(record.state));
        payload.push_back(static_cast<std::uint8_t>(record.failure_stage));
        Put16(&payload, 0);
        Put64(&payload, identity.primary());
        Put64(&payload, identity.secondary());
        Put64(&payload, identity.change_time_ns());
        Put64(&payload, record.task.key.generation());
        Put64(&payload, record.task.key.event_window());
        if (!AppendString(&payload, identity.file_handle(), kMaxRecoveryHandleBytes)
            || !AppendString(&payload, record.task.source, kMaxRecoveryPathBytes)
            || !AppendString(&payload, record.task.target, kMaxRecoveryPathBytes)) return {};
    }
    if (payload.size() > UINT32_MAX) return {};
    std::vector<std::uint8_t> output;
    output.reserve(kRecoveryHeaderSize + payload.size());
    Put32(&output, kRecoveryMagic);
    Put16(&output, static_cast<std::uint16_t>(kRecoverySnapshotVersion));
    Put16(&output, static_cast<std::uint16_t>(kRecoveryHeaderSize));
    Put32(&output, static_cast<std::uint32_t>(kRecoveryHeaderSize + payload.size()));
    Put32(&output, binary_format::Crc32(payload.data(), payload.size()));
    Put32(&output, static_cast<std::uint32_t>(payload.size()));
    Put32(&output, 0);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

bool DecodeSnapshot(const std::vector<std::uint8_t>& input,
                    RecoverySnapshot* snapshot) {
    if (snapshot == nullptr || input.size() < kRecoveryHeaderSize
        || Read32(input.data()) != kRecoveryMagic
        || Read16(input.data() + 4) != kRecoverySnapshotVersion
        || Read16(input.data() + 6) != kRecoveryHeaderSize
        || Read32(input.data() + 8) != input.size()
        || Read32(input.data() + 16) != input.size() - kRecoveryHeaderSize
        || Read32(input.data() + 20) != 0) return false;
    const std::uint8_t* payload = input.data() + kRecoveryHeaderSize;
    const std::size_t payload_size = input.size() - kRecoveryHeaderSize;
    if (binary_format::Crc32(payload, payload_size) != Read32(input.data() + 12)
        || payload_size < 8) return false;
    const std::uint32_t count = Read32(payload);
    if (count > kMaxRecoveryRecords || payload[4] > 1
        || payload[5] != 0 || payload[6] != 0 || payload[7] != 0) return false;
    RecoverySnapshot decoded;
    decoded.rescan_required = payload[4] != 0;
    decoded.records.reserve(count);
    std::size_t offset = 8;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (offset > payload_size || payload_size - offset < 48) return false;
        const auto identity_kind = static_cast<IdentityKind>(payload[offset]);
        const auto event_kind = static_cast<EventKind>(payload[offset + 1]);
        const auto mode = static_cast<TransferMode>(payload[offset + 2]);
        const bool media_scan = payload[offset + 3] != 0;
        const auto state = static_cast<TaskState>(payload[offset + 4]);
        const auto failure = static_cast<TransferStage>(payload[offset + 5]);
        if (payload[offset + 3] > 1 || Read16(payload + offset + 6) != 0) return false;
        const std::uint64_t first = Read64(payload + offset + 8);
        const std::uint64_t second = Read64(payload + offset + 16);
        const std::uint64_t third = Read64(payload + offset + 24);
        const std::uint64_t generation = Read64(payload + offset + 32);
        const std::uint64_t window = Read64(payload + offset + 40);
        offset += 48;
        std::string handle;
        ExportTask task;
        if (!ReadString(payload, payload_size, &offset, kMaxRecoveryHandleBytes, &handle)
            || !ReadString(payload, payload_size, &offset, kMaxRecoveryPathBytes, &task.source)
            || !ReadString(payload, payload_size, &offset, kMaxRecoveryPathBytes, &task.target)) return false;
        if (identity_kind == IdentityKind::kFid) {
            task.key = TaskKey::Fid(first, second, std::move(handle), generation,
                                    event_kind, window);
        } else if (identity_kind == IdentityKind::kProbedStat && handle.empty()) {
            task.key = TaskKey::ProbedStat(first, second, third, generation,
                                           event_kind, window, true);
        } else {
            return false;
        }
        task.mode = mode;
        task.media_scan = media_scan;
        RecoveryRecord record{std::move(task), state, failure};
        if (!ValidTask(record.task) || !ValidStage(record.failure_stage)) return false;
        decoded.records.push_back(std::move(record));
    }
    if (offset != payload_size) return false;
    *snapshot = std::move(decoded);
    return true;
}

bool ValidMode(TransferMode mode) {
    switch (mode) {
        case TransferMode::kCopy:
        case TransferMode::kMove:
        case TransferMode::kTrash: return true;
    }
    return false;
}

bool ValidStage(TransferStage stage) {
    switch (stage) {
        case TransferStage::kNone:
        case TransferStage::kOpenSource:
        case TransferStage::kCreateTemporary:
        case TransferStage::kCopyData:
        case TransferStage::kSyncTarget:
        case TransferStage::kRenameTarget:
        case TransferStage::kRemoveSource: return true;
    }
    return false;
}

bool ValidTask(const ExportTask& task) {
    return task.key.valid() && !task.source.empty() && !task.target.empty()
        && ValidMode(task.mode);
}

}  // namespace

StoreStatus FileRecoveryStore::Save(const RecoverySnapshot& snapshot) noexcept {
    const std::vector<std::uint8_t> encoded = EncodeSnapshot(snapshot);
    if (encoded.empty() || path_.empty()) return StoreStatus::kCorrupt;
    const std::string temporary = TemporaryPath(path_);
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (file == nullptr) return StoreStatus::kIoError;
    const bool written = std::fwrite(encoded.data(), 1, encoded.size(), file)
        == encoded.size();
    const bool synced = written && SyncFile(file);
    const bool closed = std::fclose(file) == 0;
    if (!synced || !closed || !AtomicReplace(temporary, path_)) {
        std::remove(temporary.c_str());
        return StoreStatus::kIoError;
    }
    return StoreStatus::kOk;
}

StoreStatus FileRecoveryStore::Load(RecoverySnapshot* snapshot) noexcept {
    if (snapshot == nullptr || path_.empty()) return StoreStatus::kCorrupt;
    std::FILE* file = std::fopen(path_.c_str(), "rb");
    if (file == nullptr) return errno == ENOENT
        ? StoreStatus::kEmpty : StoreStatus::kIoError;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return StoreStatus::kIoError;
    }
    const long length = std::ftell(file);
    if (length < 0 || static_cast<std::uint64_t>(length)
        > static_cast<std::uint64_t>(UINT32_MAX) + kRecoveryHeaderSize
        || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return StoreStatus::kCorrupt;
    }
    std::vector<std::uint8_t> input(static_cast<std::size_t>(length));
    const bool read = input.empty()
        || std::fread(input.data(), 1, input.size(), file) == input.size();
    const bool io_failed = std::ferror(file) != 0;
    std::fclose(file);
    if (!read || io_failed) return StoreStatus::kIoError;
    return DecodeSnapshot(input, snapshot) ? StoreStatus::kOk
                                            : StoreStatus::kCorrupt;
}

TransferResult FilesystemTransferExecutor::Transfer(
        const ExportTask& task) const noexcept {
    if (!ValidTask(task)) return TransferResult::Failed(TransferStage::kOpenSource);
    std::FILE* source = std::fopen(task.source.c_str(), "rb");
    if (source == nullptr) return TransferResult::Failed(TransferStage::kOpenSource);
    const std::string temporary = TemporaryPath(task.target);
    std::FILE* target = std::fopen(temporary.c_str(), "wbx");
    if (target == nullptr) {
        std::fclose(source);
        return TransferResult::Failed(TransferStage::kCreateTemporary);
    }
    std::vector<std::uint8_t> buffer(kCopyBufferSize);
    bool copied = true;
    for (;;) {
        const std::size_t count = std::fread(buffer.data(), 1, buffer.size(), source);
        if (count != 0 && std::fwrite(buffer.data(), 1, count, target) != count) {
            copied = false;
            break;
        }
        if (count < buffer.size()) {
            copied = std::feof(source) != 0 && std::ferror(source) == 0;
            break;
        }
    }
    const bool source_closed = std::fclose(source) == 0;
    if (!copied || !source_closed) {
        std::fclose(target);
        std::remove(temporary.c_str());
        return TransferResult::Failed(TransferStage::kCopyData);
    }
    const bool synced = SyncFile(target);
    const bool target_closed = std::fclose(target) == 0;
    if (!synced || !target_closed) {
        std::remove(temporary.c_str());
        return TransferResult::Failed(TransferStage::kSyncTarget);
    }
    if (!AtomicInstallNoReplace(temporary, task.target)) {
        std::remove(temporary.c_str());
        return TransferResult::Failed(TransferStage::kRenameTarget);
    }
    if (task.mode != TransferMode::kCopy
        && std::remove(task.source.c_str()) != 0) {
        return TransferResult::Failed(TransferStage::kRemoveSource);
    }
    return TransferResult::Complete();
}

FileIdentity FileIdentity::Fid(std::uint64_t fsid,
                               std::uint64_t mount_identity,
                               std::string file_handle) {
    FileIdentity identity;
    identity.kind_ = IdentityKind::kFid;
    identity.first_ = fsid;
    identity.second_ = mount_identity;
    identity.file_handle_ = std::move(file_handle);
    return identity;
}

FileIdentity FileIdentity::ProbedStat(std::uint64_t device,
                                      std::uint64_t inode,
                                      std::uint64_t change_time_ns,
                                      bool capability_verified) {
    FileIdentity identity;
    if (!capability_verified) return identity;
    identity.kind_ = IdentityKind::kProbedStat;
    identity.first_ = device;
    identity.second_ = inode;
    identity.third_ = change_time_ns;
    return identity;
}

bool FileIdentity::valid() const {
    if (kind_ == IdentityKind::kFid) {
        return second_ != 0 && !file_handle_.empty();
    }
    if (kind_ == IdentityKind::kProbedStat) {
        return first_ != 0 && second_ != 0 && third_ != 0;
    }
    return false;
}

TaskKey TaskKey::Fid(std::uint64_t fsid,
                     std::uint64_t mount_identity,
                     std::string file_handle,
                     std::uint64_t generation,
                     EventKind event_kind,
                     std::uint64_t event_window) {
    TaskKey key;
    key.identity_ = FileIdentity::Fid(fsid, mount_identity, std::move(file_handle));
    key.generation_ = generation;
    key.event_kind_ = event_kind;
    key.event_window_ = event_window;
    return key;
}

TaskKey TaskKey::ProbedStat(std::uint64_t device,
                            std::uint64_t inode,
                            std::uint64_t change_time_ns,
                            std::uint64_t generation,
                            EventKind event_kind,
                            std::uint64_t event_window,
                            bool capability_verified) {
    TaskKey key;
    key.identity_ = FileIdentity::ProbedStat(
        device, inode, change_time_ns, capability_verified);
    key.generation_ = generation;
    key.event_kind_ = event_kind;
    key.event_window_ = event_window;
    return key;
}

bool TaskKey::valid() const {
    return identity_.valid() && generation_ != 0 && event_window_ != 0
        && event_kind_ != EventKind::kOverflow;
}

EnqueueResult ExportWorker::Enqueue(ExportTask task) {
    if (!ValidTask(task)) return EnqueueResult::kInvalid;
    if (states_.contains(task.key)) {
        ++metrics_.duplicates;
        return EnqueueResult::kDuplicate;
    }
    while (tasks_.size() >= capacity_ && EvictOneCompleted()) {
    }
    if (capacity_ == 0 || tasks_.size() >= capacity_) {
        ++metrics_.overflow;
        rescan_required_ = true;
        return EnqueueResult::kFull;
    }
    const TaskKey key = task.key;
    tasks_.emplace(key, std::move(task));
    states_[key] = TaskState::kQueued;
    failure_stages_[key] = TransferStage::kNone;
    queue_.push_back(key);
    ++metrics_.queued;
    return EnqueueResult::kQueued;
}

bool ExportWorker::EvictOneCompleted() {
    for (const auto& [key, state] : states_) {
        if (state != TaskState::kComplete) continue;
        tasks_.erase(key);
        failure_stages_.erase(key);
        states_.erase(key);
        return true;
    }
    return false;
}

bool ExportWorker::RunNext(const Transfer& transfer) {
    if (queue_.empty() || !transfer) return false;
    const TaskKey key = queue_.front();
    queue_.pop_front();
    const auto task = tasks_.find(key);
    if (task == tasks_.end()) return false;
    states_[key] = TaskState::kRunning;
    const TransferResult result = transfer(task->second);
    if (result.valid() && result.complete) {
        states_[key] = TaskState::kComplete;
        failure_stages_[key] = TransferStage::kNone;
        ++metrics_.completed;
    } else {
        states_[key] = TaskState::kFailed;
        failure_stages_[key] = result.valid()
            ? result.failure_stage : TransferStage::kOpenSource;
        ++metrics_.failed;
    }
    return true;
}

RetryResult ExportWorker::Retry(const TaskKey& key) {
    const auto state = states_.find(key);
    if (state == states_.end()) return RetryResult::kNotFound;
    if (state->second != TaskState::kFailed) return RetryResult::kNotFailed;
    if (capacity_ == 0 || queue_.size() >= capacity_) return RetryResult::kFull;
    state->second = TaskState::kQueued;
    failure_stages_[key] = TransferStage::kNone;
    queue_.push_back(key);
    ++metrics_.retried;
    return RetryResult::kQueued;
}

void ExportWorker::ObserveOverflow() {
    ++metrics_.overflow;
    rescan_required_ = true;
}

bool ExportWorker::AcknowledgeRescan() {
    if (!rescan_required_) return false;
    rescan_required_ = false;
    ++metrics_.rescans;
    return true;
}

RecoverySnapshot ExportWorker::CaptureSnapshot() const {
    RecoverySnapshot snapshot;
    snapshot.rescan_required = rescan_required_;
    snapshot.records.reserve(tasks_.size());
    for (const auto& [key, task] : tasks_) {
        const auto state = states_.find(key);
        if (state == states_.end()) continue;
        const auto failure = failure_stages_.find(key);
        snapshot.records.push_back({
            task,
            state->second,
            failure == failure_stages_.end()
                ? TransferStage::kNone : failure->second,
        });
    }
    return snapshot;
}

StoreStatus ExportWorker::Checkpoint(RecoveryStore* store) {
    if (store == nullptr) {
        ++metrics_.checkpoint_failed;
        return StoreStatus::kIoError;
    }
    const StoreStatus status = store->Save(CaptureSnapshot());
    if (status != StoreStatus::kOk) ++metrics_.checkpoint_failed;
    return status;
}

RestoreResult ExportWorker::Restore(const RecoverySnapshot& snapshot) {
    if (snapshot.version != kRecoverySnapshotVersion) {
        ++metrics_.recovery_failed;
        return RestoreResult::kInvalidSnapshot;
    }
    if (snapshot.records.size() > capacity_
        || snapshot.records.size() > kMaxRecoveryRecords) {
        ++metrics_.recovery_failed;
        return RestoreResult::kCapacityExceeded;
    }

    std::deque<TaskKey> queue;
    std::map<TaskKey, ExportTask> tasks;
    std::map<TaskKey, TaskState> states;
    std::map<TaskKey, TransferStage> failure_stages;
    for (const RecoveryRecord& record : snapshot.records) {
        if (!ValidTask(record.task) || !ValidStage(record.failure_stage)
            || tasks.contains(record.task.key)) {
            ++metrics_.recovery_failed;
            return RestoreResult::kInvalidSnapshot;
        }

        TaskState restored_state = record.state;
        TransferStage restored_failure = record.failure_stage;
        switch (record.state) {
            case TaskState::kQueued:
            case TaskState::kRunning:
                restored_state = TaskState::kQueued;
                restored_failure = TransferStage::kNone;
                queue.push_back(record.task.key);
                break;
            case TaskState::kComplete:
                restored_failure = TransferStage::kNone;
                break;
            case TaskState::kFailed:
                if (restored_failure == TransferStage::kNone) {
                    ++metrics_.recovery_failed;
                    return RestoreResult::kInvalidSnapshot;
                }
                break;
            default:
                ++metrics_.recovery_failed;
                return RestoreResult::kInvalidSnapshot;
        }
        const TaskKey key = record.task.key;
        tasks.emplace(key, record.task);
        states.emplace(key, restored_state);
        failure_stages.emplace(key, restored_failure);
    }

    queue_ = std::move(queue);
    tasks_ = std::move(tasks);
    states_ = std::move(states);
    failure_stages_ = std::move(failure_stages);
    rescan_required_ = snapshot.rescan_required;
    metrics_.recovered += snapshot.records.size();
    return RestoreResult::kRestored;
}

RestoreResult ExportWorker::Recover(RecoveryStore* store) {
    if (store == nullptr) {
        ++metrics_.recovery_failed;
        return RestoreResult::kStoreFailed;
    }
    RecoverySnapshot snapshot;
    const StoreStatus status = store->Load(&snapshot);
    if (status == StoreStatus::kEmpty) return RestoreResult::kEmpty;
    if (status == StoreStatus::kCorrupt) {
        ++metrics_.recovery_failed;
        return RestoreResult::kInvalidSnapshot;
    }
    if (status != StoreStatus::kOk) {
        ++metrics_.recovery_failed;
        return RestoreResult::kStoreFailed;
    }
    return Restore(snapshot);
}

TaskState ExportWorker::State(const TaskKey& key) const {
    const auto found = states_.find(key);
    return found == states_.end() ? TaskState::kFailed : found->second;
}

TransferStage ExportWorker::FailureStage(const TaskKey& key) const {
    const auto found = failure_stages_.find(key);
    return found == failure_stages_.end() ? TransferStage::kNone : found->second;
}

IngestResult ExportEventIngestor::Ingest(ExportEvent event) {
    if (worker_ == nullptr || event.sequence == 0) {
        ++metrics_.invalid;
        return IngestResult::kInvalid;
    }
    if (event.kind == EventKind::kOverflow) {
        ++metrics_.overflow;
        worker_->ObserveOverflow();
        return IngestResult::kOverflow;
    }

    switch (event.kind) {
        case EventKind::kCloseWrite: ++metrics_.close_write; break;
        case EventKind::kMovedTo: ++metrics_.moved_to; break;
        case EventKind::kRename: ++metrics_.rename; break;
        case EventKind::kOverflow: break;
        default:
            ++metrics_.invalid;
            return IngestResult::kInvalid;
    }
    if (!event.task.key.MatchesEvent(event.kind)) {
        ++metrics_.invalid;
        return IngestResult::kInvalid;
    }
    switch (worker_->Enqueue(std::move(event.task))) {
        case EnqueueResult::kQueued: return IngestResult::kQueued;
        case EnqueueResult::kDuplicate: return IngestResult::kDuplicate;
        case EnqueueResult::kFull: return IngestResult::kFull;
        case EnqueueResult::kInvalid:
            ++metrics_.invalid;
            return IngestResult::kInvalid;
    }
    ++metrics_.invalid;
    return IngestResult::kInvalid;
}

IngestResult ExportEventIngestor::PumpOne(EventSource* source) {
    if (source == nullptr) {
        ++metrics_.source_failed;
        return IngestResult::kSourceFailed;
    }
    ExportEvent event;
    switch (source->Poll(&event)) {
        case EventPollResult::kEvent: return Ingest(std::move(event));
        case EventPollResult::kDrained: return IngestResult::kDrained;
        case EventPollResult::kFailed:
            ++metrics_.source_failed;
            return IngestResult::kSourceFailed;
    }
    ++metrics_.source_failed;
    return IngestResult::kSourceFailed;
}

}  // namespace pathguard::exporting
