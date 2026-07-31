#include "pathguard/export_worker.h"

namespace pathguard::exporting {

EnqueueResult ExportWorker::Enqueue(ExportTask task) {
    if (!task.key.valid() || task.source.empty() || task.target.empty()) {
        return EnqueueResult::kInvalid;
    }
    if (states_.contains(task.key)) {
        ++metrics_.duplicates;
        return EnqueueResult::kDuplicate;
    }
    if (capacity_ == 0 || queue_.size() >= capacity_) {
        ++metrics_.overflow;
        return EnqueueResult::kFull;
    }
    const TaskKey key = task.key;
    tasks_.emplace(key, std::move(task));
    states_[key] = TaskState::kQueued;
    queue_.push_back(key);
    ++metrics_.queued;
    return EnqueueResult::kQueued;
}

bool ExportWorker::RunNext(const Transfer& transfer) {
    if (queue_.empty() || !transfer) return false;
    const TaskKey key = queue_.front();
    queue_.pop_front();
    auto task = tasks_.find(key);
    if (task == tasks_.end()) return false;
    states_[key] = TaskState::kRunning;
    const bool complete = transfer(task->second);
    states_[key] = complete ? TaskState::kComplete : TaskState::kFailed;
    if (complete) ++metrics_.completed;
    else ++metrics_.failed;
    return true;
}

TaskState ExportWorker::State(const TaskKey& key) const {
    const auto found = states_.find(key);
    return found == states_.end() ? TaskState::kFailed : found->second;
}

}  // namespace pathguard::exporting
