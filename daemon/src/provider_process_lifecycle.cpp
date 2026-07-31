#include "pathguard/provider_process_lifecycle.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/policy_v6.h"

namespace pathguard::daemon {
namespace {

bool ReadAll(const std::filesystem::path& path, std::string* output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    *output = std::string(std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool ReadPolicy(const std::filesystem::path& path,
                std::vector<std::uint8_t>* output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    output->assign(std::istreambuf_iterator<char>(input),
                   std::istreambuf_iterator<char>());
    return !output->empty() && (input.good() || input.eof());
}

bool RequiresProvider(const pathguard::PolicyV6& policy) {
    for (const auto& package : policy.packages) {
        for (const auto& action : package.actions) {
            if (action.domain == pathguard::PolicyExecutionDomain::kProvider) {
                return true;
            }
        }
    }
    return false;
}

bool IsTargetProcess(std::string_view process_name) {
    return process_name == "com.android.externalstorage"
        || process_name == "com.android.providers.media.module";
}

bool ParsePid(std::string_view value, int* output) {
    if (value.empty() || output == nullptr) return false;
    int pid = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), pid);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
        || pid <= 0) return false;
    *output = pid;
    return true;
}

bool MapsPolicy(const std::filesystem::path& maps_path,
                const std::filesystem::path& policy_path,
                bool* readable) {
    std::ifstream maps(maps_path);
    if (!maps) {
        *readable = false;
        return false;
    }
    *readable = true;
    const std::string expected = policy_path.string();
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t position = line.find(expected);
        if (position == std::string::npos) continue;
        const std::size_t end = position + expected.size();
        if (end == line.size() || line.compare(end, 10, " (deleted)") != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

ProviderProcessReconcileReport ReconcileProviderProcesses(
        const std::filesystem::path& proc_root,
        const std::filesystem::path& policy_path,
        TerminateProviderProcess terminate,
        void* terminate_context,
        ProviderProcessReconcileState* state) {
    ProviderProcessReconcileReport report;
    std::vector<int> stale_process_ids;
    std::vector<std::uint8_t> bytes;
    pathguard::PolicyV6 policy;
    if (!ReadPolicy(policy_path, &bytes)
        || !pathguard::DecodePolicyV6(bytes, &policy).ok) {
        if (state != nullptr) state->stale_process_ids.clear();
        return report;
    }
    report.policy_valid = true;
    report.provider_required = RequiresProvider(policy);
    if (!report.provider_required) {
        if (state != nullptr) state->stale_process_ids.clear();
        return report;
    }

    std::error_code iteration_error;
    for (std::filesystem::directory_iterator iterator(proc_root, iteration_error), end;
         !iteration_error && iterator != end; iterator.increment(iteration_error)) {
        std::error_code type_error;
        if (!iterator->is_directory(type_error) || type_error) continue;
        int pid = 0;
        if (!ParsePid(iterator->path().filename().string(), &pid)) continue;
        std::string cmdline;
        if (!ReadAll(iterator->path() / "cmdline", &cmdline)) continue;
        const std::size_t terminator = cmdline.find('\0');
        if (terminator != std::string::npos) cmdline.resize(terminator);
        if (!IsTargetProcess(cmdline)) continue;
        ++report.target_processes;
        bool maps_readable = false;
        if (MapsPolicy(iterator->path() / "maps", policy_path,
                       &maps_readable)) {
            ++report.ready_processes;
            continue;
        }
        if (!maps_readable) {
            ++report.failed_processes;
            continue;
        }
        ++report.stale_processes;
        stale_process_ids.push_back(pid);
        const bool confirmed = state == nullptr
            || std::find(state->stale_process_ids.begin(),
                         state->stale_process_ids.end(), pid)
                != state->stale_process_ids.end();
        if (!confirmed) {
            ++report.pending_processes;
            continue;
        }
        if (terminate != nullptr && terminate(pid, terminate_context)) {
            ++report.terminated_processes;
        } else {
            ++report.failed_processes;
        }
    }
    if (state != nullptr) {
        state->stale_process_ids = std::move(stale_process_ids);
    }
    return report;
}

}  // namespace pathguard::daemon
