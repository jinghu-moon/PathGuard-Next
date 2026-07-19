#include <android/log.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pathguard/media_query_hook.hpp"
#include "zygisk.hpp"

namespace {

constexpr char kLogTag[] = "PathGuard";
constexpr char kPolicyPath[] = "run/policy.bin";
constexpr char kDenyFilesystem[] = "pathguard_deny";
constexpr char kMarkerPrefix[] = ".pathguard_mount_status_";
constexpr std::uint32_t kPolicyMagic = 0x424E4750;
constexpr std::uint16_t kPolicyFormatVersion = 1;
constexpr std::size_t kPolicyHeaderSize = 40;
constexpr std::size_t kPackageEntrySize = 24;
constexpr std::size_t kRuleEntrySize = 20;
constexpr std::uint32_t kBootstrapMagic = 0x50474250;
constexpr std::uint32_t kBootstrapVersion = 2;
constexpr std::size_t kMaxDenyRules = 8;
constexpr int kProcessReadyTimeoutMs = 5000;
constexpr int kResultTimeoutMs = 5000;
constexpr int kChildTerminateGraceMs = 1000;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

struct DenyPlan {
    std::uint32_t count = 0;
    char paths[kMaxDenyRules][PATH_MAX]{};
    char app_data_dir[PATH_MAX]{};
};

struct BootstrapHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::int32_t pid;
    std::int32_t uid;
    std::uint32_t rule_count;
    std::uint32_t app_data_dir_length;
};

bool ReadFully(int fd, void* buffer, std::size_t size) {
    auto* output = static_cast<std::uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t received = read(fd, output, size);
        if (received == 0) return false;
        if (received < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        output += received;
        size -= static_cast<std::size_t>(received);
    }
    return true;
}

bool WriteFully(int fd, const void* buffer, std::size_t size) {
    const auto* input = static_cast<const std::uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t written = write(fd, input, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        input += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

void SetSocketTimeout(int fd) {
    const timeval timeout{kResultTimeoutMs / 1000, (kResultTimeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

std::uint16_t ReadLe16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0])
        | static_cast<std::uint16_t>(value[1]) << 8;
}

std::uint32_t ReadLe32(const std::uint8_t* value) {
    std::uint32_t result = 0;
    for (int index = 0; index < 4; ++index) {
        result |= static_cast<std::uint32_t>(value[index]) << (index * 8);
    }
    return result;
}

const char* ReadPolicyString(const std::uint8_t* data, std::size_t size,
                             std::uint32_t string_offset, std::uint32_t relative_offset) {
    if (string_offset > size || relative_offset >= size - string_offset) return nullptr;
    const char* value = reinterpret_cast<const char*>(data + string_offset + relative_offset);
    const std::size_t remaining = size - string_offset - relative_offset;
    return memchr(value, '\0', remaining) == nullptr ? nullptr : value;
}

bool ListContains(const char* list, const char* expected) {
    if (list == nullptr || expected == nullptr) return false;
    if (strcmp(list, "*") == 0) return true;
    const std::size_t expected_length = strlen(expected);
    const char* current = list;
    while (*current != '\0') {
        const char* separator = strchr(current, ',');
        const std::size_t length = separator == nullptr
            ? strlen(current)
            : static_cast<std::size_t>(separator - current);
        if (length == expected_length && memcmp(current, expected, length) == 0) return true;
        if (separator == nullptr) break;
        current = separator + 1;
    }
    return false;
}

bool ProcessMatches(const char* package_name, const char* process_list, const char* process_name) {
    if (strcmp(process_list, "*") != 0) return ListContains(process_list, process_name);
    const std::size_t package_length = strlen(package_name);
    return strcmp(package_name, process_name) == 0
        || (strncmp(package_name, process_name, package_length) == 0
            && process_name[package_length] == ':');
}

bool LoadDenyPlan(int module_dir, const char* process_name, jint uid, DenyPlan* plan) {
    const int policy_fd = openat(module_dir, kPolicyPath, O_RDONLY | O_CLOEXEC);
    if (policy_fd < 0) return false;

    struct stat file_stat {};
    if (fstat(policy_fd, &file_stat) != 0 || file_stat.st_size < static_cast<off_t>(kPolicyHeaderSize)) {
        close(policy_fd);
        return false;
    }
    const std::size_t size = static_cast<std::size_t>(file_stat.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, policy_fd, 0);
    close(policy_fd);
    if (mapping == MAP_FAILED) return false;

    const auto* data = static_cast<const std::uint8_t*>(mapping);
    const std::uint32_t file_size = ReadLe32(data + 16);
    const std::uint32_t package_count = ReadLe32(data + 24);
    const std::uint32_t package_offset = ReadLe32(data + 28);
    const std::uint32_t rule_offset = ReadLe32(data + 32);
    const std::uint32_t string_offset = ReadLe32(data + 36);
    const bool valid_header = ReadLe32(data) == kPolicyMagic
        && ReadLe16(data + 4) == kPolicyFormatVersion
        && ReadLe16(data + 6) == 1
        && file_size == size
        && package_offset >= kPolicyHeaderSize
        && package_offset <= rule_offset
        && rule_offset <= string_offset
        && string_offset <= size
        && package_count <= (rule_offset - package_offset) / kPackageEntrySize;
    if (!valid_header) {
        munmap(mapping, size);
        return false;
    }

    char user_id[16];
    snprintf(user_id, sizeof(user_id), "%d", uid / 100000);
    bool found = false;
    for (std::uint32_t index = 0; index < package_count; ++index) {
        const std::size_t entry_offset = package_offset + index * kPackageEntrySize;
        const auto* entry = data + entry_offset;
        const char* package_name = ReadPolicyString(data, size, string_offset, ReadLe32(entry));
        const char* users = ReadPolicyString(data, size, string_offset, ReadLe32(entry + 4));
        const char* processes = ReadPolicyString(data, size, string_offset, ReadLe32(entry + 8));
        const std::uint32_t first_rule = ReadLe32(entry + 12);
        const std::uint32_t rule_count = ReadLe32(entry + 16);
        const bool enabled = ReadLe32(entry + 20) != 0;
        if (package_name == nullptr || users == nullptr || processes == nullptr) break;
        if (!enabled || !ProcessMatches(package_name, processes, process_name)
            || !ListContains(users, user_id)) {
            continue;
        }
        const std::size_t total_rules = (string_offset - rule_offset) / kRuleEntrySize;
        if (first_rule > total_rules || rule_count > total_rules - first_rule) break;
        for (std::uint32_t rule_index = 0; rule_index < rule_count; ++rule_index) {
            const auto* rule = data + rule_offset + (first_rule + rule_index) * kRuleEntrySize;
            if (ReadLe32(rule) != 0 || plan->count >= kMaxDenyRules) {
                plan->count = 0;
                break;
            }
            const char* source = ReadPolicyString(data, size, string_offset, ReadLe32(rule + 4));
            if (source == nullptr || strlen(source) >= PATH_MAX) {
                plan->count = 0;
                break;
            }
            strcpy(plan->paths[plan->count++], source);
        }
        found = plan->count > 0;
        break;
    }
    munmap(mapping, size);
    return found;
}

bool HasSafePathComponents(const char* path) {
    if (path == nullptr || path[0] != '/') return false;
    const char* component = path + 1;
    while (*component != '\0') {
        const char* separator = strchr(component, '/');
        const std::size_t length = separator == nullptr
            ? strlen(component)
            : static_cast<std::size_t>(separator - component);
        if (length == 0 || (length == 1 && component[0] == '.')
            || (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (separator == nullptr) break;
        component = separator + 1;
    }
    return true;
}

bool IsAllowedTarget(const char* path) {
    constexpr char kPrimaryRoot[] = "/storage/emulated/0/";
    return path != nullptr
        && strncmp(path, kPrimaryRoot, sizeof(kPrimaryRoot) - 1) == 0
        && HasSafePathComponents(path);
}

bool IsSafeAppDataDir(const char* path) {
    return path != nullptr
        && (strncmp(path, "/data/user/", 11) == 0
            || strncmp(path, "/data/user_de/", 14) == 0)
        && HasSafePathComponents(path);
}

bool BuildUserStoragePath(const char* primary_path, uid_t uid,
                          char* output, std::size_t output_size) {
    if (!IsAllowedTarget(primary_path) || output == nullptr || output_size == 0) {
        return false;
    }

    const unsigned user_id = static_cast<unsigned>(uid) / 100000u;
    constexpr char primary_root[] = "/storage/emulated/0";
    const int written = snprintf(output, output_size, "/storage/emulated/%u%s",
                                 user_id, primary_path + sizeof(primary_root) - 1);
    return written >= 0 && static_cast<std::size_t>(written) < output_size;
}

bool ReadProcessUid(pid_t pid, uid_t expected_uid) {
    char status_path[64]{};
    snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
    FILE* status = fopen(status_path, "re");
    if (status == nullptr) return false;
    char line[256]{};
    bool matched = false;
    while (fgets(line, sizeof(line), status) != nullptr) {
        unsigned real_uid = 0;
        if (sscanf(line, "Uid:\t%u", &real_uid) == 1) {
            matched = real_uid == expected_uid;
            break;
        }
    }
    fclose(status);
    return matched;
}

bool ReadProcessContext(pid_t pid, char* context, std::size_t context_size) {
    if (context == nullptr || context_size < 2) return false;
    char attr_path[64]{};
    snprintf(attr_path, sizeof(attr_path), "/proc/%d/attr/current", pid);
    FILE* attr = fopen(attr_path, "re");
    if (attr == nullptr) return false;
    const char* result = fgets(context, static_cast<int>(context_size), attr);
    fclose(attr);
    if (result == nullptr) return false;
    context[strcspn(context, "\r\n")] = '\0';
    return context[0] != '\0';
}

bool WaitForProcessReady(pid_t pid, uid_t expected_uid) {
    for (int elapsed = 0; elapsed < kProcessReadyTimeoutMs; elapsed += 10) {
        char context[256]{};
        if (ReadProcessUid(pid, expected_uid)
            && ReadProcessContext(pid, context, sizeof(context))
            && strstr(context, "zygote") == nullptr) {
            return true;
        }
        usleep(10000);
    }
    return false;
}

bool BuildMarkerPath(const char* app_data_dir, pid_t pid,
                     char* marker_path, std::size_t marker_size) {
    if (!IsSafeAppDataDir(app_data_dir) || pid <= 0
        || marker_path == nullptr || marker_size == 0) {
        return false;
    }
    const int written = snprintf(marker_path, marker_size, "%s/%s%d",
                                 app_data_dir, kMarkerPrefix, pid);
    return written >= 0 && static_cast<std::size_t>(written) < marker_size;
}

bool WriteMountMarker(const char* app_data_dir, pid_t pid, uid_t uid, int result) {
    char marker_path[PATH_MAX]{};
    if (!BuildMarkerPath(app_data_dir, pid, marker_path, sizeof(marker_path))) return false;
    const int fd = open(marker_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    const char value = result == 0 ? '1' : '0';
    bool ok = WriteFully(fd, &value, sizeof(value));
    if (fsync(fd) != 0) ok = false;
    if (fchown(fd, uid, uid) != 0) ok = false;
    if (fchmod(fd, 0600) != 0) ok = false;
    close(fd);
    return ok;
}

void ClearMountMarker(const char* app_data_dir, pid_t pid) {
    char marker_path[PATH_MAX]{};
    if (BuildMarkerPath(app_data_dir, pid, marker_path, sizeof(marker_path))) {
        unlink(marker_path);
    }
}

int ApplyDenyPlan(pid_t pid, uid_t uid, const DenyPlan& plan) {
    char namespace_path[64];
    snprintf(namespace_path, sizeof(namespace_path), "/proc/%d/ns/mnt", pid);
    const int namespace_fd = open(namespace_path, O_RDONLY | O_CLOEXEC);
    if (namespace_fd < 0) {
        const int error = errno;
        LOGE("mount namespace open failed: pid=%d errno=%d", pid, error);
        return error;
    }
    if (setns(namespace_fd, CLONE_NEWNS) != 0) {
        const int error = errno;
        LOGE("mount namespace enter failed: pid=%d errno=%d", pid, error);
        close(namespace_fd);
        return error;
    }
    close(namespace_fd);

    if (mount(nullptr, "/storage", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
        LOGE("storage mount propagation isolation unavailable: errno=%d", errno);
    }

    char applied_targets[kMaxDenyRules][PATH_MAX]{};
    std::size_t applied_count = 0;
    int error = 0;
    for (std::size_t rule_index = 0; rule_index < plan.count && error == 0; ++rule_index) {
        char canonical_path[PATH_MAX]{};
        if (!BuildUserStoragePath(plan.paths[rule_index], uid,
                                  canonical_path, sizeof(canonical_path))) {
            error = EINVAL;
            break;
        }
        struct stat target_stat {};
        if (lstat(canonical_path, &target_stat) != 0) {
            error = errno == 0 ? ENOENT : errno;
            LOGE("deny target stat failed: target=%s errno=%d", canonical_path, error);
            break;
        }
        if (!S_ISDIR(target_stat.st_mode)) {
            error = ENOTDIR;
            LOGE("deny target is not a directory: target=%s", canonical_path);
            break;
        }
        constexpr unsigned long kDenyMountFlags =
            MS_NOSUID | MS_NODEV | MS_NOEXEC;
        if (mount(kDenyFilesystem, canonical_path, "tmpfs", kDenyMountFlags,
                  "mode=000,size=4096") != 0) {
            error = errno;
            LOGE("deny tmpfs mount failed: target=%s errno=%d", canonical_path, error);
            break;
        }
        strcpy(applied_targets[applied_count++], canonical_path);
    }
    if (error != 0) {
        while (applied_count > 0) {
            umount2(applied_targets[--applied_count], MNT_DETACH);
        }
    }
    return error;
}

bool WaitForMountMarker(const char* app_data_dir, pid_t pid, bool* applied) {
    if (applied == nullptr) return false;
    *applied = false;
    char marker_path[PATH_MAX]{};
    if (!BuildMarkerPath(app_data_dir, pid, marker_path, sizeof(marker_path))) return false;

    for (int elapsed = 0; elapsed < kResultTimeoutMs; elapsed += 25) {
        const int fd = open(marker_path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            char value = '0';
            const ssize_t read_count = read(fd, &value, sizeof(value));
            close(fd);
            unlink(marker_path);
            *applied = read_count == 1 && value == '1';
            return true;
        }
        usleep(25000);
    }
    return false;
}

bool SendPlan(int fd, jint uid, const DenyPlan& plan) {
    const std::size_t app_data_dir_length = strlen(plan.app_data_dir);
    if (app_data_dir_length >= PATH_MAX) return false;
    const BootstrapHeader header{
        kBootstrapMagic,
        kBootstrapVersion,
        getpid(),
        uid,
        plan.count,
        static_cast<std::uint32_t>(app_data_dir_length),
    };
    if (!WriteFully(fd, &header, sizeof(header))) return false;
    if (app_data_dir_length > 0
        && !WriteFully(fd, plan.app_data_dir, app_data_dir_length)) {
        return false;
    }
    for (std::uint32_t index = 0; index < plan.count; ++index) {
        const std::uint32_t length = static_cast<std::uint32_t>(strlen(plan.paths[index]));
        if (!WriteFully(fd, &length, sizeof(length))
            || !WriteFully(fd, plan.paths[index], length)) {
            return false;
        }
    }
    return true;
}

class PathGuardModule final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        const char* process_name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (process_name == nullptr) {
            Unload();
            return;
        }

        DenyPlan plan;
        const int module_dir = api_->getModuleDir();
        const bool matched = module_dir >= 0
            && LoadDenyPlan(module_dir, process_name, args->uid, &plan);
        if (module_dir >= 0) close(module_dir);
        env_->ReleaseStringUTFChars(args->nice_name, process_name);
        if (!matched) {
            Unload();
            return;
        }

        if (args->app_data_dir != nullptr) {
            const char* app_data_dir = env_->GetStringUTFChars(args->app_data_dir, nullptr);
            if (app_data_dir != nullptr) {
                if (IsSafeAppDataDir(app_data_dir)) {
                    strncpy(plan.app_data_dir, app_data_dir, sizeof(plan.app_data_dir) - 1);
                } else {
                    LOGE("invalid app data dir; marker disabled: %s", app_data_dir);
                }
                env_->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
            } else if (env_->ExceptionCheck()) {
                env_->ExceptionClear();
            }
        }
        strncpy(app_data_dir_, plan.app_data_dir, sizeof(app_data_dir_) - 1);
        ClearMountMarker(plan.app_data_dir, getpid());

        const char* deny_paths[kMaxDenyRules]{};
        for (std::uint32_t index = 0; index < plan.count; ++index) {
            deny_paths[index] = plan.paths[index];
        }
        media_query_hook_installed_ = pathguard::media_query::Install(
            api_, env_, deny_paths, plan.count, args->uid);
        if (!media_query_hook_installed_) {
            LOGE("media query hook unavailable; direct filesystem deny remains enabled");
        }

        const int companion_fd = api_->connectCompanion();
        if (companion_fd < 0) {
            LOGE("cannot connect companion socket");
            if (!media_query_hook_installed_) Unload();
            return;
        }
        SetSocketTimeout(companion_fd);
        if (!SendPlan(companion_fd, args->uid, plan)) {
            LOGE("cannot send deny plan to companion");
            if (!media_query_hook_installed_) Unload();
        } else {
            mount_request_sent_ = true;
        }
        close(companion_fd);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if (mount_request_sent_ && app_data_dir_[0] != '\0') {
            bool applied = false;
            if (WaitForMountMarker(app_data_dir_, getpid(), &applied) && applied) {
                LOGI("deny mount active: pid=%d", getpid());
            } else {
                LOGE("deny mount failed or timed out: pid=%d", getpid());
            }
        } else if (mount_request_sent_) {
            LOGI("deny mount result marker disabled: pid=%d", getpid());
        }
        if (!media_query_hook_installed_) Unload();
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs*) override { Unload(); }

private:
    void Unload() { api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }

    zygisk::Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool media_query_hook_installed_ = false;
    bool mount_request_sent_ = false;
    char app_data_dir_[PATH_MAX]{};
};

void CompanionHandler(int client) {
    SetSocketTimeout(client);
    BootstrapHeader header{};
    DenyPlan plan;
    if (!ReadFully(client, &header, sizeof(header))
        || header.magic != kBootstrapMagic
        || header.version != kBootstrapVersion
        || header.pid <= 0
        || header.uid < 10000
        || header.rule_count == 0
        || header.rule_count > kMaxDenyRules) {
        return;
    }
    plan.count = header.rule_count;
    if (header.app_data_dir_length >= PATH_MAX
        || !ReadFully(client, plan.app_data_dir, header.app_data_dir_length)) {
        return;
    }
    plan.app_data_dir[header.app_data_dir_length] = '\0';
    if (plan.app_data_dir[0] != '\0' && !IsSafeAppDataDir(plan.app_data_dir)) return;
    for (std::uint32_t index = 0; index < plan.count; ++index) {
        std::uint32_t length = 0;
        if (!ReadFully(client, &length, sizeof(length))
            || length == 0
            || length >= PATH_MAX
            || !ReadFully(client, plan.paths[index], length)) {
            return;
        }
        plan.paths[index][length] = '\0';
        if (!IsAllowedTarget(plan.paths[index])) return;
    }

    if (!WaitForProcessReady(header.pid, static_cast<uid_t>(header.uid))) {
        if (!WriteMountMarker(plan.app_data_dir, header.pid,
                              static_cast<uid_t>(header.uid), ETIMEDOUT)
            && plan.app_data_dir[0] != '\0') {
            LOGE("process readiness marker write failed: pid=%d", header.pid);
        }
        LOGE("target process did not become ready: pid=%d", header.pid);
        return;
    }

    int result_sockets[2];
    int result = EIO;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, result_sockets) == 0) {
        const pid_t child = fork();
        if (child == 0) {
            close(result_sockets[0]);
            const int child_result = ApplyDenyPlan(
                header.pid, static_cast<uid_t>(header.uid), plan);
            send(result_sockets[1], &child_result, sizeof(child_result), MSG_NOSIGNAL);
            close(result_sockets[1]);
            _exit(child_result == 0 ? 0 : 1);
        }
        close(result_sockets[1]);
        if (child > 0) {
            SetSocketTimeout(result_sockets[0]);
            if (!ReadFully(result_sockets[0], &result, sizeof(result))) {
                kill(child, SIGTERM);
                const timeval grace{0, kChildTerminateGraceMs * 1000};
                setsockopt(result_sockets[0], SOL_SOCKET, SO_RCVTIMEO,
                           &grace, sizeof(grace));
                if (!ReadFully(result_sockets[0], &result, sizeof(result))) {
                    kill(child, SIGKILL);
                    result = EIO;
                }
            }
            while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        } else {
            result = errno;
        }
        close(result_sockets[0]);
    } else {
        result = errno;
    }

    if (!WriteMountMarker(plan.app_data_dir, header.pid,
                          static_cast<uid_t>(header.uid), result)
        && plan.app_data_dir[0] != '\0') {
        LOGE("mount marker write failed: pid=%d", header.pid);
    }

    if (result == 0) {
        LOGI("companion deny mount applied: pid=%d rules=%u", header.pid, plan.count);
    } else {
        LOGE("companion deny mount failed: pid=%d errno=%d", header.pid, result);
    }
}

}  // namespace

REGISTER_ZYGISK_MODULE(PathGuardModule)
REGISTER_ZYGISK_COMPANION(CompanionHandler)
