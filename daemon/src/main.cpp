#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <limits.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#define PATHGUARD_HAS_INOTIFY 1
#else
#define PATHGUARD_HAS_INOTIFY 0
#endif

#include "pathguard/binary.h"
#include "pathguard/path.h"
#include "pathguard/provenance_server.h"
#include "pathguard/policy.h"
#include "pathguard/rules_control.h"
#include "pathguard/topology.h"

#if defined(PATHGUARD_ANDROID)
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>
#include "pathguard/capabilities.h"
#include "pathguard/directory_resolver.h"
#include "pathguard/mount_executor.h"
#endif

namespace fs = std::filesystem;

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
    if (!result.ok()) {
        std::cerr << result.state.error_code << ": "
                  << result.state.message << '\n';
    }
    std::cout << std::flush;
    std::cerr << std::flush;
}

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
        const int poll_result = poll(&descriptor, 1,
                                     *topology_supported ? -1 : 1000);
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            std::cerr << "inotify poll failed; falling back to polling: errno="
                      << errno << '\n' << std::flush;
            close(fd);
            return false;
        }
        if (poll_result == 0) {
            if (RefreshPendingTopology(reconciler, topology_supported)) {
                LogReconcile("topology", reconciler->Reconcile());
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
    if (compile || self_check) {
        const pathguard::control::ReconcileResult result = reconciler.Reconcile();
        LogReconcile(self_check ? "self_check" : "compile", result);
        if (!result.ok()) return 1;
        std::cout << (self_check ? "ok" : "compiled") << '\n';
        return 0;
    }
    const pathguard::control::ReconcileResult initial = reconciler.Reconcile();
    LogReconcile("initial", initial);
    pathguard::daemon::ProvenanceServer provenance_server(
        (run_directory / "provenance.sock").string(),
        (run_directory / "provenance.wal").string());
#if defined(PATHGUARD_ANDROID)
    if (!provenance_server.Start()) {
        std::cerr << "provenance server unavailable\n" << std::flush;
    }
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
