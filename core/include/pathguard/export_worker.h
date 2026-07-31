#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

namespace pathguard::exporting {

enum class TransferMode : std::uint8_t { kCopy, kMove, kTrash };
enum class EnqueueResult : std::uint8_t { kQueued, kDuplicate, kFull, kInvalid };
enum class TaskState : std::uint8_t { kQueued, kRunning, kComplete, kFailed };

struct TaskKey {
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::uint64_t change_time_ns = 0;
    std::uint64_t generation = 0;
    auto operator<=>(const TaskKey&) const = default;
    bool valid() const {
        return device != 0 && inode != 0 && change_time_ns != 0
            && generation != 0;
    }
};

struct ExportTask {
    TaskKey key;
    std::string source;
    std::string target;
    TransferMode mode = TransferMode::kCopy;
    bool media_scan = false;
};

struct WorkerMetrics {
    std::uint64_t queued = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t overflow = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
};

class ExportWorker final {
public:
    using Transfer = std::function<bool(const ExportTask&)>;

    explicit ExportWorker(std::size_t capacity) : capacity_(capacity) {}
    EnqueueResult Enqueue(ExportTask task);
    bool RunNext(const Transfer& transfer);
    TaskState State(const TaskKey& key) const;
    std::size_t pending() const { return queue_.size(); }
    const WorkerMetrics& metrics() const { return metrics_; }

private:
    std::size_t capacity_ = 0;
    std::deque<TaskKey> queue_;
    std::map<TaskKey, ExportTask> tasks_;
    std::map<TaskKey, TaskState> states_;
    WorkerMetrics metrics_;
};

}  // namespace pathguard::exporting
