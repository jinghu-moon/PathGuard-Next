#include <chrono>
#include <cstdint>
#include <cstring>
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
#include "pathguard/policy.h"
#include "pathguard/topology.h"
#include "pathguard/validation.h"

#if defined(PATHGUARD_ANDROID)
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>
#include "pathguard/capabilities.h"
#include "pathguard/directory_resolver.h"
#include "pathguard/mount_executor.h"
#endif

namespace fs = std::filesystem;

using PerfClock = std::chrono::steady_clock;

static std::uint64_t NowNs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        PerfClock::now().time_since_epoch()).count());
}

static std::uint64_t ElapsedNs(std::uint64_t start) {
    const std::uint64_t now = NowNs();
    return now >= start ? now - start : 0;
}

static std::uint64_t NsToUs(std::uint64_t value) { return value / 1000ULL; }

struct CompilePerf {
    std::uint64_t parse_ns = 0;
    std::uint64_t validate_ns = 0;
    std::uint64_t encode_ns = 0;
    std::uint64_t compare_ns = 0;
    std::uint64_t publish_ns = 0;
    bool unchanged = false;
    bool published = false;
};

static void LogCompilePerf(const char* phase, std::uint64_t read_ns,
                           std::uint64_t debounce_ns, std::uint64_t stable_read_ns,
                           const CompilePerf& perf) {
    std::cout << "perf compile phase=" << phase
              << " read_us=" << NsToUs(read_ns)
              << " debounce_us=" << NsToUs(debounce_ns)
              << " stable_read_us=" << NsToUs(stable_read_ns)
              << " parse_us=" << NsToUs(perf.parse_ns)
              << " validate_us=" << NsToUs(perf.validate_ns)
              << " encode_us=" << NsToUs(perf.encode_ns)
              << " compare_us=" << NsToUs(perf.compare_ns)
              << " publish_us=" << NsToUs(perf.publish_ns)
              << " unchanged=" << (perf.unchanged ? 1 : 0)
              << " published=" << (perf.published ? 1 : 0)
              << '\n' << std::flush;
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

static bool CompileText(const std::string& text, const fs::path& output,
                        CompilePerf* perf, std::string* error) {
    if (perf == nullptr) return false;
    *perf = {};
    pathguard::PolicyDocument document;
    pathguard::ParseError parse_error;
    const std::uint64_t parse_started = NowNs();
    if (!pathguard::ParseRulesIni(text, &document, &parse_error)) {
        perf->parse_ns = ElapsedNs(parse_started);
        *error = "line " + std::to_string(parse_error.line) + ": " + parse_error.message;
        return false;
    }
    perf->parse_ns = ElapsedNs(parse_started);
    const std::uint64_t validate_started = NowNs();
    for (auto& app : document.apps) {
        if (!pathguard::ValidatePolicy(&app, &parse_error)) {
            perf->validate_ns = ElapsedNs(validate_started);
            *error = "line " + std::to_string(parse_error.line) + ": " + parse_error.message;
            return false;
        }
    }
    perf->validate_ns = ElapsedNs(validate_started);
    std::vector<std::uint8_t> bytes;
    const std::uint64_t encode_started = NowNs();
    if (!pathguard::EncodePolicy(document, &bytes, &parse_error)) {
        perf->encode_ns = ElapsedNs(encode_started);
        *error = parse_error.message;
        return false;
    }
    perf->encode_ns = ElapsedNs(encode_started);
    const std::uint64_t compare_started = NowNs();
    std::string current;
    if (ReadAll(output, &current)
        && current.size() == bytes.size()
        && std::memcmp(current.data(), bytes.data(), bytes.size()) == 0) {
        perf->compare_ns = ElapsedNs(compare_started);
        perf->unchanged = true;
        return true;
    }
    perf->compare_ns = ElapsedNs(compare_started);
    const std::uint64_t publish_started = NowNs();
    fs::create_directories(output.parent_path());
    const fs::path temporary = output.string() + ".tmp";
    { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); if (!file) { perf->publish_ns = ElapsedNs(publish_started); *error = "cannot create policy.bin.tmp"; return false; } file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    std::error_code ec;
    fs::rename(temporary, output, ec);
    if (ec) { fs::remove(output, ec); fs::rename(temporary, output, ec); }
    perf->publish_ns = ElapsedNs(publish_started);
    if (ec) { *error = "cannot publish policy.bin: " + ec.message(); return false; }
    perf->published = true;
    return true;
}

static bool CompileFile(const fs::path& config, const fs::path& output,
                        CompilePerf* perf, std::uint64_t* read_ns,
                        std::string* error) {
    const std::uint64_t read_started = NowNs();
    std::string text;
    if (!ReadAll(config, &text)) {
        if (read_ns != nullptr) *read_ns = ElapsedNs(read_started);
        *error = "cannot read rules.ini";
        return false;
    }
    if (read_ns != nullptr) *read_ns = ElapsedNs(read_started);
    return CompileText(text, output, perf, error);
}

static void ReloadIfChanged(const fs::path& config, const fs::path& policy,
                            std::string* active_config,
                            std::string* rejected_config) {
    const std::uint64_t reload_started = NowNs();
    const std::uint64_t candidate_read_started = NowNs();
    std::string candidate;
    if (!ReadAll(config, &candidate)
        || candidate == *active_config
        || candidate == *rejected_config) {
        return;
    }
    const std::uint64_t candidate_read_ns = ElapsedNs(candidate_read_started);

    const std::uint64_t debounce_started = NowNs();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const std::uint64_t debounce_ns = ElapsedNs(debounce_started);
    const std::uint64_t stable_read_started = NowNs();
    std::string stable_candidate;
    if (!ReadAll(config, &stable_candidate) || stable_candidate != candidate) {
        return;
    }
    const std::uint64_t stable_read_ns = ElapsedNs(stable_read_started);

    std::string error;
    CompilePerf perf;
    if (CompileText(stable_candidate, policy, &perf, &error)) {
        *active_config = std::move(stable_candidate);
        rejected_config->clear();
        LogCompilePerf("reload", candidate_read_ns, debounce_ns, stable_read_ns, perf);
        std::cout << (perf.published ? "policy reloaded\n" : "policy unchanged\n")
                  << "perf reload_total_us=" << NsToUs(ElapsedNs(reload_started))
                  << '\n' << std::flush;
    } else {
        *rejected_config = std::move(stable_candidate);
        LogCompilePerf("reload_failed", candidate_read_ns, debounce_ns, stable_read_ns, perf);
        std::cerr << "policy reload failed: " << error << '\n' << std::flush;
    }
}

static void RunPollingLoop(const fs::path& config, const fs::path& policy,
                           std::string* active_config) {
    std::string rejected_config;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ReloadIfChanged(config, policy, active_config, &rejected_config);
    }
}

#if PATHGUARD_HAS_INOTIFY
static bool IsRulesEvent(const inotify_event* event, const std::string& file_name) {
    if (event->len == 0 || file_name != event->name) return false;
    constexpr std::uint32_t kRelevantMask = IN_CLOSE_WRITE | IN_MOVED_TO
        | IN_CREATE | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM;
    return (event->mask & kRelevantMask) != 0;
}

static bool RunInotifyLoop(const fs::path& config, const fs::path& policy,
                           std::string* active_config) {
    const fs::path directory = config.parent_path();
    const std::string file_name = config.filename().string();
    const int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (fd < 0) {
        std::cerr << "inotify initialization failed; falling back to polling: errno="
                  << errno << '\n' << std::flush;
        return false;
    }
    const int watch = inotify_add_watch(
        fd, directory.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE
            | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM);
    if (watch < 0) {
        std::cerr << "inotify watch failed; falling back to polling: errno="
                  << errno << '\n' << std::flush;
        close(fd);
        return false;
    }
    std::cout << "inotify watching: " << directory.string() << "\n" << std::flush;

    std::vector<char> buffer(16 * (sizeof(inotify_event) + NAME_MAX + 1));
    std::string rejected_config;
    while (true) {
        pollfd descriptor{fd, POLLIN, 0};
        if (poll(&descriptor, 1, -1) < 0) {
            if (errno == EINTR) continue;
            std::cerr << "inotify poll failed; falling back to polling: errno="
                      << errno << '\n' << std::flush;
            close(fd);
            return false;
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
            ReloadIfChanged(config, policy, active_config, &rejected_config);
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
    const fs::path config = module_dir / "config" / "rules.ini";
    const fs::path policy = module_dir / "run" / "policy.bin";
    if (compile || self_check) {
        std::string error;
        CompilePerf perf;
        std::uint64_t read_ns = 0;
        const bool ok = CompileFile(config, policy, &perf, &read_ns, &error);
        LogCompilePerf(self_check ? "self_check" : "compile", read_ns, 0, 0, perf);
        if (!ok) { std::cerr << error << '\n'; return 1; }
        std::cout << (self_check ? "ok" : "compiled") << '\n';
        return 0;
    }
    std::string active_config;
    if (!ReadAll(config, &active_config)) {
        std::cerr << "initial compile failed: cannot read rules.ini\n";
        return 1;
    }
    {
        std::string error;
        CompilePerf perf;
        if (!CompileText(active_config, policy, &perf, &error)) {
            std::cerr << "initial compile failed: " << error << '\n';
            return 1;
        }
        LogCompilePerf("initial", 0, 0, 0, perf);
    }
    ProbeStorageTopology();
    std::cout << "pathguardd ready; module-dir=" << module_dir.string() << '\n'
              << std::flush;
#if PATHGUARD_HAS_INOTIFY
    if (RunInotifyLoop(config, policy, &active_config)) return 0;
#endif
    RunPollingLoop(config, policy, &active_config);
    return 0;
}
