#include <chrono>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <limits.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/utsname.h>
#include <unistd.h>
#define PATHGUARD_HAS_INOTIFY 1
#else
#define PATHGUARD_HAS_INOTIFY 0
#endif

#include "pathguard/path.h"
#include "pathguard/provider_process_lifecycle.h"
#include "pathguard/hide1_backend.h"
#include "pathguard/rules_contract.h"
#include "pathguard/audit_server.h"
#include "pathguard/rules_control.h"
#include "pathguard/topology.h"

#if defined(PATHGUARD_ANDROID)
#include <signal.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>
#include "pathguard/capabilities.h"
#include "pathguard/directory_resolver.h"
#include "pathguard/mount_executor.h"
#endif

namespace fs = std::filesystem;

#if defined(PATHGUARD_ANDROID)
constexpr int kProviderStartupReconcileAttempts = 30;

static bool TerminateStaleProvider(int pid, void*) {
    return kill(pid, SIGTERM) == 0 || errno == ESRCH;
}

static void ReconcileProviderStartup(const fs::path& run_directory) {
    pathguard::daemon::ProviderProcessReconcileState state;
    for (int attempt = 1; attempt <= kProviderStartupReconcileAttempts;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const pathguard::daemon::ProviderProcessReconcileReport report =
            pathguard::daemon::ReconcileProviderProcesses(
                "/proc", run_directory / "policy.bin", TerminateStaleProvider,
                nullptr, &state);
        std::cout << "provider lifecycle reconcile: attempt=" << attempt
                  << " policy_valid=" << (report.policy_valid ? 1 : 0)
                  << " required=" << (report.provider_required ? 1 : 0)
                  << " targets=" << report.target_processes
                  << " ready=" << report.ready_processes
                  << " stale=" << report.stale_processes
                  << " pending=" << report.pending_processes
                  << " terminated=" << report.terminated_processes
                  << " failed=" << report.failed_processes << '\n' << std::flush;
        if (report.policy_valid && !report.provider_required) return;
    }
}
#endif

static void LogReconcile(const char* phase,
                         const pathguard::control::ReconcileResult& result) {
    std::cout << "rules reconcile phase=" << phase
              << " candidate_sequence=" << result.state.candidate_sequence
              << " active_content_generation="
              << result.state.active_content_generation
              << " deployment_epoch=" << result.state.deployment_epoch
              << " compiled=" << (result.compiled ? 1 : 0)
              << " unchanged=" << (result.unchanged ? 1 : 0)
              << " published=" << (result.published ? 1 : 0) << '\n';
    if (result.hide_updated) std::cout << "hide reconcile=updated\n";
    if (!result.ok() || !result.state.error_code.empty()) {
        std::cerr << result.state.error_code << ": "
                  << result.state.message << '\n';
    }
    std::cout << std::flush;
    std::cerr << std::flush;
}

class HideRuntime final {
public:
    explicit HideRuntime(fs::path module_dir)
        : module_dir_(std::move(module_dir)) {}

    bool Reconcile(const pathguard::rules::RulesBuildResult& built,
                   std::string* error) {
        if (built.hide_rules.empty()) {
            if (backend_) {
                const auto result = backend_->Revoke();
                if (!result.ok()) return Fail(error, result.reason);
            }
            backend_.reset();
            return true;
        }
        if (built.hide_rules.size() != 1) {
            return Fail(error, "hide requires exactly one active hide_rules entry");
        }
        const auto& source = built.hide_rules.front();
        const auto pid = FindTargetPid(source);
        if (!pid.has_value()) {
            if (backend_) {
                const auto result = backend_->Revoke();
                // The kernel revokes a binding when its pinned target exits.
                // The old PID namespace is then gone, so DISABLE/CLEAR may
                // legitimately return ENOENT.  Treat that as completed
                // teardown and discard the in-memory backend.
                if (!result.ok()
                    && result.reason.find("namespace-ioctl-errno=2")
                        == std::string::npos) {
                    return Fail(error, result.reason);
                }
                backend_.reset();
                active_rule_key_.clear();
                active_pid_ = 0;
            }
            return true;
        }
        // Admission is a live trust input.  It must be revalidated before
        // the active fast path so replacing or corrupting the evidence
        // revokes an already-installed binding on the next reconcile tick.
        std::string admission_error;
        const auto admission = ReadAdmission(&admission_error);
        if (!admission.has_value()) {
            if (backend_) {
                const auto revoked = backend_->Revoke();
                if (!revoked.ok()) return Fail(error, revoked.reason);
                backend_.reset();
                active_rule_key_.clear();
                active_pid_ = 0;
            } else {
                // A daemon restart can lose the in-memory backend while the
                // kernel binding remains installed.  Clear that orphaned
                // binding before accepting any future admission.
                auto transport = pathguard::hide1::MakeLinuxTransport(
                    "/dev/pathguard_hide1", *pid);
                if (transport) {
                    const auto disabled = transport->Disable();
                    const auto cleared = disabled.ok()
                        ? transport->Clear() : disabled;
                    if (!cleared.ok()) return Fail(error, cleared.reason);
                }
            }
            return Fail(error, admission_error.empty()
                ? "hide-admission-evidence-invalid" : admission_error);
        }
        const std::string rule_key = source.package + "\n" + source.parent
            + "\n" + source.basename;
        if (backend_ && backend_->state() == pathguard::hide1::BackendState::kActive
            && active_rule_key_ == rule_key && active_pid_ == *pid) {
            if (backend_->Reconcile().ok()) return true;
        }

        auto transport = pathguard::hide1::MakeLinuxTransport(
            "/dev/pathguard_hide1", *pid);
        if (!transport) return Fail(error, "hide-transport-unavailable");
        const std::int32_t target_pid = *pid;
        auto identity_reader = [target_pid]() {
            return pathguard::hide1::ReadProcessIdentity(target_pid);
        };
        auto candidate = std::make_unique<pathguard::hide1::Backend>(
            std::move(transport), std::move(identity_reader));
        if (!candidate->Admit(*admission).ok()) {
            return Fail(error, candidate->error_reason());
        }
        pathguard::PolicyV6 policy;
        pathguard::PolicyPackageV6 package;
        package.package = source.package;
        package.all_processes = source.processes.empty();
        package.processes = source.processes;
        for (const auto user : source.users) {
            if (user >= 0) package.users.push_back(static_cast<std::uint32_t>(user));
        }
        pathguard::PolicySelectorV6 selector;
        selector.match_kind = pathguard::PolicyMatchKind::kLiteralPrefix;
        selector.object_type = pathguard::PolicyObjectType::kAny;
        selector.root = source.parent + "/" + source.basename;
        package.selectors.push_back(std::move(selector));
        pathguard::PolicyActionV6 action;
        action.selector_index = 0;
        action.kind = pathguard::PolicyActionKind::kDeny;
        action.domain = pathguard::PolicyExecutionDomain::kCompleteVfs;
        action.required_capabilities = pathguard::kCapabilityFuseCompletePath;
        action.required_operations = pathguard::kCompleteVfsOperationsV1;
        package.actions.push_back(action);
        policy.packages.push_back(std::move(package));

        if (backend_) {
            const auto stopped = backend_->Revoke();
            if (!stopped.ok()) return Fail(error, stopped.reason);
        }
        if (!candidate->Apply(policy, *admission).ok()) {
            return Fail(error, candidate->error_reason());
        }
        backend_ = std::move(candidate);
        active_rule_key_ = rule_key;
        active_pid_ = *pid;
        return true;
    }

private:
    static bool Fail(std::string* error, std::string message) {
        if (error) *error = std::move(message);
        return false;
    }

    std::optional<pathguard::hide1::Admission> ReadAdmission(
            std::string* error) const {
#if defined(__linux__)
        const auto boot_values = ReadKeyValues(module_dir_ / "run/boot-state");
        if (!boot_values) {
            if (error) *error = "hide-boot-state-unavailable";
            return std::nullopt;
        }

        struct utsname uts {};
        if (uname(&uts) != 0) {
            if (error) *error = "hide-kernel-release-unavailable";
            return std::nullopt;
        }
        std::ifstream boot_id_file("/proc/sys/kernel/random/boot_id");
        std::string current_boot_id;
        if (!std::getline(boot_id_file, current_boot_id)
            && current_boot_id.empty()) {
            if (error) *error = "hide-boot-id-unavailable";
            return std::nullopt;
        }

        const auto get = [](const auto& values, const char* key) {
            const auto it = values.find(key);
            return it == values.end() ? std::string{} : it->second;
        };
        pathguard::hide1::DeviceProfile runtime;
        runtime.device = get(*boot_values, "device");
        runtime.arch = get(*boot_values, "arch");
        runtime.fingerprint = get(*boot_values, "fingerprint");
        runtime.kernel_release = uts.release;
        runtime.kmi = get(*boot_values, "kmi");
        runtime.module_sha256 = get(*boot_values, "module_sha256");
        runtime.boot_id = current_boot_id;
        runtime.module_live = fs::exists("/sys/module/pathguard_hide1")
            && fs::exists("/dev/pathguard_hide1");
        if (get(*boot_values, "boot_id") != runtime.boot_id
            || get(*boot_values, "kernel") != runtime.kernel_release) {
            if (error) *error = "hide-boot-state-stale";
            return std::nullopt;
        }

        // A fixed-device profile proves only that the package was built for
        // this device.  It is not a completed Hide admission.  The daemon
        // must consume the evidence artifact produced by admit_hide1.ps1;
        // after reboot the boot_id check below invalidates stale evidence.
        std::ifstream admission_input(module_dir_ / "run/admission.json",
                                      std::ios::binary);
        if (!admission_input) {
            if (error) *error = "hide-admission-file-unavailable";
            return std::nullopt;
        }
        const std::string admission_json{
            std::istreambuf_iterator<char>(admission_input),
            std::istreambuf_iterator<char>()};
        std::string parse_error;
        auto admission = pathguard::hide1::ReadAdmissionJson(
            admission_json, &parse_error);
        if (!admission.has_value()) {
            if (error) *error = parse_error.empty()
                ? "hide-admission-json-invalid" : parse_error;
            return std::nullopt;
        }
        std::vector<std::string> mismatches;
        const auto compare = [&mismatches](const char* name,
                                            const std::string& expected,
                                            const std::string& actual) {
            if (expected != actual) {
                mismatches.emplace_back(std::string(name) + "=" + actual);
            }
        };
        compare("boot_id", admission->boot_id, runtime.boot_id);
        compare("device", admission->device, runtime.device);
        compare("arch", admission->arch, runtime.arch);
        compare("fingerprint", admission->fingerprint, runtime.fingerprint);
        compare("kernel", admission->kernel_release, runtime.kernel_release);
        compare("kmi", admission->kmi, runtime.kmi);
        compare("module_sha256", admission->module_sha256,
                runtime.module_sha256);
        if (!runtime.module_live) mismatches.emplace_back("module_live=false");
        if (admission->status_shadow_mode != 0) {
            mismatches.emplace_back("shadow_mode="
                                    + std::to_string(admission->status_shadow_mode));
        }
        if (!mismatches.empty()) {
            if (error) {
                *error = "hide-admission-runtime-mismatch:";
                for (std::size_t i = 0; i < mismatches.size(); ++i) {
                    if (i != 0) *error += ",";
                    *error += mismatches[i];
                }
            }
            return std::nullopt;
        }
        return admission;
#else
        if (error) *error = "hide-admission-platform-unsupported";
        return std::nullopt;
#endif
    }

    static std::optional<std::map<std::string, std::string>> ReadKeyValues(
            const fs::path& path) {
        std::ifstream input(path);
        if (!input) return std::nullopt;
        std::map<std::string, std::string> values;
        std::string line;
        while (std::getline(input, line)) {
            const auto separator = line.find('=');
            if (separator == std::string::npos || separator == 0) continue;
            values.emplace(line.substr(0, separator), line.substr(separator + 1));
        }
        if (input.bad()) return std::nullopt;
        return values;
    }

    static bool ProcessNameMatches(const std::string& cmdline,
                                   const pathguard::rules::CanonicalHideRuleV2& rule) {
        if (cmdline.empty()) return false;
        const auto matches = [&](const std::string& wanted) {
            return wanted == "*" || cmdline == wanted
                || (cmdline.rfind(wanted + ":", 0) == 0);
        };
        if (!rule.processes.empty()) {
            for (const auto& process : rule.processes) {
                if (matches(process)) return true;
            }
            return false;
        }
        return matches(rule.package);
    }

    static std::optional<std::int32_t> FindTargetPid(
            const pathguard::rules::CanonicalHideRuleV2& rule) {
        std::error_code error;
        for (const auto& entry : fs::directory_iterator("/proc", error)) {
            if (error || !entry.is_directory(error)) continue;
            const std::string name = entry.path().filename().string();
            std::int32_t pid = 0;
            const auto parsed = std::from_chars(name.data(), name.data() + name.size(), pid);
            if (parsed.ec != std::errc{} || parsed.ptr != name.data() + name.size() || pid <= 0) continue;
            std::ifstream cmdline(entry.path() / "cmdline", std::ios::binary);
            if (!cmdline) continue;
            std::string command((std::istreambuf_iterator<char>(cmdline)), {});
            const auto nul = command.find('\0');
            if (nul != std::string::npos) command.resize(nul);
            if (ProcessNameMatches(command, rule)) return pid;
        }
        return std::nullopt;
    }

    fs::path module_dir_;
    std::unique_ptr<pathguard::hide1::Backend> backend_;
    std::string active_rule_key_;
    std::int32_t active_pid_ = 0;
};

static bool ReadAll(const fs::path& path, std::string* output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    *output = std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

static bool ProbeStorageTopology() {
    std::string mountinfo;
    if (!ReadAll("/proc/self/mountinfo", &mountinfo)) {
        std::cerr << "storage topology unsupported: cannot read mountinfo\n";
        return false;
    }
    pathguard::StorageTopology topology;
    std::string error;
    if (!pathguard::ParseMountInfo(mountinfo, &topology, &error)) {
        std::cerr << "storage topology unsupported: " << error << '\n';
        return false;
    }
    std::cout << "storage topology ready: users=" << topology.mounts.size() << '\n';
    for (const pathguard::StorageTopologyMount& mount : topology.mounts) {
        std::cout << "storage topology user=" << mount.user_id
                  << " mount_id=" << mount.mount_id
                  << " visible=" << mount.visible_root
                  << " backend=" << mount.backend_root
                  << " fs=" << mount.filesystem_type
                  << " aliases=" << mount.aliases.size() << '\n';
    }
    std::cout << std::flush;
    return true;
}

#if defined(PATHGUARD_ANDROID)
static bool ProbeProcFdMount(const char* source_root_path, const char* source_path,
                             const char* target_root_path, const char* target_path,
                             bool force_component_walk) {
    pathguard::DirectoryResolveResult source_root =
        pathguard::OpenDirectoryRoot(source_root_path);
    pathguard::DirectoryResolveResult target_root =
        pathguard::OpenDirectoryRoot(target_root_path);
    if (source_root.fd < 0 || target_root.fd < 0) {
        std::cerr << "mount probe root open failed: source_errno=" << source_root.error
                  << " target_errno=" << target_root.error << '\n';
        if (source_root.fd >= 0) close(source_root.fd);
        if (target_root.fd >= 0) close(target_root.fd);
        return false;
    }
    pathguard::DirectoryResolveResult source = pathguard::ResolveDirectoryBeneath(
        source_root.fd, source_path, force_component_walk);
    pathguard::DirectoryResolveResult target = pathguard::ResolveDirectoryBeneath(
        target_root.fd, target_path, force_component_walk);
    close(source_root.fd);
    close(target_root.fd);
    if (source.fd < 0 || target.fd < 0) {
        std::cerr << "mount probe resolve failed: source_errno=" << source.error
                  << " target_errno=" << target.error << '\n';
        if (source.fd >= 0) close(source.fd);
        if (target.fd >= 0) close(target.fd);
        return false;
    }
    if (unshare(CLONE_NEWNS) != 0
        || mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
        const int error = errno;
        close(source.fd);
        close(target.fd);
        std::cerr << "mount probe namespace isolation failed: errno=" << error << '\n';
        return false;
    }

    char source_absolute[PATH_MAX]{};
    char target_absolute[PATH_MAX]{};
    char source_proc_fd[64]{};
    char target_proc_fd[64]{};
    const int source_written = snprintf(
        source_absolute, sizeof(source_absolute), "%s/%s", source_root_path, source_path);
    const int target_written = snprintf(
        target_absolute, sizeof(target_absolute), "%s/%s", target_root_path, target_path);
    const int source_fd_written = snprintf(
        source_proc_fd, sizeof(source_proc_fd), "/proc/self/fd/%d", source.fd);
    const int target_fd_written = snprintf(
        target_proc_fd, sizeof(target_proc_fd), "/proc/self/fd/%d", target.fd);
    if (source_written < 0 || static_cast<size_t>(source_written) >= sizeof(source_absolute)
        || target_written < 0 || static_cast<size_t>(target_written) >= sizeof(target_absolute)
        || source_fd_written < 0
        || static_cast<size_t>(source_fd_written) >= sizeof(source_proc_fd)
        || target_fd_written < 0
        || static_cast<size_t>(target_fd_written) >= sizeof(target_proc_fd)) {
        close(source.fd);
        close(target.fd);
        std::cerr << "mount probe path construction failed\n";
        return false;
    }

    const auto probe_bind = [&](const char* label, const char* mount_source,
                                const char* mount_target) {
        const int mount_error = mount(
            mount_source, mount_target, nullptr, MS_BIND, nullptr) == 0 ? 0 : errno;
        const int unmount_error = mount_error == 0
            ? (umount2(target_absolute, MNT_DETACH) == 0 ? 0 : errno)
            : 0;
        std::cout << "proc fd mount case=" << label
                  << " mount_errno=" << mount_error
                  << " unmount_errno=" << unmount_error << '\n';
        return mount_error == 0 && unmount_error == 0;
    };

    const bool string_string = probe_bind(
        "string_string", source_absolute, target_absolute);
    const bool fd_string = probe_bind(
        "fd_string", source_proc_fd, target_absolute);
    const bool string_fd = probe_bind(
        "string_fd", source_absolute, target_proc_fd);
    const bool fd_fd = probe_bind(
        "fd_fd", source_proc_fd, target_proc_fd);
    const int move_mount_error = pathguard::MoveMountDirectoryFds(source.fd, target.fd);
    const int move_unmount_error = move_mount_error == 0
        ? (umount2(target_absolute, MNT_DETACH) == 0 ? 0 : errno)
        : 0;
    std::cout << "proc fd mount case=open_tree_move_mount"
              << " mount_errno=" << move_mount_error
              << " unmount_errno=" << move_unmount_error << '\n';
    const bool open_tree_move_mount =
        move_mount_error == 0 && move_unmount_error == 0;
    const std::uint64_t capabilities = source.capability | target.capability
        | (fd_fd ? pathguard::kCapabilityProcFdMount : 0)
        | (open_tree_move_mount ? pathguard::kCapabilityOpenTreeMoveMount : 0);
    close(source.fd);
    close(target.fd);
    if (!string_string || (!fd_fd && !open_tree_move_mount)) {
        std::cerr << "mount probe compatibility incomplete: capability="
                  << capabilities << '\n';
        return false;
    }
    std::cout << "proc fd mount ready: capability=" << capabilities << '\n';
    return true;
}
#endif

static pathguard::rules::DeviceSnapshot MakeDeviceSnapshot(
        bool topology_supported) {
    pathguard::rules::DeviceSnapshot snapshot;
    snapshot.mount.primitives = pathguard::kCapabilityOpenTreeMoveMount
        | pathguard::kCapabilityProcFdMount
        | pathguard::kCapabilityStringBindMount;
    snapshot.mount.strict_actions = pathguard::kMountActionRedirect
        | pathguard::kMountActionDenyAnchor;
    snapshot.mount.legacy_actions = pathguard::kMountActionRedirect;
    snapshot.provider_supported = topology_supported;
    snapshot.topology_supported = topology_supported;
    snapshot.capability_generation = 1;
    snapshot.topology_generation = topology_supported ? 1 : 0;
    return snapshot;
}

static bool RefreshPendingTopology(pathguard::control::Reconciler* reconciler,
                                   bool* topology_supported) {
#if defined(PATHGUARD_ANDROID)
    if (reconciler == nullptr || topology_supported == nullptr
        || *topology_supported || !ProbeStorageTopology()) {
        return false;
    }
    *topology_supported = true;
    reconciler->SetDeviceSnapshot(MakeDeviceSnapshot(true));
    return true;
#else
    (void)reconciler;
    (void)topology_supported;
    return false;
#endif
}

static void RunPollingLoop(pathguard::control::Reconciler* reconciler,
                           bool* topology_supported) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const bool topology_changed = RefreshPendingTopology(
            reconciler, topology_supported);
        LogReconcile(topology_changed ? "topology" : "poll",
                     reconciler->Reconcile());
    }
}

#if PATHGUARD_HAS_INOTIFY
static bool IsRulesEvent(const inotify_event* event, const std::string& file_name) {
    if (event->len == 0 || file_name != event->name) return false;
    constexpr std::uint32_t kRelevantMask = IN_CLOSE_WRITE | IN_MOVED_TO
        | IN_CREATE | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM;
    return (event->mask & kRelevantMask) != 0;
}

static bool RunInotifyLoop(const fs::path& config_directory,
                           pathguard::control::Reconciler* reconciler,
                           bool* topology_supported) {
    const std::string file_name = pathguard::control::kRulesFileName;
    const int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (fd < 0) {
        std::cerr << "inotify initialization failed; falling back to polling: errno="
                  << errno << '\n' << std::flush;
        return false;
    }
    const int watch = inotify_add_watch(
        fd, config_directory.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE
            | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM);
    if (watch < 0) {
        std::cerr << "inotify watch failed; falling back to polling: errno="
                  << errno << '\n' << std::flush;
        close(fd);
        return false;
    }
    std::cout << "inotify watching: " << config_directory.string()
              << "\n" << std::flush;

    std::vector<char> buffer(16 * (sizeof(inotify_event) + NAME_MAX + 1));
    while (true) {
        pollfd descriptor{fd, POLLIN, 0};
        const int poll_result = poll(&descriptor, 1, 1000);
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            std::cerr << "inotify poll failed; falling back to polling: errno="
                      << errno << '\n' << std::flush;
            close(fd);
            return false;
        }
        if (poll_result == 0) {
            const bool topology_changed = RefreshPendingTopology(
                reconciler, topology_supported);
            const auto result = reconciler->Reconcile();
            // Reconcile on every idle tick so a manually loaded LKM, a target
            // process restart, or a revoked binding is observed without
            // requiring a rules.toml write event.
            if (topology_changed || !result.unchanged || result.hide_updated
                || !result.ok()) {
                LogReconcile(topology_changed ? "topology" : "poll", result);
            }
            continue;
        }
        if ((descriptor.revents & POLLIN) == 0) continue;

        bool config_changed = false;
        while (true) {
            const ssize_t count = read(fd, buffer.data(), buffer.size());
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (count <= 0) {
                close(fd);
                return false;
            }
            for (ssize_t offset = 0; offset < count;) {
                const auto* event = reinterpret_cast<const inotify_event*>(
                    buffer.data() + offset);
                if ((event->mask & IN_Q_OVERFLOW) != 0
                    || IsRulesEvent(event, file_name)) {
                    config_changed = true;
                }
                offset += sizeof(inotify_event) + event->len;
            }
        }
        if (config_changed) {
            RefreshPendingTopology(reconciler, topology_supported);
            LogReconcile("inotify", reconciler->Reconcile());
        }
    }
}
#endif

int main(int argc, char** argv) {
    fs::path module_dir = ".";
    bool compile = false;
    bool self_check = false;
    bool probe_topology = false;
#if defined(PATHGUARD_ANDROID)
    const char* probe_directory_root = nullptr;
    const char* probe_directory_path = nullptr;
    const char* probe_mount_source_root = nullptr;
    const char* probe_mount_source_path = nullptr;
    const char* probe_mount_target_root = nullptr;
    const char* probe_mount_target_path = nullptr;
    bool force_component_walk = false;
#endif
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--module-dir" && i + 1 < argc) module_dir = argv[++i];
        else if (arg == "--compile") compile = true;
        else if (arg == "--self-check") self_check = true;
        else if (arg == "--probe-topology") probe_topology = true;
#if defined(PATHGUARD_ANDROID)
        else if (arg == "--probe-directory" && i + 2 < argc) {
            probe_directory_root = argv[++i];
            probe_directory_path = argv[++i];
        } else if (arg == "--probe-proc-fd-mount" && i + 4 < argc) {
            probe_mount_source_root = argv[++i];
            probe_mount_source_path = argv[++i];
            probe_mount_target_root = argv[++i];
            probe_mount_target_path = argv[++i];
        } else if (arg == "--force-component-walk") {
            force_component_walk = true;
        }
#endif
    }
    if (probe_topology) return ProbeStorageTopology() ? 0 : 1;
#if defined(PATHGUARD_ANDROID)
    if (probe_mount_source_root != nullptr) {
        return ProbeProcFdMount(
            probe_mount_source_root, probe_mount_source_path,
            probe_mount_target_root, probe_mount_target_path,
            force_component_walk)
            ? 0
            : 1;
    }
    if (probe_directory_root != nullptr) {
        pathguard::DirectoryResolveResult root =
            pathguard::OpenDirectoryRoot(probe_directory_root);
        if (root.fd < 0) {
            std::cerr << "directory root open failed: errno=" << root.error << '\n';
            return 1;
        }
        pathguard::DirectoryResolveResult resolved = pathguard::ResolveDirectoryBeneath(
            root.fd, probe_directory_path, force_component_walk);
        close(root.fd);
        if (resolved.fd < 0) {
            std::cerr << "directory resolve failed: errno=" << resolved.error << '\n';
            return 1;
        }
        std::cout << "directory resolve ready: capability=" << resolved.capability
                  << " fd=" << resolved.fd << '\n';
        close(resolved.fd);
        return 0;
    }
#endif
    const fs::path config_directory = module_dir / "config";
    const fs::path run_directory = module_dir / "run";
    fs::create_directories(run_directory);
#if defined(PATHGUARD_ANDROID)
    bool topology_supported = ProbeStorageTopology();
#else
    bool topology_supported = true;
#endif
    pathguard::control::Reconciler reconciler(
        config_directory, run_directory, pathguard::rules::RulesLimits{},
        MakeDeviceSnapshot(topology_supported));
    HideRuntime hide_runtime(module_dir);
    reconciler.SetHideReconcileCallback(
        [&hide_runtime](const pathguard::rules::RulesBuildResult& built,
                        std::string* error) {
            return hide_runtime.Reconcile(built, error);
        });
    if (compile || self_check) {
        const pathguard::control::ReconcileResult result = reconciler.Reconcile();
        LogReconcile(self_check ? "self_check" : "compile", result);
        if (!result.ok()) return 1;
        std::cout << (self_check ? "ok" : "compiled") << '\n';
        return 0;
    }
    const pathguard::control::ReconcileResult initial = reconciler.Reconcile();
    LogReconcile("initial", initial);
    pathguard::daemon::AuditServer audit_server(
        (run_directory / "audit.sock").string(),
        (run_directory / "audit-v1.wal").string());
#if defined(PATHGUARD_ANDROID)
    if (!audit_server.Start()) {
        std::cerr << "audit server unavailable\n" << std::flush;
    }
    std::thread(ReconcileProviderStartup, run_directory).detach();
#endif
    std::cout << "pathguardd ready; module-dir=" << module_dir.string() << '\n'
              << std::flush;
#if PATHGUARD_HAS_INOTIFY
    if (RunInotifyLoop(config_directory, &reconciler,
                       &topology_supported)) return 0;
#endif
    RunPollingLoop(&reconciler, &topology_supported);
    return 0;
}
