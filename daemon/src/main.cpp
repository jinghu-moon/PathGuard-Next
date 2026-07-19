#include <chrono>
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
#include "pathguard/validation.h"

namespace fs = std::filesystem;

static bool ReadAll(const fs::path& path, std::string* output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    *output = std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

static bool CompileText(const std::string& text, const fs::path& output,
                        bool* published, std::string* error) {
    *published = false;
    pathguard::PolicyDocument document;
    pathguard::ParseError parse_error;
    if (!pathguard::ParseRulesIni(text, &document, &parse_error)) { *error = "line " + std::to_string(parse_error.line) + ": " + parse_error.message; return false; }
    for (auto& app : document.apps) {
        if (!pathguard::ValidatePolicy(&app, &parse_error)) { *error = "line " + std::to_string(parse_error.line) + ": " + parse_error.message; return false; }
    }
    std::vector<std::uint8_t> bytes;
    if (!pathguard::EncodePolicy(document, 1, &bytes, &parse_error)) { *error = parse_error.message; return false; }
    std::string current;
    if (ReadAll(output, &current)
        && current.size() == bytes.size()
        && std::memcmp(current.data(), bytes.data(), bytes.size()) == 0) {
        return true;
    }
    fs::create_directories(output.parent_path());
    const fs::path temporary = output.string() + ".tmp";
    { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); if (!file) { *error = "cannot create policy.bin.tmp"; return false; } file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    std::error_code ec;
    fs::rename(temporary, output, ec);
    if (ec) { fs::remove(output, ec); fs::rename(temporary, output, ec); }
    if (ec) { *error = "cannot publish policy.bin: " + ec.message(); return false; }
    *published = true;
    return true;
}

static bool CompileFile(const fs::path& config, const fs::path& output,
                        std::string* error) {
    std::string text;
    if (!ReadAll(config, &text)) {
        *error = "cannot read rules.ini";
        return false;
    }
    bool published = false;
    return CompileText(text, output, &published, error);
}

static void ReloadIfChanged(const fs::path& config, const fs::path& policy,
                            std::string* active_config,
                            std::string* rejected_config) {
    std::string candidate;
    if (!ReadAll(config, &candidate)
        || candidate == *active_config
        || candidate == *rejected_config) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::string stable_candidate;
    if (!ReadAll(config, &stable_candidate) || stable_candidate != candidate) {
        return;
    }

    std::string error;
    bool published = false;
    if (CompileText(stable_candidate, policy, &published, &error)) {
        *active_config = std::move(stable_candidate);
        rejected_config->clear();
        std::cout << (published ? "policy reloaded\n" : "policy unchanged\n")
                  << std::flush;
    } else {
        *rejected_config = std::move(stable_candidate);
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
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--module-dir" && i + 1 < argc) module_dir = argv[++i];
        else if (arg == "--compile") compile = true;
        else if (arg == "--self-check") self_check = true;
    }
    const fs::path config = module_dir / "config" / "rules.ini";
    const fs::path policy = module_dir / "run" / "policy.bin";
    if (compile || self_check) {
        std::string error;
        const bool ok = CompileFile(config, policy, &error);
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
        bool published = false;
        if (!CompileText(active_config, policy, &published, &error)) {
            std::cerr << "initial compile failed: " << error << '\n';
            return 1;
        }
    }
    std::cout << "pathguardd ready; module-dir=" << module_dir.string() << '\n'
              << std::flush;
#if PATHGUARD_HAS_INOTIFY
    if (RunInotifyLoop(config, policy, &active_config)) return 0;
#endif
    RunPollingLoop(config, policy, &active_config);
    return 0;
}
