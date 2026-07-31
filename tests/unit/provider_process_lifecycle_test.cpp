#include "pathguard/provider_process_lifecycle.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pathguard/pattern.h"
#include "pathguard/policy_v6.h"

namespace fs = std::filesystem;

namespace {

void Write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    assert(output.good());
}

std::vector<std::uint8_t> Policy(bool provider_required) {
    pathguard::PolicyV6 policy;
    pathguard::PolicyPackageV6 package;
    package.package = "example.app";
    package.users = {0};
    pathguard::PolicySelectorV6 selector;
    selector.match_kind = pathguard::PolicyMatchKind::kGlob;
    selector.object_type = pathguard::PolicyObjectType::kFile;
    selector.root = "Download";
    const auto program = pathguard::pattern::CompilePattern("**");
    assert(program.ok());
    selector.base_pattern = *program.program;
    package.selectors.push_back(std::move(selector));
    pathguard::PolicyActionV6 action;
    action.selector_index = 0;
    action.rule_id = 1;
    action.kind = pathguard::PolicyActionKind::kRedirect;
    action.domain = provider_required
        ? pathguard::PolicyExecutionDomain::kProvider
        : pathguard::PolicyExecutionDomain::kAppPath;
    action.target = "Download/redirect";
    package.actions.push_back(std::move(action));
    policy.packages.push_back(std::move(package));
    std::vector<std::uint8_t> bytes;
    std::string error;
    assert(pathguard::EncodePolicyV6(policy, &bytes, &error));
    return bytes;
}

void WritePolicy(const fs::path& path, bool provider_required) {
    const auto bytes = Policy(provider_required);
    Write(path, std::string(reinterpret_cast<const char*>(bytes.data()),
                            bytes.size()));
}

void WriteProcess(const fs::path& proc_root, int pid, std::string name,
                  const std::string& maps) {
    const fs::path process = proc_root / std::to_string(pid);
    name.push_back('\0');
    Write(process / "cmdline", name);
    Write(process / "maps", maps);
}

struct Signals {
    std::vector<int> pids;
};

bool CaptureSignal(int pid, void* context) {
    static_cast<Signals*>(context)->pids.push_back(pid);
    return true;
}

}  // namespace

int main() {
    using namespace pathguard::daemon;
    const fs::path root = fs::temp_directory_path()
        / "pathguard-provider-process-lifecycle-test";
    std::error_code error;
    fs::remove_all(root, error);
    const fs::path proc_root = root / "proc";
    const fs::path policy_path = root / "run" / "policy.bin";

    WritePolicy(policy_path, true);
    WriteProcess(proc_root, 101, "com.android.externalstorage", "libart.so\n");
    WriteProcess(proc_root, 102, "com.android.providers.media.module",
                 "libart.so\n" + policy_path.string() + "\n");
    WriteProcess(proc_root, 103, "unrelated.app", "libart.so\n");
    WriteProcess(proc_root, 104, "com.android.externalstorage:remote",
                 "libart.so\n");
    WriteProcess(proc_root, 105, "com.android.providers.media.module",
                 policy_path.string() + " (deleted)\n");
    const fs::path unreadable = proc_root / "106";
    Write(unreadable / "cmdline",
          std::string("com.android.externalstorage\0", 28));

    Signals signals;
    const ProviderProcessReconcileReport report = ReconcileProviderProcesses(
        proc_root, policy_path, CaptureSignal, &signals);
    assert(report.policy_valid);
    assert(report.provider_required);
    assert(report.target_processes == 4);
    assert(report.ready_processes == 1);
    assert(report.stale_processes == 2);
    assert(report.terminated_processes == 2);
    assert(report.failed_processes == 1);
    assert(signals.pids == std::vector<int>({101, 105}));

    const fs::path delayed_root = root / "delayed-proc";
    ProviderProcessReconcileState state;
    signals.pids.clear();
    const auto before_start = ReconcileProviderProcesses(
        delayed_root, policy_path, CaptureSignal, &signals, &state);
    assert(before_start.target_processes == 0);
    fs::create_directories(delayed_root);
    WriteProcess(delayed_root, 201, "com.android.externalstorage", "libart.so\n");
    const auto first_observation = ReconcileProviderProcesses(
        delayed_root, policy_path, CaptureSignal, &signals, &state);
    assert(first_observation.stale_processes == 1);
    assert(first_observation.pending_processes == 1);
    assert(first_observation.terminated_processes == 0);
    assert(signals.pids.empty());
    const auto confirmed_stale = ReconcileProviderProcesses(
        delayed_root, policy_path, CaptureSignal, &signals, &state);
    assert(confirmed_stale.stale_processes == 1);
    assert(confirmed_stale.pending_processes == 0);
    assert(confirmed_stale.terminated_processes == 1);
    assert(signals.pids == std::vector<int>({201}));

    signals.pids.clear();
    WriteProcess(delayed_root, 201, "com.android.externalstorage",
                 policy_path.string() + "\n");
    const auto became_ready = ReconcileProviderProcesses(
        delayed_root, policy_path, CaptureSignal, &signals, &state);
    assert(became_ready.ready_processes == 1);
    assert(became_ready.stale_processes == 0);
    assert(state.stale_process_ids.empty());
    assert(signals.pids.empty());

    WritePolicy(policy_path, false);
    signals.pids.clear();
    const auto inactive = ReconcileProviderProcesses(
        proc_root, policy_path, CaptureSignal, &signals);
    assert(inactive.policy_valid);
    assert(!inactive.provider_required);
    assert(inactive.target_processes == 0);
    assert(signals.pids.empty());

    Write(policy_path, "invalid-policy");
    signals.pids.clear();
    const auto invalid = ReconcileProviderProcesses(
        proc_root, policy_path, CaptureSignal, &signals);
    assert(!invalid.policy_valid);
    assert(!invalid.provider_required);
    assert(signals.pids.empty());

    fs::remove_all(root, error);
    return 0;
}
