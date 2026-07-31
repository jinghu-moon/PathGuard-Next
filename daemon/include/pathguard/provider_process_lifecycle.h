#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace pathguard::daemon {

using TerminateProviderProcess = bool (*)(int pid, void* context);

struct ProviderProcessReconcileReport {
    bool policy_valid = false;
    bool provider_required = false;
    std::size_t target_processes = 0;
    std::size_t ready_processes = 0;
    std::size_t stale_processes = 0;
    std::size_t pending_processes = 0;
    std::size_t terminated_processes = 0;
    std::size_t failed_processes = 0;
};

struct ProviderProcessReconcileState {
    std::vector<int> stale_process_ids;
};

ProviderProcessReconcileReport ReconcileProviderProcesses(
    const std::filesystem::path& proc_root,
    const std::filesystem::path& policy_path,
    TerminateProviderProcess terminate,
    void* terminate_context = nullptr,
    ProviderProcessReconcileState* state = nullptr);

}  // namespace pathguard::daemon
