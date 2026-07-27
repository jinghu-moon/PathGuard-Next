#include <android/log.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/futex.h>
#include <linux/memfd.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pathguard/provider_redirect_hook.hpp"
#include "pathguard/media_query_hook.hpp"
#include "pathguard/mount_backend.h"
#include "pathguard/mount_executor.h"
#include "pathguard/mount_transaction.h"
#include "pathguard/mutation_journal.h"
#include "pathguard/perf_clock.hpp"
#include "pathguard/policy_format.h"
#include "pathguard/policy_index.h"
#include "pathguard/runtime_status.h"
#include "zygisk.hpp"

namespace {

constexpr char kLogTag[] = "PathGuard";
constexpr char kPolicyPath[] = "run/policy.bin";
constexpr uint32_t kPolicyMagic = pathguard::binary_format::kMagic;
constexpr uint16_t kPolicyFormatVersion = pathguard::binary_format::kFormatVersion;
constexpr size_t kPolicyHeaderSize = pathguard::binary_format::kHeaderSize;
constexpr size_t kPackageEntrySize = pathguard::binary_format::kPackageSize;
constexpr size_t kMountRuleEntrySize = pathguard::binary_format::kMountRuleSize;
constexpr size_t kEventRuleEntrySize = pathguard::binary_format::kEventRuleSize;
constexpr uint32_t kBootstrapMagic = 0x50474250;
constexpr uint32_t kBootstrapVersion = 5;
constexpr uint32_t kCompanionResultMagic = 0x52534750;
constexpr uint32_t kCompanionResultVersion = 1;
constexpr uint32_t kSharedStateMagic = 0x53534750;
constexpr uint32_t kSharedStateVersion = 3;
constexpr size_t kMaxMountRules = 64;
constexpr size_t kMaxPlanPathBytes = 64 * 1024;
constexpr size_t kMaxProcessNameBytes = 256;
constexpr int kProcessReadyTimeoutMs = 5000;
constexpr int kCompanionIoTimeoutMs = 5000;
constexpr int kAppResultTimeoutMs = 300;
constexpr int kCapabilityProbeGraceMs = 500;
constexpr int kApplyingCompletionGraceMs = 500;
constexpr int kChildTerminateGraceMs = 1000;
constexpr int kApplyingOwnerDeathTimeoutMs = 10000;
constexpr uint8_t kDenyAction = 0;
constexpr uint8_t kRedirectAction = 1;
constexpr char kDenyAnchorRelativePath[] = "run/deny-anchor";
constexpr char kDiagnosticPackage[] = "org.localsend.localsend_app";
constexpr char kExternalStorageProviderProcess[] = "com.android.externalstorage";
constexpr char kMainlineMediaProviderProcess[] = "com.android.providers.media.module";
#ifndef PATHGUARD_TEST_MOUNT_DELAY_MS
#define PATHGUARD_TEST_MOUNT_DELAY_MS 0
#endif
#ifndef PATHGUARD_TEST_PRE_LEASE_DELAY_MS
#define PATHGUARD_TEST_PRE_LEASE_DELAY_MS 0
#endif
#ifndef PATHGUARD_TEST_CRASH_AFTER_MOUNT
#define PATHGUARD_TEST_CRASH_AFTER_MOUNT 0
#endif
#ifndef PATHGUARD_TEST_ROLLBACK_FAILURE
#define PATHGUARD_TEST_ROLLBACK_FAILURE 0
#endif

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

struct PlannedMount {
    uint8_t action = 0;
    uint32_t visible_path = 0;
    uint32_t backing_path = 0;
};

struct RuntimeStorageTopology {
    uint64_t generation = 0;
    uint32_t user_id = 0;
    char visible_root[PATH_MAX]{};
    char source_root[PATH_MAX]{};
    pathguard::MountPathIdentity visible_identity;
    pathguard::MountPathIdentity source_identity;
};

struct ProcessPlan {
    uint64_t snapshot_generation = 0;
    uint64_t plan_generation = 0;
    uint32_t policy_flags = 0;
    uint32_t count = 0;
    uint32_t path_bytes = 1;
    bool media_compat = false;
    bool provider_compat = false;
    RuntimeStorageTopology topology;
    char process_name[kMaxProcessNameBytes]{};
    PlannedMount mounts[kMaxMountRules]{};
    char paths[kMaxPlanPathBytes]{};
};

struct BootstrapHeader {
    uint32_t magic;
    uint32_t version;
    int32_t pid;
    int32_t uid;
    uint32_t rule_count;
    uint32_t policy_flags;
    uint64_t snapshot_generation;
    uint64_t plan_generation;
    uint32_t process_name_length;
    uint32_t compat_flags;
};

struct PolicyLoadPerf {
    uint64_t open_map_ns = 0;
    uint64_t lookup_ns = 0;
    uint64_t unmap_ns = 0;
};

struct MountPerfResult {
    int32_t result = EIO;
    uint32_t rule_count = 0;
    uint32_t backend = 0;
    uint32_t backend_reason = 0;
    uint32_t failure_stage = 0;
    uint32_t rollback_failed = 0;
    uint32_t runtime_reason = 0;
    uint32_t mountinfo_snapshot_count = 0;
    uint64_t topology_generation = 0;
    uint64_t setns_ns = 0;
    uint64_t topology_ns = 0;
    uint64_t source_pin_ns = 0;
    uint64_t policy_revalidate_ns = 0;
    uint64_t propagation_check_ns = 0;
    uint64_t propagation_ns = 0;
    uint64_t mount_total_ns = 0;
    uint64_t mount_max_ns = 0;
    uint64_t rollback_ns = 0;
    uint64_t mountinfo_read_ns = 0;
    uint64_t mountinfo_parse_ns = 0;
};

struct CompanionResult {
    uint32_t magic = kCompanionResultMagic;
    uint32_t version = kCompanionResultVersion;
    uint64_t ready_ns = 0;
    MountPerfResult mount;
};

struct SharedMountState {
    uint32_t magic = kSharedStateMagic;
    uint32_t version = kSharedStateVersion;
    alignas(4) uint32_t status = static_cast<uint32_t>(
        pathguard::MountTransactionState::kPending);
    alignas(4) uint32_t preflight_phase = static_cast<uint32_t>(
        pathguard::MountPreflightPhase::kIdle);
    CompanionResult result;
};

using MountMutationJournal =
    pathguard::MutationJournal<pathguard::AppliedMount, kMaxMountRules>;

struct MountTransactionWorkspace {
    pathguard::PinnedIdentity sources[kMaxMountRules]{};
    MountMutationJournal journal;
};

MountTransactionWorkspace* CreateMountTransactionWorkspace() {
    void* mapping = mmap(nullptr, sizeof(MountTransactionWorkspace),
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return nullptr;
    memset(mapping, 0, sizeof(MountTransactionWorkspace));
    return static_cast<MountTransactionWorkspace*>(mapping);
}

void DestroyMountTransactionWorkspace(MountTransactionWorkspace* workspace) {
    if (workspace == nullptr) return;
    munmap(workspace, sizeof(*workspace));
}

bool IsAllowedTarget(const char* path);
bool ExpandRuntimePath(const char* input, uint32_t user_id,
                       char* output, size_t capacity);

bool ReadFully(int fd, void* buffer, size_t size) {
    auto* output = static_cast<uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t received = read(fd, output, size);
        if (received == 0) return false;
        if (received < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        output += received;
        size -= static_cast<size_t>(received);
    }
    return true;
}

bool WriteFully(int fd, const void* buffer, size_t size) {
    const auto* input = static_cast<const uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t written = send(fd, input, size, MSG_NOSIGNAL);
        if (written == 0) return false;
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        input += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

bool WriteRegularFileFully(int fd, const void* buffer, size_t size) {
    const auto* input = static_cast<const uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t written = write(fd, input, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        input += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

void SetSocketTimeout(int fd, int timeout_ms) {
    const timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

uint16_t ReadLe16(const uint8_t* value) {
    return static_cast<uint16_t>(value[0])
        | static_cast<uint16_t>(value[1]) << 8;
}

uint32_t ReadLe32(const uint8_t* value) {
    uint32_t result = 0;
    for (int index = 0; index < 4; ++index) {
        result |= static_cast<uint32_t>(value[index]) << (index * 8);
    }
    return result;
}

uint64_t ReadLe64(const uint8_t* value) {
    uint64_t result = 0;
    for (int index = 0; index < 8; ++index) {
        result |= static_cast<uint64_t>(value[index]) << (index * 8);
    }
    return result;
}

const char* ReadPolicyString(const uint8_t* data, size_t size,
                             uint32_t string_offset, uint32_t relative_offset) {
    if (string_offset > size || relative_offset >= size - string_offset) return nullptr;
    const char* value = reinterpret_cast<const char*>(data + string_offset + relative_offset);
    const size_t remaining = size - string_offset - relative_offset;
    return memchr(value, '\0', remaining) == nullptr ? nullptr : value;
}

bool ListContains(const char* list, const char* expected) {
    if (list == nullptr || expected == nullptr) return false;
    if (strcmp(list, "*") == 0) return true;
    const size_t expected_length = strlen(expected);
    const char* current = list;
    while (*current != '\0') {
        const char* separator = strchr(current, ',');
        const size_t length = separator == nullptr
            ? strlen(current)
            : static_cast<size_t>(separator - current);
        if (length == expected_length && memcmp(current, expected, length) == 0) return true;
        if (separator == nullptr) break;
        current = separator + 1;
    }
    return false;
}

bool ProcessMatches(const char* package_name, const char* process_list, const char* process_name) {
    if (strcmp(process_list, "*") != 0) return ListContains(process_list, process_name);
    const size_t package_length = strlen(package_name);
    return strcmp(package_name, process_name) == 0
        || (strncmp(package_name, process_name, package_length) == 0
            && process_name[package_length] == ':');
}

const char* PlanPath(const ProcessPlan& plan, uint32_t offset) {
    return offset < plan.path_bytes ? plan.paths + offset : nullptr;
}

bool StorePlanPath(ProcessPlan* plan, const char* path, uint32_t* offset) {
    if (plan == nullptr || path == nullptr || offset == nullptr) return false;
    const size_t length = strlen(path) + 1;
    if (length > kMaxPlanPathBytes - plan->path_bytes) return false;
    *offset = plan->path_bytes;
    memcpy(plan->paths + plan->path_bytes, path, length);
    plan->path_bytes += static_cast<uint32_t>(length);
    return true;
}

bool IsExecutableMountAction(uint8_t action) {
    return action == kDenyAction || action == kRedirectAction;
}

pathguard::MountActionMask RequiredMountActions(const ProcessPlan& plan) {
    pathguard::MountActionMask required = 0;
    for (uint32_t index = 0; index < plan.count; ++index) {
        if (plan.mounts[index].action == kDenyAction) {
            required |= pathguard::kMountActionDenyAnchor;
        } else if (plan.mounts[index].action == kRedirectAction) {
            required |= pathguard::kMountActionRedirect;
        } else {
            return 0;
        }
    }
    return required;
}

bool BuildDenyAnchorPath(int module_dir_fd, char* output, size_t capacity) {
    if (module_dir_fd < 0 || output == nullptr || capacity == 0) return false;
    const int run_fd = openat(module_dir_fd, "run",
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (run_fd < 0) return false;
    const int anchor_fd = openat(run_fd, "deny-anchor",
                                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(run_fd);
    if (anchor_fd < 0) return false;
    struct stat anchor_stat {};
    bool valid = fstat(anchor_fd, &anchor_stat) == 0
        && S_ISDIR(anchor_stat.st_mode) && anchor_stat.st_uid == 0
        && (anchor_stat.st_mode & 0777) == 0;
    const int scan_fd = valid ? dup(anchor_fd) : -1;
    DIR* directory = scan_fd >= 0 ? fdopendir(scan_fd) : nullptr;
    if (directory == nullptr) valid = false;
    while (valid) {
        errno = 0;
        dirent* entry = readdir(directory);
        if (entry == nullptr) {
            valid = errno == 0;
            break;
        }
        if (strcmp(entry->d_name, ".") != 0
            && strcmp(entry->d_name, "..") != 0) {
            valid = false;
        }
    }
    if (directory != nullptr) closedir(directory);

    char descriptor_path[64]{};
    char module_path[PATH_MAX]{};
    const int descriptor_written = snprintf(
        descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d",
        module_dir_fd);
    const ssize_t module_length = descriptor_written > 0
            && static_cast<size_t>(descriptor_written) < sizeof(descriptor_path)
        ? readlink(descriptor_path, module_path, sizeof(module_path) - 1)
        : -1;
    if (module_length <= 0) valid = false;
    if (valid) {
        module_path[module_length] = '\0';
        const int written = snprintf(output, capacity, "%s/%s", module_path,
                                     kDenyAnchorRelativePath);
        struct stat path_stat {};
        valid = module_path[0] == '/'
            && strstr(module_path, " (deleted)") == nullptr
            && written > 0 && static_cast<size_t>(written) < capacity
            && lstat(output, &path_stat) == 0
            && path_stat.st_dev == anchor_stat.st_dev
            && path_stat.st_ino == anchor_stat.st_ino;
    }
    close(anchor_fd);
    return valid;
}

bool BuildPathUnderRoot(const char* root, const char* logical_path,
                        char* output, size_t output_size);

bool BuildMountSourcePath(const ProcessPlan& plan,
                          const PlannedMount& mount,
                          const char* deny_anchor_path,
                          char* output, size_t capacity) {
    if (mount.action == kDenyAction) {
        if (deny_anchor_path == nullptr || deny_anchor_path[0] != '/') {
            return false;
        }
        const int written = snprintf(output, capacity, "%s", deny_anchor_path);
        return written > 0 && static_cast<size_t>(written) < capacity;
    }
    const char* backing = PlanPath(plan, mount.backing_path);
    return mount.action == kRedirectAction && backing != nullptr
        && backing[0] != '\0'
        && BuildPathUnderRoot(plan.topology.source_root, backing,
                              output, capacity);
}

bool ValidPackageIndex(const uint8_t* data, size_t size, uint32_t package_count,
                       uint32_t package_offset, uint32_t string_offset) {
    uint32_t previous_hash = 0;
    const char* previous_name = nullptr;
    for (uint32_t index = 0; index < package_count; ++index) {
        const auto* entry = data + package_offset + index * kPackageEntrySize;
        const uint32_t hash = ReadLe32(
            entry + pathguard::binary_format::kPackageHashOffset);
        const char* name = ReadPolicyString(
            data, size, string_offset,
            ReadLe32(entry + pathguard::binary_format::kPackageNameOffset));
        if (name == nullptr
            || pathguard::binary_format::PackageNameHash(name, strlen(name)) != hash
            || (index > 0 && (hash < previous_hash
                || (hash == previous_hash && strcmp(name, previous_name) <= 0)))) {
            return false;
        }
        previous_hash = hash;
        previous_name = name;
    }
    return true;
}

bool ValidPolicyTables(const uint8_t* data, size_t size, uint32_t package_count,
                       uint32_t mount_count, uint32_t event_count,
                       uint32_t package_offset, uint32_t mount_offset,
                       uint32_t event_offset, uint32_t string_offset) {
    uint32_t expected_first_mount = 0;
    uint32_t expected_first_event = 0;
    for (uint32_t index = 0; index < package_count; ++index) {
        const auto* entry = data + package_offset + index * kPackageEntrySize;
        const uint32_t first_mount = ReadLe32(
            entry + pathguard::binary_format::kPackageFirstMountOffset);
        const uint32_t package_mount_count = ReadLe32(
            entry + pathguard::binary_format::kPackageMountCountOffset);
        const uint32_t first_event = ReadLe32(
            entry + pathguard::binary_format::kPackageFirstEventOffset);
        const uint32_t package_event_count = ReadLe32(
            entry + pathguard::binary_format::kPackageEventCountOffset);
        if (first_mount != expected_first_mount
            || package_mount_count > mount_count - first_mount
            || first_event != expected_first_event
            || package_event_count > event_count - first_event
            || entry[pathguard::binary_format::kPackageFailureModeOffset] > 1
            || entry[pathguard::binary_format::kPackageMediaCompatOffset] > 1
            || entry[pathguard::binary_format::kPackageProviderCompatOffset] > 1
            || entry[43] != 0 || ReadLe32(entry + 44) != 0) {
            return false;
        }
        expected_first_mount += package_mount_count;
        expected_first_event += package_event_count;
    }
    if (expected_first_mount != mount_count || expected_first_event != event_count) {
        return false;
    }
    for (uint32_t index = 0; index < mount_count; ++index) {
        const auto* rule = data + mount_offset + index * kMountRuleEntrySize;
        if (rule[pathguard::binary_format::kMountActionOffset] > 3
            || rule[1] != 0 || ReadLe16(rule + 6) != 0
            || ReadPolicyString(data, size, string_offset,
                ReadLe32(rule + pathguard::binary_format::kMountVisiblePathOffset)) == nullptr
            || ReadPolicyString(data, size, string_offset,
                ReadLe32(rule + pathguard::binary_format::kMountBackingPathOffset)) == nullptr) {
            return false;
        }
    }
    for (uint32_t index = 0; index < event_count; ++index) {
        const auto* rule = data + event_offset + index * kEventRuleEntrySize;
        if (rule[pathguard::binary_format::kEventActionOffset] > 1
            || rule[1] != 0 || ReadLe16(rule + 2) != 0
            || ReadPolicyString(data, size, string_offset,
                ReadLe32(rule + pathguard::binary_format::kEventSourcePathOffset)) == nullptr
            || ReadPolicyString(data, size, string_offset,
                ReadLe32(rule + pathguard::binary_format::kEventTargetPathOffset)) == nullptr) {
            return false;
        }
    }
    return true;
}

bool LoadProcessPlan(int module_dir, const char* process_name, jint uid,
                     ProcessPlan* plan, PolicyLoadPerf* perf) {
    if (plan == nullptr || process_name == nullptr
        || strlen(process_name) >= kMaxProcessNameBytes) return false;
    const bool diagnostic = process_name != nullptr
        && strcmp(process_name, kDiagnosticPackage) == 0;
    *plan = {};
    plan->path_bytes = 1;
    snprintf(plan->process_name, sizeof(plan->process_name), "%s", process_name);
    const uint64_t open_map_started = pathguard::perf::NowNs();
    const int policy_fd = openat(module_dir, kPolicyPath, O_RDONLY | O_CLOEXEC);
    if (policy_fd < 0) {
        if (diagnostic) LOGE("policy_open_failed errno=%d", errno);
        if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
        return false;
    }

    struct stat file_stat {};
    if (fstat(policy_fd, &file_stat) != 0 || file_stat.st_size < static_cast<off_t>(kPolicyHeaderSize)) {
        if (diagnostic) LOGE("policy_stat_invalid errno=%d size=%lld", errno,
                             static_cast<long long>(file_stat.st_size));
        close(policy_fd);
        if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
        return false;
    }
    const size_t size = static_cast<size_t>(file_stat.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, policy_fd, 0);
    close(policy_fd);
    if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
    if (mapping == MAP_FAILED) {
        if (diagnostic) LOGE("policy_mmap_failed errno=%d", errno);
        return false;
    }

    const uint64_t lookup_started = pathguard::perf::NowNs();
    const auto finish = [&](bool result) {
        if (perf != nullptr) perf->lookup_ns = pathguard::perf::ElapsedNs(lookup_started);
        const uint64_t unmap_started = pathguard::perf::NowNs();
        munmap(mapping, size);
        if (perf != nullptr) perf->unmap_ns = pathguard::perf::ElapsedNs(unmap_started);
        return result;
    };

    const auto* data = static_cast<const uint8_t*>(mapping);
    const uint32_t file_size = ReadLe32(
        data + pathguard::binary_format::kFileSizeOffset);
    const uint32_t payload_checksum = ReadLe32(
        data + pathguard::binary_format::kPayloadChecksumOffset);
    const uint32_t package_count = ReadLe32(
        data + pathguard::binary_format::kPackageCountOffset);
    const uint32_t mount_count = ReadLe32(
        data + pathguard::binary_format::kMountRuleCountOffset);
    const uint32_t event_count = ReadLe32(
        data + pathguard::binary_format::kEventRuleCountOffset);
    const uint32_t package_offset = ReadLe32(
        data + pathguard::binary_format::kPackageTableOffset);
    const uint32_t mount_offset = ReadLe32(
        data + pathguard::binary_format::kMountRuleTableOffset);
    const uint32_t event_offset = ReadLe32(
        data + pathguard::binary_format::kEventRuleTableOffset);
    const uint32_t string_offset = ReadLe32(
        data + pathguard::binary_format::kStringTableOffset);
    const uint32_t policy_flags = ReadLe32(
        data + pathguard::binary_format::kHeaderFlagsOffset);
    const uint64_t expected_mount_offset = kPolicyHeaderSize
        + static_cast<uint64_t>(package_count) * kPackageEntrySize;
    const uint64_t expected_event_offset = expected_mount_offset
        + static_cast<uint64_t>(mount_count) * kMountRuleEntrySize;
    const uint64_t expected_string_offset = expected_event_offset
        + static_cast<uint64_t>(event_count) * kEventRuleEntrySize;
    const bool valid_header = ReadLe32(data) == kPolicyMagic
        && ReadLe16(data + 4) == kPolicyFormatVersion
        && ReadLe16(data + 6) == pathguard::binary_format::kSchemaVersion
        && file_size == size
        && package_count > 0
        && package_offset == kPolicyHeaderSize
        && mount_offset == expected_mount_offset
        && event_offset == expected_event_offset
        && string_offset == expected_string_offset
        && string_offset < size
        && (policy_flags
            & ~pathguard::binary_format::kPolicyFlagAllowLegacyStringBind) == 0
        && pathguard::binary_format::Crc32(data + kPolicyHeaderSize,
                                          size - kPolicyHeaderSize)
            == payload_checksum
        && ValidPackageIndex(data, size, package_count, package_offset,
                             string_offset)
        && ValidPolicyTables(data, size, package_count, mount_count, event_count,
                             package_offset, mount_offset, event_offset,
                             string_offset);
    if (!valid_header) {
        if (diagnostic) {
            LOGE("policy_header_invalid size=%zu packages=%u mounts=%u events=%u flags=%u",
                 size, package_count, mount_count, event_count, policy_flags);
        }
        return finish(false);
    }

    char user_id[16];
    snprintf(user_id, sizeof(user_id), "%d", uid / 100000);
    const char* process_separator = strchr(process_name, ':');
    const size_t package_length = process_separator == nullptr
        ? strlen(process_name)
        : static_cast<size_t>(process_separator - process_name);
    if (package_length == 0) return finish(false);
    const pathguard::binary_format::PolicyIndexView index{
        data, size, package_count, package_offset, mount_offset, string_offset,
    };
    const auto* entry = pathguard::binary_format::FindPackageEntry(
        index, process_name, package_length);
    if (entry == nullptr) {
        if (diagnostic) LOGE("policy_package_miss uid=%d", uid);
        return finish(false);
    }
    const char* package_name = ReadPolicyString(
        data, size, string_offset,
        ReadLe32(entry + pathguard::binary_format::kPackageNameOffset));
    const char* users = ReadPolicyString(
        data, size, string_offset,
        ReadLe32(entry + pathguard::binary_format::kPackageUsersOffset));
    const char* processes = ReadPolicyString(
        data, size, string_offset,
        ReadLe32(entry + pathguard::binary_format::kPackageProcessesOffset));
    const uint32_t first_rule = ReadLe32(
        entry + pathguard::binary_format::kPackageFirstMountOffset);
    const uint32_t rule_count = ReadLe32(
        entry + pathguard::binary_format::kPackageMountCountOffset);
    const uint32_t first_event = ReadLe32(
        entry + pathguard::binary_format::kPackageFirstEventOffset);
    const uint32_t app_event_count = ReadLe32(
        entry + pathguard::binary_format::kPackageEventCountOffset);
    const uint8_t failure_mode = entry[
        pathguard::binary_format::kPackageFailureModeOffset];
    const uint8_t media_compat = entry[
        pathguard::binary_format::kPackageMediaCompatOffset];
    const uint8_t provider_compat = entry[
        pathguard::binary_format::kPackageProviderCompatOffset];
    if (failure_mode != 0 || media_compat > 1 || provider_compat > 1
        || first_event > event_count
        || app_event_count > event_count - first_event || app_event_count != 0
        || entry[43] != 0 || ReadLe32(entry + 44) != 0) {
        if (diagnostic) LOGE("policy_package_entry_invalid");
        return finish(false);
    }
    plan->snapshot_generation = ReadLe64(
        data + pathguard::binary_format::kContentGenerationOffset);
    plan->policy_flags = policy_flags;
    plan->plan_generation = ReadLe64(
        entry + pathguard::binary_format::kPackagePlanGenerationOffset);
    plan->media_compat = media_compat == 1;
    plan->provider_compat = provider_compat == 1;
    if (package_name == nullptr || users == nullptr || processes == nullptr
        || !ProcessMatches(package_name, processes, process_name)
        || !ListContains(users, user_id)) {
        if (diagnostic) {
            LOGE("policy_scope_miss user=%s users=%s processes=%s",
                 user_id, users == nullptr ? "<null>" : users,
                 processes == nullptr ? "<null>" : processes);
        }
        return finish(false);
    }
    if (first_rule > mount_count || rule_count > mount_count - first_rule
        || rule_count > kMaxMountRules) {
        if (diagnostic) LOGE("policy_rule_range_invalid first=%u count=%u total=%u",
                             first_rule, rule_count, mount_count);
        return finish(false);
    }
    for (uint32_t rule_index = 0; rule_index < rule_count; ++rule_index) {
        const auto* rule = data + mount_offset
            + (first_rule + rule_index) * kMountRuleEntrySize;
        const uint8_t action = rule[pathguard::binary_format::kMountActionOffset];
        const char* visible = ReadPolicyString(
            data, size, string_offset,
            ReadLe32(rule + pathguard::binary_format::kMountVisiblePathOffset));
        const char* backing = ReadPolicyString(
            data, size, string_offset,
            ReadLe32(rule + pathguard::binary_format::kMountBackingPathOffset));
        char expanded_backing[PATH_MAX]{};
        const bool deny = action == kDenyAction;
        if (!IsExecutableMountAction(action)
            || rule[1] != 0 || ReadLe16(rule + 6) != 0
            || visible == nullptr || backing == nullptr
            || !IsAllowedTarget(visible)
            || (deny ? backing[0] != '\0'
                     : !ExpandRuntimePath(
                           backing, static_cast<uint32_t>(uid) / 100000u,
                           expanded_backing, sizeof(expanded_backing)))
            || strlen(visible) >= PATH_MAX) {
            plan->count = 0;
            if (diagnostic) LOGE("policy_rule_invalid index=%u action=%u",
                                 rule_index, action);
            return finish(false);
        }
        PlannedMount& mount = plan->mounts[plan->count++];
        mount.action = action;
        if (!StorePlanPath(plan, visible, &mount.visible_path)
            || !StorePlanPath(plan, deny ? "" : expanded_backing,
                              &mount.backing_path)) {
            plan->count = 0;
            if (diagnostic) LOGE("policy_plan_path_overflow index=%u", rule_index);
            return finish(false);
        }
    }
    if (diagnostic) {
        LOGI("policy_plan_loaded uid=%d rules=%u flags=%u", uid, plan->count,
             plan->policy_flags);
    }
    return finish(plan->count > 0);
}

bool SameProcessPlan(const ProcessPlan& expected, const ProcessPlan& actual) {
    if (expected.snapshot_generation != actual.snapshot_generation
        || expected.plan_generation != actual.plan_generation
        || expected.policy_flags != actual.policy_flags
        || expected.count != actual.count
        || expected.media_compat != actual.media_compat
        || expected.provider_compat != actual.provider_compat
        || strcmp(expected.process_name, actual.process_name) != 0) {
        return false;
    }
    for (uint32_t index = 0; index < expected.count; ++index) {
        const PlannedMount& lhs = expected.mounts[index];
        const PlannedMount& rhs = actual.mounts[index];
        const char* lhs_visible = PlanPath(expected, lhs.visible_path);
        const char* lhs_backing = PlanPath(expected, lhs.backing_path);
        const char* rhs_visible = PlanPath(actual, rhs.visible_path);
        const char* rhs_backing = PlanPath(actual, rhs.backing_path);
        if (lhs.action != rhs.action || lhs_visible == nullptr || lhs_backing == nullptr
            || rhs_visible == nullptr || rhs_backing == nullptr
            || strcmp(lhs_visible, rhs_visible) != 0
            || strcmp(lhs_backing, rhs_backing) != 0) {
            return false;
        }
    }
    return true;
}

bool LoadProviderRules(int module_dir,
                       pathguard::provider_redirect::Rule* rules,
                       uint32_t capacity, uint32_t* rule_count) {
    if (rules == nullptr || rule_count == nullptr) return false;
    *rule_count = 0;
    const int policy_fd = openat(module_dir, kPolicyPath, O_RDONLY | O_CLOEXEC);
    if (policy_fd < 0) return false;
    struct stat file_stat {};
    if (fstat(policy_fd, &file_stat) != 0
        || file_stat.st_size < static_cast<off_t>(kPolicyHeaderSize)) {
        close(policy_fd);
        return false;
    }
    const size_t size = static_cast<size_t>(file_stat.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, policy_fd, 0);
    close(policy_fd);
    if (mapping == MAP_FAILED) return false;

    const auto* data = static_cast<const uint8_t*>(mapping);
    const uint32_t package_count = ReadLe32(
        data + pathguard::binary_format::kPackageCountOffset);
    const uint32_t mount_count = ReadLe32(
        data + pathguard::binary_format::kMountRuleCountOffset);
    const uint32_t event_count = ReadLe32(
        data + pathguard::binary_format::kEventRuleCountOffset);
    const uint32_t package_offset = ReadLe32(
        data + pathguard::binary_format::kPackageTableOffset);
    const uint32_t mount_offset = ReadLe32(
        data + pathguard::binary_format::kMountRuleTableOffset);
    const uint32_t event_offset = ReadLe32(
        data + pathguard::binary_format::kEventRuleTableOffset);
    const uint32_t string_offset = ReadLe32(
        data + pathguard::binary_format::kStringTableOffset);
    const uint32_t flags = ReadLe32(data + pathguard::binary_format::kHeaderFlagsOffset);
    const bool valid = ReadLe32(data) == kPolicyMagic
        && ReadLe16(data + 4) == kPolicyFormatVersion
        && ReadLe16(data + 6) == pathguard::binary_format::kSchemaVersion
        && ReadLe32(data + pathguard::binary_format::kFileSizeOffset) == size
        && package_count > 0
        && package_offset == kPolicyHeaderSize
        && mount_offset == package_offset + package_count * kPackageEntrySize
        && event_offset == mount_offset + mount_count * kMountRuleEntrySize
        && string_offset == event_offset + event_count * kEventRuleEntrySize
        && string_offset < size
        && (flags & ~pathguard::binary_format::kPolicyFlagAllowLegacyStringBind) == 0
        && pathguard::binary_format::Crc32(data + kPolicyHeaderSize,
                                          size - kPolicyHeaderSize)
            == ReadLe32(data + pathguard::binary_format::kPayloadChecksumOffset)
        && ValidPackageIndex(data, size, package_count, package_offset, string_offset)
        && ValidPolicyTables(data, size, package_count, mount_count, event_count,
                             package_offset, mount_offset, event_offset, string_offset);
    if (!valid) {
        munmap(mapping, size);
        return false;
    }

    for (uint32_t package_index = 0; package_index < package_count; ++package_index) {
        const auto* entry = data + package_offset
            + package_index * kPackageEntrySize;
        if (entry[pathguard::binary_format::kPackageProviderCompatOffset] != 1) continue;
        const char* package_name = ReadPolicyString(
            data, size, string_offset,
            ReadLe32(entry + pathguard::binary_format::kPackageNameOffset));
        const char* users = ReadPolicyString(
            data, size, string_offset,
            ReadLe32(entry + pathguard::binary_format::kPackageUsersOffset));
        const uint32_t first_mount = ReadLe32(
            entry + pathguard::binary_format::kPackageFirstMountOffset);
        const uint32_t app_mount_count = ReadLe32(
            entry + pathguard::binary_format::kPackageMountCountOffset);
        if (package_name == nullptr || users == nullptr || strchr(users, '*') != nullptr) {
            LOGE("provider redirect requires explicit numeric users");
            continue;
        }
        const char* user = users;
        while (*user != '\0') {
            char* end = nullptr;
            const long user_id = strtol(user, &end, 10);
            if (end == user || user_id < 0 || user_id > 99999
                || (*end != '\0' && *end != ',')) {
                break;
            }
            char package_data_path[PATH_MAX]{};
            struct stat package_stat {};
            const int package_path_written = snprintf(
                package_data_path, sizeof(package_data_path), "/data/user/%ld/%s",
                user_id, package_name);
            if (package_path_written <= 0
                || static_cast<size_t>(package_path_written) >= sizeof(package_data_path)
                || stat(package_data_path, &package_stat) != 0
                || package_stat.st_uid < 10000) {
                LOGE("provider redirect package uid resolution failed: package=%s user=%ld errno=%d",
                     package_name, user_id, errno);
                if (*end == '\0') break;
                user = end + 1;
                continue;
            }
            const int32_t caller_uid = static_cast<int32_t>(package_stat.st_uid);
            for (uint32_t mount_index = 0; mount_index < app_mount_count; ++mount_index) {
                if (*rule_count >= capacity) break;
                const auto* mount = data + mount_offset
                    + (first_mount + mount_index) * kMountRuleEntrySize;
                if (mount[pathguard::binary_format::kMountActionOffset]
                    != kRedirectAction) {
                    continue;
                }
                const char* visible = ReadPolicyString(
                    data, size, string_offset,
                    ReadLe32(mount + pathguard::binary_format::kMountVisiblePathOffset));
                const char* backing = ReadPolicyString(
                    data, size, string_offset,
                    ReadLe32(mount + pathguard::binary_format::kMountBackingPathOffset));
                char expanded_backing[PATH_MAX]{};
                if (visible == nullptr || backing == nullptr
                    || strlen(visible) >= PATH_MAX
                    || !IsAllowedTarget(visible)
                    || !ExpandRuntimePath(
                        backing, static_cast<uint32_t>(user_id), expanded_backing,
                        sizeof(expanded_backing))) {
                    continue;
                }
                auto& output = rules[*rule_count];
                output.caller_uid = caller_uid;
                output.user_id = static_cast<uint32_t>(user_id);
                snprintf(output.visible_path, sizeof(output.visible_path), "%s", visible);
                snprintf(output.backing_path, sizeof(output.backing_path), "%s",
                         expanded_backing);
                ++*rule_count;
            }
            if (*end == '\0') break;
            user = end + 1;
        }
    }
    munmap(mapping, size);
    return *rule_count > 0;
}

bool HasSafePathComponents(const char* path) {
    if (path == nullptr || path[0] == '\0' || path[0] == '/') return false;
    const char* component = path;
    while (*component != '\0') {
        const char* separator = strchr(component, '/');
        const size_t length = separator == nullptr
            ? strlen(component)
            : static_cast<size_t>(separator - component);
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
    return HasSafePathComponents(path)
        && strchr(path, '{') == nullptr && strchr(path, '}') == nullptr;
}

bool ExpandRuntimePath(const char* input, uint32_t user_id,
                       char* output, size_t capacity) {
    if (input == nullptr || output == nullptr || capacity == 0) return false;
    char user[16]{};
    const int user_length = snprintf(user, sizeof(user), "%u", user_id);
    if (user_length <= 0 || static_cast<size_t>(user_length) >= sizeof(user)) {
        return false;
    }
    size_t written = 0;
    const char* current = input;
    while (*current != '\0') {
        if (strncmp(current, "{user}", 6) == 0) {
            if (written + static_cast<size_t>(user_length) >= capacity) return false;
            memcpy(output + written, user, static_cast<size_t>(user_length));
            written += static_cast<size_t>(user_length);
            current += 6;
            continue;
        }
        if (*current == '{' || *current == '}' || written + 1 >= capacity) return false;
        output[written++] = *current++;
    }
    output[written] = '\0';
    return IsAllowedTarget(output);
}

bool BuildPathUnderRoot(const char* root, const char* logical_path,
                        char* output, size_t output_size) {
    if (root == nullptr || root[0] != '/' || !IsAllowedTarget(logical_path)
        || output == nullptr || output_size == 0) {
        return false;
    }
    const int written = snprintf(output, output_size, "%s/%s", root, logical_path);
    return written >= 0 && static_cast<size_t>(written) < output_size;
}

uint64_t TopologyGeneration(const RuntimeStorageTopology& topology) {
    char canonical[PATH_MAX * 2 + 512]{};
    const int written = snprintf(
        canonical, sizeof(canonical),
        "PGTOPO1|%u|%s|%s|%llu|%llu|%llu|%s|%s|%llu|%llu|%llu|%s|%s",
        topology.user_id, topology.visible_root, topology.source_root,
        static_cast<unsigned long long>(topology.visible_identity.mount_id),
        static_cast<unsigned long long>(topology.visible_identity.parent_mount_id),
        static_cast<unsigned long long>(topology.visible_identity.device),
        topology.visible_identity.root, topology.visible_identity.filesystem,
        static_cast<unsigned long long>(topology.source_identity.mount_id),
        static_cast<unsigned long long>(topology.source_identity.parent_mount_id),
        static_cast<unsigned long long>(topology.source_identity.device),
        topology.source_identity.root, topology.source_identity.filesystem);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(canonical)) return 0;
    return pathguard::binary_format::Fnv1a64(
        reinterpret_cast<const uint8_t*>(canonical), static_cast<size_t>(written));
}

bool CaptureStorageTopologyFromSnapshot(
    uid_t uid, const char* required_source_root,
    const pathguard::MountInfoSnapshot& snapshot,
    RuntimeStorageTopology* topology) {
    if (topology == nullptr) return false;
    *topology = {};
    topology->user_id = static_cast<uint32_t>(uid) / 100000u;
    if (snprintf(topology->visible_root, sizeof(topology->visible_root),
                 "/storage/emulated/%u", topology->user_id) <= 0) {
        return false;
    }
    pathguard::PinnedIdentity visible;
    if (pathguard::PinDirectory(
            snapshot, topology->visible_root, &visible) != 0) {
        return false;
    }
    topology->visible_identity = visible.mount;
    pathguard::ClosePinnedIdentity(&visible);
    char candidates[4][PATH_MAX]{};
    size_t candidate_count = 0;
    if (required_source_root != nullptr && required_source_root[0] != '\0') {
        snprintf(candidates[candidate_count++], PATH_MAX, "%s", required_source_root);
    } else {
        snprintf(candidates[candidate_count++], PATH_MAX,
                 "/mnt/user/%u/emulated/%u", topology->user_id, topology->user_id);
        snprintf(candidates[candidate_count++], PATH_MAX,
                 "/mnt/runtime/default/emulated/%u", topology->user_id);
        snprintf(candidates[candidate_count++], PATH_MAX,
                 "/mnt/pass_through/%u/emulated/%u", topology->user_id,
                 topology->user_id);
    }
    for (size_t index = 0; index < candidate_count; ++index) {
        pathguard::PinnedIdentity pinned;
        if (pathguard::PinDirectory(
                snapshot, candidates[index], &pinned) != 0) {
            continue;
        }
        const bool same_plane = pinned.device == topology->visible_identity.device
            && strcmp(pinned.mount.filesystem,
                      topology->visible_identity.filesystem) == 0;
        if (same_plane) {
            snprintf(topology->source_root, sizeof(topology->source_root), "%s",
                     candidates[index]);
            topology->source_identity = pinned.mount;
        }
        pathguard::ClosePinnedIdentity(&pinned);
        if (same_plane) break;
    }
    if (topology->source_root[0] == '\0') return false;
    topology->generation = TopologyGeneration(*topology);
    return topology->generation != 0;
}

bool SameStorageTopology(const RuntimeStorageTopology& expected,
                         const RuntimeStorageTopology& actual) {
    return expected.generation != 0 && actual.generation != 0
        && expected.user_id == actual.user_id
        && strcmp(expected.visible_root, actual.visible_root) == 0
        && strcmp(expected.source_root, actual.source_root) == 0
        && expected.visible_identity.device == actual.visible_identity.device
        && expected.source_identity.device == actual.source_identity.device
        && strcmp(expected.visible_identity.root, actual.visible_identity.root) == 0
        && strcmp(expected.source_identity.root, actual.source_identity.root) == 0
        && strcmp(expected.visible_identity.filesystem,
                  actual.visible_identity.filesystem) == 0
        && strcmp(expected.source_identity.filesystem,
                  actual.source_identity.filesystem) == 0;
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

bool ReadProcessStartTime(pid_t pid, uint64_t* start_time) {
    if (start_time == nullptr) return false;
    char stat_path[64]{};
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
    FILE* input = fopen(stat_path, "re");
    if (input == nullptr) return false;
    char line[4096]{};
    const bool read = fgets(line, sizeof(line), input) != nullptr;
    fclose(input);
    if (!read) return false;
    char* current = strrchr(line, ')');
    if (current == nullptr || current[1] != ' ') return false;
    current += 2;
    for (int field = 3; field < 22; ++field) {
        current = strchr(current, ' ');
        if (current == nullptr) return false;
        while (*current == ' ') ++current;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = strtoull(current, &end, 10);
    if (errno != 0 || end == current) return false;
    *start_time = static_cast<uint64_t>(value);
    return true;
}

bool ReadProcessContext(pid_t pid, char* context, size_t context_size) {
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

using MountState = pathguard::MountTransactionState;

const char* EnforcementName(pathguard::EnforcementState value) {
    switch (value) {
        case pathguard::EnforcementState::kInactive: return "inactive";
        case pathguard::EnforcementState::kActive: return "active";
        case pathguard::EnforcementState::kPendingRestart: return "pending_restart";
        case pathguard::EnforcementState::kFailed: return "failed";
    }
    return "failed";
}

const char* TransactionName(pathguard::TransactionOutcome value) {
    switch (value) {
        case pathguard::TransactionOutcome::kNone: return "none";
        case pathguard::TransactionOutcome::kComplete: return "complete";
        case pathguard::TransactionOutcome::kFailedPreflight: return "failed_preflight";
        case pathguard::TransactionOutcome::kRollbackComplete: return "rollback_complete";
        case pathguard::TransactionOutcome::kNamespaceTainted: return "namespace_tainted";
    }
    return "none";
}

const char* SecurityName(pathguard::SecurityLevel value) {
    switch (value) {
        case pathguard::SecurityLevel::kNone: return "none";
        case pathguard::SecurityLevel::kFdPinned: return "fd_pinned";
        case pathguard::SecurityLevel::kLegacyToctou: return "legacy_toctou";
    }
    return "none";
}

const char* ReasonName(pathguard::RuntimeReason value) {
    switch (value) {
        case pathguard::RuntimeReason::kNone: return "none";
        case pathguard::RuntimeReason::kCapabilityMissing: return "capability_missing";
        case pathguard::RuntimeReason::kLegacyNotAuthorized: return "legacy_not_authorized";
        case pathguard::RuntimeReason::kUnsupportedAction: return "unsupported_action";
        case pathguard::RuntimeReason::kTopologyChanged: return "topology_changed";
        case pathguard::RuntimeReason::kPolicyChanged: return "policy_changed";
        case pathguard::RuntimeReason::kPreflightFailed: return "preflight_failed";
        case pathguard::RuntimeReason::kApplyFailed: return "apply_failed";
        case pathguard::RuntimeReason::kRollbackFailed: return "rollback_failed";
        case pathguard::RuntimeReason::kOwnerDeath: return "owner_death";
    }
    return "preflight_failed";
}

void WriteRuntimeStatus(int module_dir_fd, pid_t pid, uid_t uid,
                        const ProcessPlan& plan, const MountPerfResult& mount,
                        MountState state) {
    if (module_dir_fd < 0 || pid <= 0) return;
    pathguard::RuntimeStatusRecord status;
    status.pid = pid;
    status.uid = uid;
    ReadProcessStartTime(pid, &status.process_start_time);
    status.snapshot_generation = plan.snapshot_generation;
    status.plan_generation = plan.plan_generation;
    status.topology_generation = plan.topology.generation;
    status.backend = static_cast<uint8_t>(mount.backend);
    if (state == MountState::kComplete) {
        status.enforcement = pathguard::EnforcementState::kActive;
        status.transaction = pathguard::TransactionOutcome::kComplete;
    } else {
        status.enforcement = pathguard::EnforcementState::kFailed;
        status.transaction = state == MountState::kNamespaceTainted
            ? pathguard::TransactionOutcome::kNamespaceTainted
            : state == MountState::kRollbackComplete
                ? pathguard::TransactionOutcome::kRollbackComplete
                : pathguard::TransactionOutcome::kFailedPreflight;
    }
    status.security = mount.backend
            == static_cast<uint32_t>(pathguard::MountBackendKind::kLegacyString)
        ? pathguard::SecurityLevel::kLegacyToctou
        : mount.backend == static_cast<uint32_t>(
              pathguard::MountBackendKind::kStrictOpenTree)
              || mount.backend == static_cast<uint32_t>(
                  pathguard::MountBackendKind::kStrictProcFd)
            ? pathguard::SecurityLevel::kFdPinned
            : pathguard::SecurityLevel::kNone;
    if (mount.runtime_reason != 0) {
        status.reason = static_cast<pathguard::RuntimeReason>(mount.runtime_reason);
    } else if (mount.result == EOWNERDEAD) {
        status.reason = pathguard::RuntimeReason::kOwnerDeath;
    } else if (mount.rollback_failed != 0
               || state == MountState::kNamespaceTainted) {
        status.reason = pathguard::RuntimeReason::kRollbackFailed;
    } else if (mount.backend_reason == static_cast<uint32_t>(
                   pathguard::MountBackendReason::kLegacyNotAuthorized)) {
        status.reason = pathguard::RuntimeReason::kLegacyNotAuthorized;
    } else if (mount.backend_reason == static_cast<uint32_t>(
                   pathguard::MountBackendReason::kUnsupportedAction)) {
        status.reason = pathguard::RuntimeReason::kUnsupportedAction;
    } else if (mount.backend_reason == static_cast<uint32_t>(
                   pathguard::MountBackendReason::kCapabilityMissing)) {
        status.reason = pathguard::RuntimeReason::kCapabilityMissing;
    } else if (mount.result == ESTALE) {
        status.reason = pathguard::RuntimeReason::kPolicyChanged;
    } else if (mount.failure_stage != 0) {
        status.reason = pathguard::RuntimeReason::kApplyFailed;
    } else if (mount.result != 0) {
        status.reason = pathguard::RuntimeReason::kPreflightFailed;
    }
    status.error = mount.result;

    if (mkdirat(module_dir_fd, "run/status", 0755) != 0 && errno != EEXIST) return;
    char temporary[96]{};
    char final_path[64]{};
    snprintf(temporary, sizeof(temporary), "run/status/.%d.%d.tmp", pid, getpid());
    snprintf(final_path, sizeof(final_path), "run/status/%d.status", pid);
    const int output = openat(module_dir_fd, temporary,
                              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (output < 0) return;
    char text[1536]{};
    const int length = snprintf(
        text, sizeof(text),
        "version=1\npid=%d\nuid=%u\nprocess_start_time=%llu\nprocess=%s\n"
        "enforcement=%s\nbackend=%u\ntransaction=%s\nsecurity=%s\nreason=%s\n"
        "error=%d\nsnapshot_generation=%llu\nplan_generation=%llu\n"
        "topology_generation=%llu\n",
        pid, uid, static_cast<unsigned long long>(status.process_start_time),
        plan.process_name, EnforcementName(status.enforcement), status.backend,
        TransactionName(status.transaction), SecurityName(status.security),
        ReasonName(status.reason), status.error,
        static_cast<unsigned long long>(status.snapshot_generation),
        static_cast<unsigned long long>(status.plan_generation),
        static_cast<unsigned long long>(status.topology_generation));
    const bool written = length > 0 && static_cast<size_t>(length) < sizeof(text)
        && WriteRegularFileFully(output, text, static_cast<size_t>(length));
    close(output);
    if (!written || renameat(module_dir_fd, temporary,
                             module_dir_fd, final_path) != 0) {
        unlinkat(module_dir_fd, temporary, 0);
    }
}

uint32_t StateValue(MountState state) {
    return static_cast<uint32_t>(state);
}

MountState LoadSharedStatus(const SharedMountState* state) {
    return static_cast<MountState>(
        __atomic_load_n(&state->status, __ATOMIC_ACQUIRE));
}

pathguard::MountPreflightPhase LoadSharedPreflightPhase(
        const SharedMountState* state) {
    return static_cast<pathguard::MountPreflightPhase>(
        __atomic_load_n(&state->preflight_phase, __ATOMIC_ACQUIRE));
}

void StoreSharedPreflightPhase(SharedMountState* state,
                               pathguard::MountPreflightPhase phase) {
    __atomic_store_n(&state->preflight_phase, static_cast<uint32_t>(phase),
                     __ATOMIC_RELEASE);
}

bool IsCancelRequested(const SharedMountState* state) {
    return state != nullptr
        && LoadSharedStatus(state) == MountState::kCancelRequested;
}

void WakeSharedState(SharedMountState* state) {
    syscall(SYS_futex, &state->status, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

bool TransitionSharedState(SharedMountState* state, MountState from, MountState to) {
    if (state == nullptr || !pathguard::IsMountTransitionAllowed(from, to)) return false;
    uint32_t expected = StateValue(from);
    const bool transitioned = __atomic_compare_exchange_n(
        &state->status, &expected, StateValue(to), false,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (transitioned) WakeSharedState(state);
    return transitioned;
}

bool PublishSharedResult(SharedMountState* state, const CompanionResult& result,
                         MountState from, MountState terminal) {
    state->result = result;
    return TransitionSharedState(state, from, terminal);
}

bool MarkSharedCancelled(SharedMountState* state) {
    return TransitionSharedState(
        state, MountState::kCancelRequested, MountState::kCancelled);
}

bool PublishPendingFailure(SharedMountState* state, const CompanionResult& result) {
    if (PublishSharedResult(
            state, result, MountState::kPending, MountState::kFailed)) {
        return true;
    }
    if (LoadSharedStatus(state) == MountState::kCancelRequested) {
        MarkSharedCancelled(state);
    }
    return false;
}

bool AcquireMutationLease(SharedMountState* state) {
    if (TransitionSharedState(
            state, MountState::kPending, MountState::kApplying)) {
        return true;
    }
    if (LoadSharedStatus(state) == MountState::kCancelRequested) {
        MarkSharedCancelled(state);
    }
    return false;
}

enum class ProcessReadyResult {
    kReady,
    kTimedOut,
    kCancelled,
};

ProcessReadyResult WaitForProcessReady(pid_t pid, uid_t expected_uid,
                                       SharedMountState* state) {
    int elapsed = 0;
    int delay_ms = 1;
    while (elapsed < kProcessReadyTimeoutMs) {
        if (IsCancelRequested(state)) return ProcessReadyResult::kCancelled;
        char context[256]{};
        if (ReadProcessUid(pid, expected_uid)
            && ReadProcessContext(pid, context, sizeof(context))
            && strstr(context, "zygote") == nullptr) {
            return ProcessReadyResult::kReady;
        }
        usleep(static_cast<useconds_t>(delay_ms) * 1000u);
        elapsed += delay_ms;
        if (delay_ms < 10) delay_ms = delay_ms >= 5 ? 10 : delay_ms * 2;
    }
    return ProcessReadyResult::kTimedOut;
}

pathguard::MountBackendProbe ProbeMountBackendsIsolated(
        const char* source_path, const char* target_path) {
    pathguard::MountBackendProbe probe;
    int sockets[2]{};
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
        probe.error = errno;
        return probe;
    }
    const pid_t child = fork();
    if (child == 0) {
        close(sockets[0]);
        probe.uid = static_cast<uint32_t>(getuid());
        probe.euid = static_cast<uint32_t>(geteuid());
        struct stat namespace_stat {};
        if (stat("/proc/self/ns/mnt", &namespace_stat) == 0) {
            probe.namespace_before = namespace_stat.st_ino;
        }
        FILE* status = fopen("/proc/self/status", "re");
        if (status != nullptr) {
            char line[256]{};
            while (fgets(line, sizeof(line), status) != nullptr) {
                unsigned long long value = 0;
                if (sscanf(line, "CapEff:\t%llx", &value) == 1) {
                    probe.cap_effective = value;
                } else if (sscanf(line, "CapBnd:\t%llx", &value) == 1) {
                    probe.cap_bounding = value;
                }
            }
            fclose(status);
        }
        FILE* context = fopen("/proc/self/attr/current", "re");
        if (context != nullptr) {
            if (fgets(probe.selinux_context, sizeof(probe.selinux_context), context)) {
                probe.selinux_context[strcspn(
                    probe.selinux_context, "\r\n")] = '\0';
            }
            fclose(context);
        }
        if (unshare(CLONE_NEWNS) != 0) {
            probe.unshare_error = errno;
            probe.error = probe.unshare_error;
        } else if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
            probe.private_error = errno;
            probe.error = probe.private_error;
        } else {
            if (stat("/proc/self/ns/mnt", &namespace_stat) == 0) {
                probe.namespace_after = namespace_stat.st_ino;
            }
            const uint32_t uid = probe.uid;
            const uint32_t euid = probe.euid;
            const uint64_t cap_effective = probe.cap_effective;
            const uint64_t cap_bounding = probe.cap_bounding;
            const uint64_t namespace_before = probe.namespace_before;
            const uint64_t namespace_after = probe.namespace_after;
            char selinux_context[sizeof(probe.selinux_context)]{};
            memcpy(selinux_context, probe.selinux_context,
                   sizeof(selinux_context));
            probe = pathguard::ProbeDirectoryMountBackends(
                source_path, target_path);
            probe.uid = uid;
            probe.euid = euid;
            probe.cap_effective = cap_effective;
            probe.cap_bounding = cap_bounding;
            probe.namespace_before = namespace_before;
            probe.namespace_after = namespace_after;
            memcpy(probe.selinux_context, selinux_context,
                   sizeof(probe.selinux_context));
        }
        send(sockets[1], &probe, sizeof(probe), MSG_NOSIGNAL);
        close(sockets[1]);
        _exit(probe.error == 0 ? 0 : 1);
    }
    close(sockets[1]);
    if (child < 0 || !ReadFully(sockets[0], &probe, sizeof(probe))) {
        probe = {};
        probe.error = child < 0 ? errno : EIO;
    }
    close(sockets[0]);
    if (child > 0) {
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
    }
    return probe;
}

void LogProbeStep(const char* name,
                  const pathguard::MountBackendProbeStep& step) {
    LOGI("probe_step backend=%s before_errno=%d before_count=%llu "
         "apply_errno=%d stat_errno=%d identity=%u after_errno=%d "
         "after_count=%llu mount_id=%llu verify_errno=%d rollback_errno=%d "
         "remaining_errno=%d remaining_id=%llu success=%u",
         name, step.before_error,
         static_cast<unsigned long long>(step.before_count), step.apply_error,
         step.stat_error, step.identity_match, step.after_error,
         static_cast<unsigned long long>(step.after_count),
         static_cast<unsigned long long>(step.mounted_id), step.verify_error,
         step.rollback_error, step.remaining_error,
         static_cast<unsigned long long>(step.remaining_id), step.success);
}

// P0a: process-level capability probe cache. Per ADR-0005 the probe result
// binds to boot identity, SELinux environment and policy flags; it does NOT
// depend on the target namespace (ProbeDirectoryMountBackends forks its own
// isolated unshare+MS_PRIVATE namespace). Verified on-device: probe run in the
// companion global ns and inside an app ns produce identical primitives.
// The cache lives in the persistent companion parent process so forked
// per-request children inherit it via COW fork and never re-probe.
struct ProbeCacheKey {
    char boot_id[48];
    int selinux_enforce;
    uint32_t policy_flags;
    uint64_t selinux_policy_identity;
    uint64_t topology_generation;
};

struct ProbeCacheEntry {
    bool valid = false;
    ProbeCacheKey key{};
    pathguard::MountBackendProbe probe{};
};

ProbeCacheEntry g_probe_cache;

bool ReadBootId(char* output, size_t size) {
    if (output == nullptr || size == 0) return false;
    output[0] = '\0';
    FILE* input = fopen("/proc/sys/kernel/random/boot_id", "re");
    if (input == nullptr) return false;
    const char* result = fgets(output, static_cast<int>(size), input);
    fclose(input);
    if (result == nullptr) return false;
    output[strcspn(output, "\r\n")] = '\0';
    return output[0] != '\0';
}

int ReadSelinuxEnforce() {
    FILE* input = fopen("/sys/fs/selinux/enforce", "re");
    if (input == nullptr) return -1;
    int value = -1;
    if (fscanf(input, "%d", &value) != 1) value = -1;
    fclose(input);
    return value;
}

uint64_t ReadSelinuxPolicyIdentity() {
    struct stat value {};
    if (stat("/sys/fs/selinux/policy", &value) != 0) return 0;
    char canonical[192]{};
    const int written = snprintf(
        canonical, sizeof(canonical), "%llu:%llu:%llu:%llu",
        static_cast<unsigned long long>(value.st_dev),
        static_cast<unsigned long long>(value.st_ino),
        static_cast<unsigned long long>(value.st_size),
        static_cast<unsigned long long>(value.st_mtim.tv_sec));
    return written > 0 && static_cast<size_t>(written) < sizeof(canonical)
        ? pathguard::binary_format::Fnv1a64(
            reinterpret_cast<const uint8_t*>(canonical),
            static_cast<size_t>(written))
        : 0;
}

bool ProbeKeyMatches(const ProbeCacheKey& a, const ProbeCacheKey& b) {
    return a.selinux_enforce == b.selinux_enforce
        && a.policy_flags == b.policy_flags
        && a.selinux_policy_identity == b.selinux_policy_identity
        && a.topology_generation == b.topology_generation
        && strncmp(a.boot_id, b.boot_id, sizeof(a.boot_id)) == 0;
}

// Runs (or reuses) the isolated capability probe. Called from the persistent
// companion parent so the result is cached across requests. source_path and
// target_path only need to be a representative existing storage pair; the probe
// mounts inside its own throwaway namespace and the capability outcome is path
// independent on a given device/topology.
const pathguard::MountBackendProbe& GetOrProbeMountBackends(
        const char* source_path, const char* target_path,
        uint32_t policy_flags, uint64_t topology_generation, bool* cache_hit) {
    ProbeCacheKey key{};
    ReadBootId(key.boot_id, sizeof(key.boot_id));
    key.selinux_enforce = ReadSelinuxEnforce();
    key.policy_flags = policy_flags;
    key.selinux_policy_identity = ReadSelinuxPolicyIdentity();
    key.topology_generation = topology_generation;
    if (g_probe_cache.valid && ProbeKeyMatches(g_probe_cache.key, key)) {
        if (cache_hit != nullptr) *cache_hit = true;
        return g_probe_cache.probe;
    }
    g_probe_cache.probe = ProbeMountBackendsIsolated(source_path, target_path);
    g_probe_cache.key = key;
    g_probe_cache.valid = true;
    if (cache_hit != nullptr) *cache_hit = false;
    return g_probe_cache.probe;
}

bool StoragePropagationRequiresPrivate(
    const pathguard::MountInfoSnapshot& snapshot) {
    bool requires_private = true;
    if (pathguard::MountInfoSnapshotRequiresPrivate(
            snapshot, "/storage", &requires_private) != 0) {
        return true;
    }
    return requires_private;
}

struct NamespaceTerminationResult {
    size_t matched = 0;
    size_t signaled = 0;
    size_t remaining = 0;
};

size_t ScanNamespaceMembers(dev_t namespace_device, ino_t namespace_inode,
                            bool terminate, size_t* signaled) {
    DIR* proc = opendir("/proc");
    if (proc == nullptr) return 0;
    size_t matched = 0;
    while (dirent* entry = readdir(proc)) {
        char* end = nullptr;
        const long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || value <= 0
            || value == getpid()) {
            continue;
        }
        char path[64]{};
        snprintf(path, sizeof(path), "/proc/%ld/ns/mnt", value);
        struct stat identity {};
        if (stat(path, &identity) == 0
            && identity.st_dev == namespace_device
            && identity.st_ino == namespace_inode) {
            ++matched;
            if (terminate && kill(static_cast<pid_t>(value), SIGKILL) == 0
                && signaled != nullptr) {
                ++*signaled;
            }
        }
    }
    closedir(proc);
    return matched;
}

NamespaceTerminationResult TerminateNamespaceMembers(dev_t namespace_device,
                                                       ino_t namespace_inode) {
    NamespaceTerminationResult result;
    result.matched = ScanNamespaceMembers(
        namespace_device, namespace_inode, true, &result.signaled);
    for (int attempt = 0; attempt < 20; ++attempt) {
        result.remaining = ScanNamespaceMembers(
            namespace_device, namespace_inode, false, nullptr);
        if (result.remaining == 0) break;
        usleep(50000);
        ScanNamespaceMembers(namespace_device, namespace_inode, true,
                             &result.signaled);
    }
    return result;
}

bool IsDisposableAppNamespace(pid_t target_pid, uid_t target_uid,
                              dev_t namespace_device,
                              ino_t namespace_inode) {
    DIR* proc = opendir("/proc");
    if (proc == nullptr) return false;
    bool target_found = false;
    bool valid = true;
    while (valid) {
        dirent* entry = readdir(proc);
        if (entry == nullptr) break;
        char* end = nullptr;
        const long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || value <= 0
            || value == getpid()) {
            continue;
        }
        char path[64]{};
        snprintf(path, sizeof(path), "/proc/%ld/ns/mnt", value);
        struct stat identity {};
        if (stat(path, &identity) != 0
            || identity.st_dev != namespace_device
            || identity.st_ino != namespace_inode) {
            continue;
        }
        const pid_t member = static_cast<pid_t>(value);
        if (member != target_pid || !ReadProcessUid(member, target_uid)) {
            valid = false;
            break;
        }
        target_found = true;
    }
    closedir(proc);
    return valid && target_found;
}

pathguard::MountRollbackResult UnmountTransactionMount(
    const pathguard::AppliedMount& applied) {
#if PATHGUARD_TEST_ROLLBACK_FAILURE
    pathguard::MountRollbackResult result;
    result.failure.backend = applied.backend;
    result.failure.operation_id = applied.operation_id;
    result.failure.stage = pathguard::MountOperationStage::kRollbackUnmount;
    result.failure.error = EIO;
    result.failure.expected_mount_id = applied.mount_id;
    result.failure.actual_mount_id = applied.mount_id;
    result.failure.mutation_happened = 1;
    result.failure.identity_confirmed = 1;
    return result;
#else
    return pathguard::UnmountValidatedDirectoryMount(applied);
#endif
}

MountPerfResult ApplyProcessPlan(pid_t pid, uid_t uid, int module_dir_fd,
                                 const ProcessPlan& plan,
                                 SharedMountState* state) {
    MountPerfResult perf;
    perf.rule_count = plan.count;
    perf.topology_generation = plan.topology.generation;
    if (IsCancelRequested(state)) {
        perf.result = ECANCELED;
        return perf;
    }
    uint64_t expected_start_time = 0;
    if (!ReadProcessUid(pid, uid)
        || !ReadProcessStartTime(pid, &expected_start_time)) {
        perf.result = ESRCH;
        return perf;
    }
    const uint64_t setns_started = pathguard::perf::NowNs();
    char namespace_path[64];
    snprintf(namespace_path, sizeof(namespace_path), "/proc/%d/ns/mnt", pid);
    const int namespace_fd = open(namespace_path, O_RDONLY | O_CLOEXEC);
    if (namespace_fd < 0) {
        const int error = errno;
        LOGE("mount namespace open failed: pid=%d errno=%d", pid, error);
        perf.result = error;
        perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);
        return perf;
    }
    if (setns(namespace_fd, CLONE_NEWNS) != 0) {
        const int error = errno;
        LOGE("mount namespace enter failed: pid=%d errno=%d", pid, error);
        close(namespace_fd);
        perf.result = error;
        perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);
        return perf;
    }
    struct stat namespace_identity {};
    if (fstat(namespace_fd, &namespace_identity) != 0) {
        const int error = errno;
        close(namespace_fd);
        perf.result = error;
        perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);
        return perf;
    }
    close(namespace_fd);
    perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);

    char source_path[PATH_MAX]{};
    char target_path[PATH_MAX]{};
    char deny_anchor_path[PATH_MAX]{};
    const pathguard::MountActionMask required_actions =
        RequiredMountActions(plan);
    if (required_actions == 0
        || ((required_actions & pathguard::kMountActionDenyAnchor) != 0
            && !BuildDenyAnchorPath(module_dir_fd, deny_anchor_path,
                                    sizeof(deny_anchor_path)))) {
        perf.result = ENOTSUP;
        perf.runtime_reason = static_cast<uint32_t>(
            pathguard::RuntimeReason::kUnsupportedAction);
        return perf;
    }
    const char* first_visible = PlanPath(plan, plan.mounts[0].visible_path);
    if (!BuildPathUnderRoot(plan.topology.visible_root, first_visible,
                            target_path, sizeof(target_path))
        || !BuildMountSourcePath(plan, plan.mounts[0], deny_anchor_path,
                                source_path, sizeof(source_path))) {
        perf.result = EINVAL;
        return perf;
    }
    const uint64_t probe_started = pathguard::perf::NowNs();
    bool probe_cache_hit = false;
    const pathguard::MountBackendProbe probe = GetOrProbeMountBackends(
        source_path, target_path, plan.policy_flags,
        plan.topology.generation, &probe_cache_hit);
    LOGI("perf probe_total pid=%d probe_us=%llu cached=%d", pid,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             pathguard::perf::ElapsedNs(probe_started))),
         probe_cache_hit ? 1 : 0);
    LOGI("probe_runtime uid=%u euid=%u context=%s cap_eff=%llx cap_bnd=%llx "
         "ns_before=%llu ns_after=%llu unshare_errno=%d private_errno=%d "
         "probe_errno=%d primitives=%llx source=%s target=%s",
         probe.uid, probe.euid,
         probe.selinux_context[0] == '\0' ? "<empty>" : probe.selinux_context,
         static_cast<unsigned long long>(probe.cap_effective),
         static_cast<unsigned long long>(probe.cap_bounding),
         static_cast<unsigned long long>(probe.namespace_before),
         static_cast<unsigned long long>(probe.namespace_after),
         probe.unshare_error, probe.private_error, probe.error,
         static_cast<unsigned long long>(probe.capabilities.primitives),
         source_path, target_path);
    LogProbeStep("open_tree", probe.open_tree);
    LogProbeStep("proc_fd", probe.proc_fd);
    LogProbeStep("legacy_string", probe.legacy_string);
    pathguard::MountBackendCapabilities capabilities = probe.capabilities;
    const bool allow_legacy = (plan.policy_flags
        & pathguard::binary_format::kPolicyFlagAllowLegacyStringBind) != 0;
    const auto selection = pathguard::SelectMountBackend(
        required_actions, capabilities, allow_legacy);
    perf.backend = static_cast<uint32_t>(selection.backend);
    perf.backend_reason = static_cast<uint32_t>(selection.reason);
    if (selection.backend == pathguard::MountBackendKind::kUnsupported) {
        perf.result = probe.error != 0 ? probe.error : ENOTSUP;
        return perf;
    }
    if (selection.backend == pathguard::MountBackendKind::kLegacyString
        && !IsDisposableAppNamespace(pid, uid, namespace_identity.st_dev,
                                     namespace_identity.st_ino)) {
        perf.backend = static_cast<uint32_t>(
            pathguard::MountBackendKind::kUnsupported);
        perf.backend_reason = static_cast<uint32_t>(
            pathguard::MountBackendReason::kCapabilityMissing);
        perf.result = ENOTSUP;
        return perf;
    }
    MountTransactionWorkspace* workspace = CreateMountTransactionWorkspace();
    if (workspace == nullptr) {
        perf.result = errno != 0 ? errno : ENOMEM;
        return perf;
    }
    pathguard::MountInfoSnapshot current_mounts;
    const int snapshot_error = pathguard::CaptureMountInfoSnapshot(
        &current_mounts);
    if (snapshot_error != 0
        || !pathguard::MountInfoSnapshotMatchesCurrentNamespace(
            current_mounts)) {
        pathguard::DestroyMountInfoSnapshot(&current_mounts);
        DestroyMountTransactionWorkspace(workspace);
        perf.result = snapshot_error != 0 ? snapshot_error : ESTALE;
        return perf;
    }
    perf.mountinfo_snapshot_count = 1;
    perf.mountinfo_read_ns = current_mounts.read_ns;
    perf.mountinfo_parse_ns = current_mounts.parse_ns;
    size_t pinned_source_count = 0;
    const auto finish_preflight = [&](int error) {
        for (size_t index = 0; index < pinned_source_count; ++index) {
            pathguard::ClosePinnedIdentity(&workspace->sources[index]);
        }
        pathguard::DestroyMountInfoSnapshot(&current_mounts);
        DestroyMountTransactionWorkspace(workspace);
        perf.result = error;
        return perf;
    };
    const uint64_t src_pin_started = pathguard::perf::NowNs();
    for (size_t rule_index = 0; rule_index < plan.count; ++rule_index) {
        if (!BuildMountSourcePath(
                plan, plan.mounts[rule_index], deny_anchor_path,
                source_path, sizeof(source_path))) {
            return finish_preflight(EINVAL);
        }
        const int pin_error = pathguard::PinDirectory(
            current_mounts, source_path, &workspace->sources[rule_index]);
        if (pin_error != 0) {
            return finish_preflight(pin_error);
        }
        ++pinned_source_count;
    }
    perf.source_pin_ns = pathguard::perf::ElapsedNs(src_pin_started);
    LOGI("perf_stage source_pin_loop_us=%llu count=%u",
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             perf.source_pin_ns)),
         plan.count);
    uint64_t actual_start_time = 0;
    if (!ReadProcessUid(pid, uid)
        || !ReadProcessStartTime(pid, &actual_start_time)
        || actual_start_time != expected_start_time) {
        return finish_preflight(ESRCH);
    }
    ProcessPlan current_plan;
    const uint64_t current_policy_started = pathguard::perf::NowNs();
    const bool current_policy_valid = LoadProcessPlan(
        module_dir_fd, plan.process_name, static_cast<jint>(uid),
        &current_plan, nullptr) && SameProcessPlan(plan, current_plan);
    perf.policy_revalidate_ns = pathguard::perf::ElapsedNs(
        current_policy_started);
    if (!current_policy_valid) {
        perf.result = ESTALE;
        perf.runtime_reason = static_cast<uint32_t>(
            pathguard::RuntimeReason::kPolicyChanged);
        return finish_preflight(ESTALE);
    }

#if PATHGUARD_TEST_PRE_LEASE_DELAY_MS > 0
    LOGI("transaction test pre-lease delay: pid=%d delay_ms=%d",
         pid, PATHGUARD_TEST_PRE_LEASE_DELAY_MS);
    usleep(static_cast<useconds_t>(PATHGUARD_TEST_PRE_LEASE_DELAY_MS) * 1000u);
#endif
    const uint64_t propagation_check_started = pathguard::perf::NowNs();
    const bool propagation_requires_private = StoragePropagationRequiresPrivate(
        current_mounts);
    perf.propagation_check_ns = pathguard::perf::ElapsedNs(
        propagation_check_started);
    ProcessPlan lease_plan;
    RuntimeStorageTopology lease_topology;
    uint64_t lease_start_time = 0;
    const bool lease_process_valid = ReadProcessUid(pid, uid)
        && ReadProcessStartTime(pid, &lease_start_time)
        && lease_start_time == expected_start_time;
    bool lease_policy_valid = false;
    if (lease_process_valid) {
        const uint64_t lease_policy_started = pathguard::perf::NowNs();
        lease_policy_valid = LoadProcessPlan(
            module_dir_fd, plan.process_name, static_cast<jint>(uid),
            &lease_plan, nullptr) && SameProcessPlan(plan, lease_plan);
        perf.policy_revalidate_ns += pathguard::perf::ElapsedNs(
            lease_policy_started);
    }
    bool lease_topology_valid = false;
    if (lease_policy_valid) {
        const uint64_t topology_started = pathguard::perf::NowNs();
        lease_topology_valid = CaptureStorageTopologyFromSnapshot(
            uid, plan.topology.source_root, current_mounts, &lease_topology)
            && SameStorageTopology(plan.topology, lease_topology);
        perf.topology_ns = pathguard::perf::ElapsedNs(topology_started);
    }
    if (!lease_process_valid || !lease_policy_valid || !lease_topology_valid) {
        perf.result = ESTALE;
        perf.runtime_reason = static_cast<uint32_t>(
            !lease_process_valid ? pathguard::RuntimeReason::kPreflightFailed
            : !lease_policy_valid ? pathguard::RuntimeReason::kPolicyChanged
            : pathguard::RuntimeReason::kTopologyChanged);
        return finish_preflight(ESTALE);
    }
    if (IsCancelRequested(state)) {
        return finish_preflight(ECANCELED);
    }
    if (!AcquireMutationLease(state)) {
        return finish_preflight(ECANCELED);
    }

    const uint64_t propagation_started = pathguard::perf::NowNs();
    bool propagation_changed = false;
    int error = 0;
    if (propagation_requires_private) {
        if (mount(nullptr, "/storage", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
            error = errno;
        } else {
            propagation_changed = true;
        }
    }
    perf.propagation_ns = pathguard::perf::ElapsedNs(propagation_started);

    const uint64_t mounts_started = pathguard::perf::NowNs();
    bool untracked_mutation = false;
    for (size_t rule_index = 0; rule_index < plan.count && error == 0; ++rule_index) {
        if (IsCancelRequested(state)) {
            error = ECANCELED;
            break;
        }
        const uint64_t mount_started = pathguard::perf::NowNs();
        const char* visible_path = PlanPath(
            plan, plan.mounts[rule_index].visible_path);
        const char* backing_path = PlanPath(
            plan, plan.mounts[rule_index].backing_path);
        if (!IsExecutableMountAction(plan.mounts[rule_index].action)
            || visible_path == nullptr || backing_path == nullptr
            || !BuildPathUnderRoot(plan.topology.visible_root, visible_path,
                                   target_path, sizeof(target_path))
            || !BuildMountSourcePath(
                plan, plan.mounts[rule_index], deny_anchor_path,
                source_path, sizeof(source_path))) {
            error = EINVAL;
            break;
        }
        if (error == 0) {
            pathguard::PinnedIdentity target;
            const uint64_t target_pin_started = pathguard::perf::NowNs();
            error = pathguard::PinDirectory(
                current_mounts, target_path, &target);
            const uint64_t target_pin_ns = pathguard::perf::ElapsedNs(target_pin_started);
            if (error == 0) {
                pathguard::CanonicalLocator source_locator;
                pathguard::CanonicalLocator target_locator;
                snprintf(source_locator.path, sizeof(source_locator.path), "%s", source_path);
                snprintf(target_locator.path, sizeof(target_locator.path), "%s", target_path);
                pathguard::MountApplyTiming apply_timing;
                pathguard::MountApplyResult apply_result =
                    pathguard::ApplyDirectoryMountRaw(
                        selection.backend, static_cast<uint32_t>(rule_index),
                        workspace->sources[rule_index], target, source_locator,
                        target_locator, &apply_timing);
                // Journal the mount the moment the syscall mutated the ns, even
                // while its mount ID is not known yet. Verification fills the ID
                // from the post-mutation snapshot and updates this same entry.
                if (apply_result.mutation_happened()) {
                    if (!workspace->journal.Push(apply_result.mount)) {
                        error = EOVERFLOW;
                        untracked_mutation = true;
                    }
                }
#if PATHGUARD_TEST_CRASH_AFTER_MOUNT
                if (workspace->journal.size() == 1) _exit(86);
#endif
                if (error == 0 && apply_result.ok()
                    && apply_result.mutation_happened()) {
                    pathguard::MountInfoSnapshot after_mounts;
                    const int after_error = pathguard::CaptureMountInfoSnapshot(
                        &after_mounts);
                    if (after_error == 0) {
                        ++perf.mountinfo_snapshot_count;
                        perf.mountinfo_read_ns += after_mounts.read_ns;
                        perf.mountinfo_parse_ns += after_mounts.parse_ns;
                        apply_result.failure = pathguard::VerifyDirectoryMount(
                            current_mounts, after_mounts,
                            workspace->sources[rule_index], target,
                            target_locator, &apply_result.mount, &apply_timing);
                        if (!workspace->journal.UpdateLast(apply_result.mount)) {
                            apply_result.failure.stage =
                                pathguard::MountOperationStage::kVerify;
                            apply_result.failure.error = EOVERFLOW;
                            apply_result.failure.mutation_happened = 1;
                            untracked_mutation = true;
                        }
                        if (apply_result.failure.error == 0) {
                            pathguard::DestroyMountInfoSnapshot(&current_mounts);
                            current_mounts = after_mounts;
                            after_mounts = {};
                        }
                    } else {
                        apply_result.failure.stage =
                            pathguard::MountOperationStage::kVerify;
                        apply_result.failure.error = after_error;
                        apply_result.failure.mutation_happened = 1;
                    }
                    pathguard::DestroyMountInfoSnapshot(&after_mounts);
                }
                if (error == 0) error = apply_result.failure.error;
                perf.failure_stage = static_cast<uint32_t>(
                    apply_result.failure.stage);
                LOGI("perf mount_step rule=%zu backend=%u target_pin_us=%llu "
                     "verify_pinned_us=%llu before_scan_us=%llu apply_raw_us=%llu "
                     "verify_us=%llu verify_stat_us=%llu verify_mi_read_us=%llu "
                     "verify_mi_parse_us=%llu mutation=%d stage=%u errno=%d mount_id=%llu",
                     rule_index, static_cast<uint32_t>(selection.backend),
                     static_cast<unsigned long long>(target_pin_ns / 1000u),
                     static_cast<unsigned long long>(apply_timing.verify_pinned_ns / 1000u),
                     static_cast<unsigned long long>(apply_timing.before_scan_ns / 1000u),
                     static_cast<unsigned long long>(apply_timing.apply_raw_ns / 1000u),
                     static_cast<unsigned long long>(apply_timing.verify_ns / 1000u),
                     static_cast<unsigned long long>(apply_timing.verify_stat_ns / 1000u),
                     static_cast<unsigned long long>(apply_timing.verify_mountinfo_read_ns / 1000u),
                     static_cast<unsigned long long>(apply_timing.verify_mountinfo_parse_ns / 1000u),
                     apply_result.mutation_happened() ? 1 : 0,
                     static_cast<unsigned>(apply_result.failure.stage),
                     apply_result.failure.error,
                     static_cast<unsigned long long>(apply_result.mount.mount_id));
            }
            pathguard::ClosePinnedIdentity(&target);
        }
#if PATHGUARD_TEST_MOUNT_DELAY_MS > 0
        if (workspace->journal.size() == 1) {
            usleep(static_cast<useconds_t>(PATHGUARD_TEST_MOUNT_DELAY_MS) * 1000u);
        }
#endif
        const uint64_t mount_ns = pathguard::perf::ElapsedNs(mount_started);
        if (mount_ns > perf.mount_max_ns) perf.mount_max_ns = mount_ns;
    }
    if (error == 0 && IsCancelRequested(state)) error = ECANCELED;
    perf.mount_total_ns = pathguard::perf::ElapsedNs(mounts_started);
    if (error == 0) {
        perf.result = 0;
        CompanionResult result = state->result;
        result.mount = perf;
        if (!PublishSharedResult(state, result, MountState::kApplying,
                                 MountState::kComplete)) {
            error = ECANCELED;
        }
    }
    bool rollback_failed = false;
    if (error != 0) {
        const uint64_t rollback_started = pathguard::perf::NowNs();
        const auto record_rollback_failure = [
            &rollback_failed, &perf, &error](
                const pathguard::MountRollbackResult& rollback) {
            if (!rollback.ok()) {
                rollback_failed = true;
                perf.rollback_failed = 1;
                error = rollback.failure.error;
                LOGE("mount rollback failed: op=%u stage=%u errno=%d expected=%llu actual=%llu identity=%u",
                     rollback.failure.operation_id,
                     static_cast<unsigned>(rollback.failure.stage),
                     rollback.failure.error,
                     static_cast<unsigned long long>(rollback.failure.expected_mount_id),
                     static_cast<unsigned long long>(rollback.failure.actual_mount_id),
                     rollback.failure.identity_confirmed);
            }
        };
        pathguard::MountInfoSnapshot rollback_before;
        pathguard::MountInfoSnapshot rollback_after;
        const int rollback_snapshot_error =
            workspace->journal.empty() ? 0
            : pathguard::CaptureMountInfoSnapshot(&rollback_before);
        if (rollback_snapshot_error == 0
            && rollback_before.mapping != nullptr) {
            ++perf.mountinfo_snapshot_count;
            perf.mountinfo_read_ns += rollback_before.read_ns;
            perf.mountinfo_parse_ns += rollback_before.parse_ns;
        }
        if (rollback_snapshot_error != 0) {
            pathguard::MountRollbackResult rollback;
            rollback.failure.stage =
                pathguard::MountOperationStage::kRollbackIdentity;
            rollback.failure.error = rollback_snapshot_error;
            rollback.failure.mutation_happened = 1;
            record_rollback_failure(rollback);
        }
        for (size_t index = workspace->journal.size();
             !rollback_failed && index > 0; --index) {
            const pathguard::AppliedMount* applied =
                workspace->journal.At(index - 1);
            if (applied == nullptr) {
                pathguard::MountRollbackResult rollback;
                rollback.failure.stage =
                    pathguard::MountOperationStage::kRollbackIdentity;
                rollback.failure.error = EOVERFLOW;
                rollback.failure.mutation_happened = 1;
                record_rollback_failure(rollback);
                break;
            }
            record_rollback_failure(
                pathguard::ValidateRollbackDirectoryMount(
                    *applied, rollback_before));
        }
        for (size_t index = workspace->journal.size();
             !rollback_failed && index > 0; --index) {
            const pathguard::AppliedMount* applied =
                workspace->journal.At(index - 1);
            record_rollback_failure(
                UnmountTransactionMount(*applied));
        }
        if (!rollback_failed && !workspace->journal.empty()) {
            const int after_error = pathguard::CaptureMountInfoSnapshot(
                &rollback_after);
            if (after_error != 0) {
                pathguard::MountRollbackResult rollback;
                rollback.failure.stage =
                    pathguard::MountOperationStage::kRollbackVerify;
                rollback.failure.error = after_error;
                rollback.failure.mutation_happened = 1;
                rollback.failure.identity_confirmed = 1;
                record_rollback_failure(rollback);
            } else {
                ++perf.mountinfo_snapshot_count;
                perf.mountinfo_read_ns += rollback_after.read_ns;
                perf.mountinfo_parse_ns += rollback_after.parse_ns;
            }
        }
        for (size_t index = workspace->journal.size();
             !rollback_failed && index > 0; --index) {
            const pathguard::AppliedMount* applied =
                workspace->journal.At(index - 1);
            record_rollback_failure(
                pathguard::VerifyRollbackDirectoryMount(
                    *applied, rollback_after));
        }
        if (!rollback_failed) workspace->journal.Clear();
        pathguard::DestroyMountInfoSnapshot(&rollback_after);
        pathguard::DestroyMountInfoSnapshot(&rollback_before);
        perf.rollback_ns = pathguard::perf::ElapsedNs(rollback_started);
    }
    for (size_t index = 0; index < pinned_source_count; ++index) {
        pathguard::ClosePinnedIdentity(&workspace->sources[index]);
    }
    pathguard::DestroyMountInfoSnapshot(&current_mounts);
    DestroyMountTransactionWorkspace(workspace);
    perf.result = error;
    if (error != 0) {
        CompanionResult result = state->result;
        result.mount = perf;
        const MountState current = LoadSharedStatus(state);
        if (propagation_changed || rollback_failed || untracked_mutation) {
            PublishSharedResult(state, result, current,
                                MountState::kNamespaceTainted);
            const NamespaceTerminationResult termination = TerminateNamespaceMembers(
                namespace_identity.st_dev, namespace_identity.st_ino);
            LOGE("namespace tainted: pid=%d matched=%zu signaled=%zu remaining=%zu",
                 pid, termination.matched, termination.signaled,
                 termination.remaining);
        } else if (current == MountState::kCancelRequested) {
            PublishSharedResult(state, result, MountState::kCancelRequested,
                                MountState::kRollbackComplete);
        } else if (current == MountState::kApplying) {
            PublishSharedResult(state, result, MountState::kApplying,
                                MountState::kRollbackComplete);
        }
    }
    return perf;
}

void LogZygiskPrePerf(pid_t pid, jint uid, uint32_t rule_count,
                      const PolicyLoadPerf& policy, uint64_t hook_ns,
                      uint64_t connect_ns, uint64_t send_ns,
                      uint64_t total_ns, bool hook_required,
                      bool hook_deferred, bool request_sent) {
    LOGI("perf zygisk_pre pid=%d uid=%d rules=%u hook_required=%d hook_deferred=%d request_sent=%d "
         "policy_open_us=%llu policy_lookup_us=%llu policy_unmap_us=%llu "
         "hook_us=%llu companion_connect_us=%llu companion_send_us=%llu total_us=%llu",
          pid, uid, rule_count, hook_required ? 1 : 0, hook_deferred ? 1 : 0,
         request_sent ? 1 : 0,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(policy.open_map_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(policy.lookup_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(policy.unmap_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(hook_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(connect_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(send_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(total_ns)));
}

SharedMountState* CreateSharedState(int* shared_fd) {
    if (shared_fd == nullptr) return nullptr;
    *shared_fd = static_cast<int>(syscall(
        SYS_memfd_create, "pathguard-result", MFD_CLOEXEC));
    if (*shared_fd < 0 || ftruncate(*shared_fd, sizeof(SharedMountState)) != 0) {
        if (*shared_fd >= 0) close(*shared_fd);
        *shared_fd = -1;
        return nullptr;
    }
    void* mapping = mmap(nullptr, sizeof(SharedMountState), PROT_READ | PROT_WRITE,
                         MAP_SHARED, *shared_fd, 0);
    if (mapping == MAP_FAILED) {
        close(*shared_fd);
        *shared_fd = -1;
        return nullptr;
    }
    auto* state = static_cast<SharedMountState*>(mapping);
    memset(state, 0, sizeof(*state));
    state->magic = kSharedStateMagic;
    state->version = kSharedStateVersion;
    state->result.magic = kCompanionResultMagic;
    state->result.version = kCompanionResultVersion;
    return state;
}

SharedMountState* MapSharedState(int shared_fd) {
    struct stat file_stat {};
    if (shared_fd < 0 || fstat(shared_fd, &file_stat) != 0
        || file_stat.st_size != static_cast<off_t>(sizeof(SharedMountState))) {
        return nullptr;
    }
    void* mapping = mmap(nullptr, sizeof(SharedMountState), PROT_READ | PROT_WRITE,
                         MAP_SHARED, shared_fd, 0);
    if (mapping == MAP_FAILED) return nullptr;
    auto* state = static_cast<SharedMountState*>(mapping);
    if (state->magic != kSharedStateMagic || state->version != kSharedStateVersion) {
        munmap(state, sizeof(*state));
        return nullptr;
    }
    return state;
}

bool WaitForSharedResult(SharedMountState* state, int timeout_ms) {
    uint64_t deadline = pathguard::perf::NowNs()
        + static_cast<uint64_t>(timeout_ms) * 1000000u;
    bool preflight_grace_used = false;
    bool applying_grace_used = false;
    while (true) {
        const MountState current = LoadSharedStatus(state);
        if (pathguard::MountTransactionHasResult(current)) return true;
        if (current == MountState::kCancelled) return false;
        const uint64_t now = pathguard::perf::NowNs();
        if (now >= deadline) {
            if (pathguard::ShouldExtendPendingMountWait(
                    current, LoadSharedPreflightPhase(state),
                    preflight_grace_used)) {
                preflight_grace_used = true;
                deadline = now
                    + static_cast<uint64_t>(kCapabilityProbeGraceMs) * 1000000u;
                LOGI("mount wait extended for capability probe: pid=%d grace_ms=%d",
                     getpid(), kCapabilityProbeGraceMs);
                continue;
            }
            if (current == MountState::kPending) {
                if (TransitionSharedState(state, MountState::kPending,
                                          MountState::kCancelRequested)) {
                    return false;
                }
                continue;
            }
            if (current == MountState::kApplying && !applying_grace_used) {
                applying_grace_used = true;
                deadline = now
                    + static_cast<uint64_t>(kApplyingCompletionGraceMs) * 1000000u;
                continue;
            }
            if (current == MountState::kApplying
                && !TransitionSharedState(state, MountState::kApplying,
                                          MountState::kCancelRequested)) {
                continue;
            }
            if (pathguard::IsMountTransactionTerminal(current)) {
                return pathguard::MountTransactionHasResult(current);
            }
            const uint64_t owner_deadline = pathguard::perf::NowNs()
                + static_cast<uint64_t>(kApplyingOwnerDeathTimeoutMs) * 1000000u;
            while (true) {
                const MountState applying_state = LoadSharedStatus(state);
                if (pathguard::MountTransactionHasResult(applying_state)) return true;
                if (applying_state == MountState::kCancelled) return false;
                const uint64_t applying_now = pathguard::perf::NowNs();
                if (applying_now >= owner_deadline) {
                    LOGE("mount transaction owner unavailable; terminating target pid=%d state=%u",
                         getpid(), StateValue(applying_state));
                    kill(getpid(), SIGKILL);
                    return false;
                }
                const uint64_t owner_remaining = owner_deadline - applying_now;
                const timespec owner_timeout{
                    static_cast<time_t>(owner_remaining / 1000000000u),
                    static_cast<long>(owner_remaining % 1000000000u),
                };
                syscall(SYS_futex, &state->status, FUTEX_WAIT,
                        StateValue(applying_state), &owner_timeout, nullptr, 0);
            }
        }
        const uint64_t remaining = deadline - now;
        const timespec timeout{
            static_cast<time_t>(remaining / 1000000000u),
            static_cast<long>(remaining % 1000000000u),
        };
        syscall(SYS_futex, &state->status, FUTEX_WAIT, StateValue(current),
                &timeout, nullptr, 0);
    }
}

bool SendPlanWithSharedFd(int fd, jint uid, const ProcessPlan& plan,
                          int shared_fd, int module_dir_fd) {
    if (module_dir_fd < 0 || plan.process_name[0] == '\0') return false;
    BootstrapHeader header{};
    header.magic = kBootstrapMagic;
    header.version = kBootstrapVersion;
    header.pid = getpid();
    header.uid = uid;
    header.rule_count = plan.count;
    header.policy_flags = plan.policy_flags;
    header.snapshot_generation = plan.snapshot_generation;
    header.plan_generation = plan.plan_generation;
    header.process_name_length = static_cast<uint32_t>(strlen(plan.process_name));
    header.compat_flags = (plan.media_compat ? 1u : 0u)
        | (plan.provider_compat ? 2u : 0u);
    int descriptors[2] = {shared_fd, module_dir_fd};
    char control[CMSG_SPACE(sizeof(descriptors))]{};
    iovec header_iov{&header, sizeof(header)};
    msghdr message{};
    message.msg_iov = &header_iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr* descriptor = CMSG_FIRSTHDR(&message);
    descriptor->cmsg_level = SOL_SOCKET;
    descriptor->cmsg_type = SCM_RIGHTS;
    descriptor->cmsg_len = CMSG_LEN(sizeof(descriptors));
    memcpy(CMSG_DATA(descriptor), descriptors, sizeof(descriptors));
    const ssize_t sent = sendmsg(fd, &message, MSG_NOSIGNAL);
    if (sent != static_cast<ssize_t>(sizeof(header))) return false;
    if (!WriteFully(fd, plan.process_name, header.process_name_length)) return false;
    for (uint32_t index = 0; index < plan.count; ++index) {
        const PlannedMount& mount = plan.mounts[index];
        const char* visible = PlanPath(plan, mount.visible_path);
        const char* backing = PlanPath(plan, mount.backing_path);
        if (visible == nullptr || backing == nullptr) return false;
        const uint32_t visible_length = static_cast<uint32_t>(strlen(visible));
        const uint32_t backing_length = static_cast<uint32_t>(strlen(backing));
        if (!WriteFully(fd, &mount.action, sizeof(mount.action))
            || !WriteFully(fd, &visible_length, sizeof(visible_length))
            || !WriteFully(fd, visible, visible_length)
            || !WriteFully(fd, &backing_length, sizeof(backing_length))
            || !WriteFully(fd, backing, backing_length)) {
            return false;
        }
    }
    return true;
}

bool ReceiveBootstrap(int fd, BootstrapHeader* header, int* shared_fd,
                      int* module_dir_fd) {
    if (header == nullptr || shared_fd == nullptr || module_dir_fd == nullptr) {
        return false;
    }
    *shared_fd = -1;
    *module_dir_fd = -1;
    int descriptors[2] = {-1, -1};
    char control[CMSG_SPACE(sizeof(descriptors))]{};
    iovec header_iov{header, sizeof(*header)};
    msghdr message{};
    message.msg_iov = &header_iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    const ssize_t received = recvmsg(fd, &message, MSG_WAITALL);
    if (received != static_cast<ssize_t>(sizeof(*header))) return false;
    for (cmsghdr* descriptor = CMSG_FIRSTHDR(&message); descriptor != nullptr;
         descriptor = CMSG_NXTHDR(&message, descriptor)) {
        if (descriptor->cmsg_level == SOL_SOCKET
            && descriptor->cmsg_type == SCM_RIGHTS
            && descriptor->cmsg_len >= CMSG_LEN(sizeof(descriptors))) {
            memcpy(descriptors, CMSG_DATA(descriptor), sizeof(descriptors));
            *shared_fd = descriptors[0];
            *module_dir_fd = descriptors[1];
            break;
        }
    }
    return *shared_fd >= 0 && *module_dir_fd >= 0;
}

class PathGuardModule final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
        LOGI("module_onload pid=%d", getpid());
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        const uint64_t pre_started = pathguard::perf::NowNs();
        const char* process_name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (process_name == nullptr) {
            Unload();
            return;
        }

        ProcessPlan plan;
        PolicyLoadPerf policy_perf;
        const int module_dir = api_->getModuleDir();
        const bool external_storage_provider =
            strcmp(process_name, kExternalStorageProviderProcess) == 0;
        const bool media_provider =
            strcmp(process_name, kMainlineMediaProviderProcess) == 0;
        if (external_storage_provider || media_provider) {
            pathguard::provider_redirect::Rule provider_rules[kMaxMountRules]{};
            uint32_t provider_rule_count = 0;
            const bool loaded = module_dir >= 0
                && LoadProviderRules(module_dir, provider_rules, kMaxMountRules,
                                     &provider_rule_count);
            if (module_dir >= 0) close(module_dir);
            env_->ReleaseStringUTFChars(args->nice_name, process_name);
            provider_redirect_required_ = loaded;
            if (!loaded) {
                LOGI("provider redirect has no active rules");
                Unload();
                return;
            }
            const pathguard::provider_redirect::InstallResult install_result =
                pathguard::provider_redirect::Install(
                api_, env_, provider_rules, provider_rule_count,
                media_provider
                    ? pathguard::provider_redirect::CallerMode::kSystemMedia
                    : pathguard::provider_redirect::CallerMode::kBinderUid);
            provider_redirect_installed_ = install_result.virtualization_active;
            provider_redirect_hooks_committed_ = install_result.hooks_committed;
            LOGI("provider redirect specialize: process=%s rules=%u mode=%u committed=%d installed=%d",
                 media_provider ? "media" : "external_storage", provider_rule_count,
                 media_provider ? 1u : 0u,
                 provider_redirect_hooks_committed_ ? 1 : 0,
                 provider_redirect_installed_ ? 1 : 0);
            if (!provider_redirect_installed_
                && !provider_redirect_hooks_committed_) Unload();
            return;
        }
        const bool diagnostic = strcmp(process_name, kDiagnosticPackage) == 0;
        if (diagnostic) LOGI("pre_specialize_enter pid=%d uid=%d module_fd=%d",
                             getpid(), args->uid, module_dir);
        const bool matched = module_dir >= 0
            && LoadProcessPlan(module_dir, process_name, args->uid, &plan, &policy_perf);
        env_->ReleaseStringUTFChars(args->nice_name, process_name);
        if (!matched) {
            if (module_dir >= 0) close(module_dir);
            if (diagnostic) LOGE("pre_specialize_no_plan pid=%d uid=%d",
                                 getpid(), args->uid);
            Unload();
            return;
        }

        media_query_hook_required_ = plan.provider_compat;
        if (media_query_hook_required_) {
            bool has_deny = false;
            for (uint32_t index = 0; index < plan.count; ++index) {
                if (plan.mounts[index].action == kDenyAction) {
                    has_deny = true;
                    break;
                }
            }
            media_query_hook_required_ = has_deny;
            if (has_deny) {
                media_plan_ = plan;
                media_uid_ = args->uid;
            }
        }

        const uint64_t hook_ns = 0;

        const uint64_t connect_started = pathguard::perf::NowNs();
        const int companion_fd = api_->connectCompanion();
        const uint64_t connect_ns = pathguard::perf::ElapsedNs(connect_started);
        if (companion_fd < 0) {
            close(module_dir);
            LOGE("cannot connect companion socket");
            LogZygiskPrePerf(getpid(), args->uid, plan.count, policy_perf,
                             hook_ns, connect_ns, 0, pathguard::perf::ElapsedNs(pre_started),
                             plan.media_compat, false, false);
            Unload();
            return;
        }
        SetSocketTimeout(companion_fd, kCompanionIoTimeoutMs);
        int shared_fd = -1;
        SharedMountState* shared_state = CreateSharedState(&shared_fd);
        if (shared_state == nullptr) {
            LOGE("cannot create shared result state");
            close(companion_fd);
            close(module_dir);
            Unload();
            return;
        }
        const uint64_t send_started = pathguard::perf::NowNs();
        if (!SendPlanWithSharedFd(
                companion_fd, args->uid, plan, shared_fd, module_dir)) {
            LOGE("cannot send process plan to companion");
            LogZygiskPrePerf(getpid(), args->uid, plan.count, policy_perf,
                             hook_ns, connect_ns, pathguard::perf::ElapsedNs(send_started),
                             pathguard::perf::ElapsedNs(pre_started),
                             plan.media_compat, false, false);
            munmap(shared_state, sizeof(*shared_state));
            close(shared_fd);
            Unload();
        } else {
            mount_request_sent_ = true;
            mount_shared_state_ = shared_state;
            close(shared_fd);
            LogZygiskPrePerf(getpid(), args->uid, plan.count, policy_perf,
                             hook_ns, connect_ns, pathguard::perf::ElapsedNs(send_started),
                             pathguard::perf::ElapsedNs(pre_started),
                             plan.media_compat, plan.media_compat, true);
        }
        close(module_dir);
        close(companion_fd);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if (provider_redirect_required_) {
            if (!provider_redirect_installed_
                && !provider_redirect_hooks_committed_) Unload();
            return;
        }
        const uint64_t post_started = pathguard::perf::NowNs();
        if (mount_request_sent_ && mount_shared_state_ != nullptr) {
            CompanionResult result;
            const uint64_t result_started = pathguard::perf::NowNs();
            const bool received = WaitForSharedResult(mount_shared_state_, kAppResultTimeoutMs);
            const uint64_t result_ns = pathguard::perf::ElapsedNs(result_started);
            if (received) result = mount_shared_state_->result;
            const MountState final_state = LoadSharedStatus(mount_shared_state_);
            munmap(mount_shared_state_, sizeof(*mount_shared_state_));
            mount_shared_state_ = nullptr;
            const bool valid = received
                && result.magic == kCompanionResultMagic
                && result.version == kCompanionResultVersion;
            LOGI("perf zygisk_post pid=%d request_sent=1 result_received=%d "
                 "ready_us=%llu result_wait_us=%llu mount_us=%llu total_us=%llu result=%d",
                 getpid(), valid ? 1 : 0,
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(
                     valid ? result.ready_ns : 0)),
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(result_ns)),
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(
                     valid ? result.mount.mount_total_ns : 0)),
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(
                     pathguard::perf::ElapsedNs(post_started))),
                  valid ? result.mount.result : ETIMEDOUT);
            if (valid && result.mount.result == 0
                && final_state == MountState::kComplete) {
                LOGI("redirect mount active: pid=%d backend=%u", getpid(),
                     result.mount.backend);
                if (media_query_hook_required_) {
                    char primary_paths[kMaxMountRules][PATH_MAX]{};
                    const char* deny_paths[kMaxMountRules]{};
                    char storage_root[64]{};
                    size_t deny_path_count = 0;
                    const int storage_root_length = snprintf(
                        storage_root, sizeof(storage_root), "/storage/emulated/%u",
                        static_cast<unsigned>(media_uid_) / 100000u);
                    if (storage_root_length <= 0
                        || static_cast<size_t>(storage_root_length)
                            >= sizeof(storage_root)) {
                        media_query_hook_required_ = false;
                    }
                    for (uint32_t index = 0;
                         media_query_hook_required_ && index < media_plan_.count;
                         ++index) {
                        if (media_plan_.mounts[index].action != kDenyAction) continue;
                        const char* visible = PlanPath(
                            media_plan_, media_plan_.mounts[index].visible_path);
                        if (visible == nullptr || !BuildPathUnderRoot(
                                storage_root, visible,
                                primary_paths[deny_path_count],
                                sizeof(primary_paths[deny_path_count]))) {
                            media_query_hook_required_ = false;
                            break;
                        }
                        deny_paths[deny_path_count] = primary_paths[deny_path_count];
                        ++deny_path_count;
                    }
                    const uint64_t hook_started = pathguard::perf::NowNs();
                    if (media_query_hook_required_ && deny_path_count > 0) {
                        media_query_hook_installed_ = pathguard::media_query::Install(
                            api_, env_, deny_paths, deny_path_count, media_uid_);
                    }
                    LOGI("perf media_hook_install installed=%d deny=%zu elapsed_us=%llu",
                         media_query_hook_installed_ ? 1 : 0, deny_path_count,
                         static_cast<unsigned long long>(pathguard::perf::NsToUs(
                             pathguard::perf::ElapsedNs(hook_started))));
                    if (!media_query_hook_installed_) {
                        LOGE("media query hook unavailable after successful mount");
                    }
                }
            } else if (final_state == MountState::kNamespaceTainted) {
                LOGE("redirect namespace tainted; process must terminate: pid=%d",
                     getpid());
            } else {
                LOGE("redirect mount failed or timed out; fail-open: pid=%d",
                     getpid());
            }
        }
        if (!media_query_hook_installed_) Unload();
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs*) override { Unload(); }

private:
    void Unload() { api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }

    zygisk::Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool provider_redirect_required_ = false;
    bool provider_redirect_installed_ = false;
    bool provider_redirect_hooks_committed_ = false;
    bool media_query_hook_installed_ = false;
    bool media_query_hook_required_ = false;
    bool mount_request_sent_ = false;
    SharedMountState* mount_shared_state_ = nullptr;
    ProcessPlan media_plan_;
    jint media_uid_ = 0;
};

void CompanionHandler(int client) {
    LOGI("companion_enter pid=%d", getpid());
    const uint64_t handler_started = pathguard::perf::NowNs();
    SetSocketTimeout(client, kCompanionIoTimeoutMs);
    BootstrapHeader header{};
    int shared_fd = -1;
    int module_dir_fd = -1;
    ProcessPlan plan;
    if (!ReceiveBootstrap(client, &header, &shared_fd, &module_dir_fd)
        || header.magic != kBootstrapMagic
        || header.version != kBootstrapVersion
        || header.pid <= 0
        || header.uid < 10000
        || header.rule_count == 0
        || header.rule_count > kMaxMountRules
        || header.process_name_length == 0
        || header.process_name_length >= kMaxProcessNameBytes
        || (header.compat_flags & ~3u) != 0
        || (header.policy_flags
            & ~pathguard::binary_format::kPolicyFlagAllowLegacyStringBind) != 0) {
        if (shared_fd >= 0) close(shared_fd);
        if (module_dir_fd >= 0) close(module_dir_fd);
        return;
    }
    SharedMountState* state = MapSharedState(shared_fd);
    close(shared_fd);
    if (state == nullptr) {
        close(module_dir_fd);
        return;
    }
    plan.count = header.rule_count;
    plan.policy_flags = header.policy_flags;
    plan.path_bytes = 1;
    plan.snapshot_generation = header.snapshot_generation;
    plan.plan_generation = header.plan_generation;
    plan.media_compat = (header.compat_flags & 1u) != 0;
    plan.provider_compat = (header.compat_flags & 2u) != 0;
    if (!ReadFully(client, plan.process_name, header.process_name_length)) {
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }
    plan.process_name[header.process_name_length] = '\0';
    for (uint32_t index = 0; index < plan.count; ++index) {
        PlannedMount& mount = plan.mounts[index];
        uint32_t visible_length = 0;
        uint32_t backing_length = 0;
        char visible[PATH_MAX]{};
        char backing[PATH_MAX]{};
        if (!ReadFully(client, &mount.action, sizeof(mount.action))
            || !ReadFully(client, &visible_length, sizeof(visible_length))
            || visible_length == 0 || visible_length >= PATH_MAX
            || !ReadFully(client, visible, visible_length)
            || !ReadFully(client, &backing_length, sizeof(backing_length))
            || backing_length >= PATH_MAX
            || (backing_length > 0
                && !ReadFully(client, backing, backing_length))) {
            close(module_dir_fd);
            munmap(state, sizeof(*state));
            return;
        }
        visible[visible_length] = '\0';
        backing[backing_length] = '\0';
        if (!IsExecutableMountAction(mount.action)
            || (mount.action == kDenyAction ? backing_length != 0
                                            : backing_length == 0)
            || !IsAllowedTarget(visible)
            || (mount.action == kRedirectAction && !IsAllowedTarget(backing))
            || !StorePlanPath(&plan, visible, &mount.visible_path)
            || !StorePlanPath(&plan, backing, &mount.backing_path)) {
            close(module_dir_fd);
            munmap(state, sizeof(*state));
            return;
        }
    }

    ProcessPlan current_plan;
    if (!LoadProcessPlan(module_dir_fd, plan.process_name,
                         static_cast<jint>(header.uid), &current_plan, nullptr)
        || !SameProcessPlan(plan, current_plan)) {
        CompanionResult result;
        result.mount.result = ESTALE;
        result.mount.runtime_reason = static_cast<uint32_t>(
            pathguard::RuntimeReason::kPolicyChanged);
        PublishPendingFailure(state, result);
        WriteRuntimeStatus(module_dir_fd, header.pid,
                           static_cast<uid_t>(header.uid), plan, result.mount,
                           LoadSharedStatus(state));
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }

    const uint64_t ready_started = pathguard::perf::NowNs();
    const ProcessReadyResult ready_result = WaitForProcessReady(
        header.pid, static_cast<uid_t>(header.uid), state);
    const uint64_t ready_ns = pathguard::perf::ElapsedNs(ready_started);
    if (ready_result != ProcessReadyResult::kReady) {
        CompanionResult result;
        result.ready_ns = ready_ns;
        result.mount.result = ready_result == ProcessReadyResult::kCancelled
            ? ECANCELED
            : ETIMEDOUT;
        const uint64_t send_started = pathguard::perf::NowNs();
        const bool sent = ready_result == ProcessReadyResult::kCancelled
            ? MarkSharedCancelled(state)
            : PublishPendingFailure(state, result);
        LOGI("perf companion pid=%d rules=%u ready_ok=0 ready_us=%llu "
             "mount_us=0 result_send_us=%llu total_us=%llu result=%d sent=%d",
             header.pid, plan.count,
             static_cast<unsigned long long>(pathguard::perf::NsToUs(ready_ns)),
             static_cast<unsigned long long>(pathguard::perf::NsToUs(
                 pathguard::perf::ElapsedNs(send_started))),
             static_cast<unsigned long long>(pathguard::perf::NsToUs(
                 pathguard::perf::ElapsedNs(handler_started))),
             result.mount.result, sent ? 1 : 0);
        LOGE("target process readiness failed: pid=%d errno=%d",
             header.pid, result.mount.result);
        WriteRuntimeStatus(module_dir_fd, header.pid,
                           static_cast<uid_t>(header.uid), plan, result.mount,
                           LoadSharedStatus(state));
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }
    state->result.ready_ns = ready_ns;

    pathguard::MountInfoSnapshot candidate_mounts;
    const uint64_t candidate_topology_started = pathguard::perf::NowNs();
    const int candidate_snapshot_error = pathguard::CaptureMountInfoSnapshot(
        &candidate_mounts);
    const bool candidate_topology_valid = candidate_snapshot_error == 0
        && CaptureStorageTopologyFromSnapshot(
            static_cast<uid_t>(header.uid), nullptr,
            candidate_mounts, &plan.topology);
    LOGI("perf topology_candidate pid=%d total_us=%llu read_us=%llu parse_us=%llu rows=%zu errno=%d",
         header.pid,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             pathguard::perf::ElapsedNs(candidate_topology_started))),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             candidate_mounts.read_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             candidate_mounts.parse_ns)),
         candidate_mounts.entry_count, candidate_snapshot_error);
    pathguard::DestroyMountInfoSnapshot(&candidate_mounts);
    if (!candidate_topology_valid) {
        CompanionResult result = state->result;
        result.mount.result = ENOTSUP;
        result.mount.runtime_reason = static_cast<uint32_t>(
            pathguard::RuntimeReason::kTopologyChanged);
        PublishPendingFailure(state, result);
        LOGE("storage topology unsupported: pid=%d uid=%d", header.pid, header.uid);
        WriteRuntimeStatus(module_dir_fd, header.pid,
                           static_cast<uid_t>(header.uid), plan, result.mount,
                           LoadSharedStatus(state));
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }

    // P0a: warm the capability probe cache in the persistent parent process
    // before forking, so the per-request child inherits it (COW) and skips the
    // ~100ms isolated probe. The probe is namespace independent (verified on
    // device) so a representative storage path pair built from the first rule
    // and the requesting uid is sufficient; the outcome is path independent.
    {
        char warm_source[PATH_MAX]{};
        char warm_target[PATH_MAX]{};
        char deny_anchor_path[PATH_MAX]{};
        const char* warm_visible = PlanPath(plan, plan.mounts[0].visible_path);
        const bool needs_deny_anchor =
            (RequiredMountActions(plan) & pathguard::kMountActionDenyAnchor) != 0;
        if (warm_visible != nullptr
            && (!needs_deny_anchor
                || BuildDenyAnchorPath(module_dir_fd, deny_anchor_path,
                                       sizeof(deny_anchor_path)))
            && BuildPathUnderRoot(plan.topology.visible_root, warm_visible,
                                  warm_target, sizeof(warm_target))
            && BuildMountSourcePath(plan, plan.mounts[0], deny_anchor_path,
                                    warm_source, sizeof(warm_source))) {
            bool warm_hit = false;
            const uint64_t warm_started = pathguard::perf::NowNs();
            StoreSharedPreflightPhase(
                state, pathguard::MountPreflightPhase::kCapabilityProbe);
            GetOrProbeMountBackends(warm_source, warm_target,
                                    plan.policy_flags, plan.topology.generation,
                                    &warm_hit);
            StoreSharedPreflightPhase(
                state, pathguard::MountPreflightPhase::kIdle);
            LOGI("perf probe_warm pid=%d warm_us=%llu cache_hit=%d",
                 header.pid,
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(
                     pathguard::perf::ElapsedNs(warm_started))),
                 warm_hit ? 1 : 0);
        }
    }

    int result_sockets[2];
    MountPerfResult mount_result;
    bool mount_result_received = false;
    bool owner_death_tainted = false;
    struct stat target_namespace_identity {};
    char target_namespace_path[64]{};
    snprintf(target_namespace_path, sizeof(target_namespace_path),
             "/proc/%d/ns/mnt", header.pid);
    const bool namespace_identity_ready =
        stat(target_namespace_path, &target_namespace_identity) == 0;
    const uint64_t mount_started = pathguard::perf::NowNs();
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, result_sockets) == 0) {
        const pid_t child = fork();
        if (child == 0) {
            close(result_sockets[0]);
            const MountPerfResult child_result = ApplyProcessPlan(
                header.pid, static_cast<uid_t>(header.uid), module_dir_fd,
                plan, state);
            close(module_dir_fd);
            send(result_sockets[1], &child_result, sizeof(child_result), MSG_NOSIGNAL);
            close(result_sockets[1]);
            _exit(child_result.result == 0 ? 0 : 1);
        }
        close(result_sockets[1]);
        if (child > 0) {
            SetSocketTimeout(result_sockets[0], kCompanionIoTimeoutMs);
            mount_result_received = ReadFully(
                result_sockets[0], &mount_result, sizeof(mount_result));
            if (!mount_result_received) {
                const MountState current = LoadSharedStatus(state);
                if (current == MountState::kApplying
                    || current == MountState::kCancelRequested) {
                    kill(child, SIGKILL);
                    owner_death_tainted = true;
                    mount_result = {};
                    mount_result.result = EOWNERDEAD;
                    mount_result.runtime_reason = static_cast<uint32_t>(
                        pathguard::RuntimeReason::kOwnerDeath);
                } else {
                    kill(child, SIGTERM);
                    const timeval grace{0, kChildTerminateGraceMs * 1000};
                    setsockopt(result_sockets[0], SOL_SOCKET, SO_RCVTIMEO,
                               &grace, sizeof(grace));
                    mount_result_received = ReadFully(
                        result_sockets[0], &mount_result, sizeof(mount_result));
                    if (!mount_result_received) {
                        kill(child, SIGKILL);
                        mount_result = {};
                        mount_result.result = EIO;
                    }
                }
            }
            while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
            const MountState after_wait = LoadSharedStatus(state);
            if (owner_death_tainted
                && (after_wait == MountState::kApplying
                    || after_wait == MountState::kCancelRequested)) {
                CompanionResult result = state->result;
                result.mount = mount_result;
                PublishSharedResult(state, result, after_wait,
                                    MountState::kNamespaceTainted);
                NamespaceTerminationResult termination;
                if (namespace_identity_ready) {
                    termination = TerminateNamespaceMembers(
                        target_namespace_identity.st_dev,
                        target_namespace_identity.st_ino);
                }
                LOGE("mount worker owner death: pid=%d namespace_known=%d matched=%zu signaled=%zu remaining=%zu",
                     header.pid, namespace_identity_ready ? 1 : 0,
                     termination.matched, termination.signaled,
                     termination.remaining);
            }
        } else {
            mount_result = {};
            mount_result.result = errno;
        }
        close(result_sockets[0]);
    } else {
        mount_result = {};
        mount_result.result = errno;
    }
    const uint64_t mount_ns = pathguard::perf::ElapsedNs(mount_started);
    const MountState final_state = LoadSharedStatus(state);
    if (final_state == MountState::kPending) {
        CompanionResult result = state->result;
        result.mount = mount_result;
        if (mount_result.result == 0) {
            result.mount.result = EPROTO;
        }
        PublishPendingFailure(state, result);
    } else if (mount_result_received
               && final_state == MountState::kCancelRequested
               && mount_result.result != 0) {
        CompanionResult result = state->result;
        result.mount = mount_result;
        PublishSharedResult(state, result, MountState::kCancelRequested,
                            MountState::kRollbackComplete);
    } else if (mount_result_received && final_state == MountState::kApplying) {
        CompanionResult result = state->result;
        result.mount = mount_result;
        PublishSharedResult(
            state, result, MountState::kApplying,
            mount_result.result == 0 ? MountState::kComplete
                                     : MountState::kRollbackComplete);
    }

    WriteRuntimeStatus(module_dir_fd, header.pid, static_cast<uid_t>(header.uid),
                       plan, mount_result, LoadSharedStatus(state));

    LOGI("perf companion pid=%d rules=%u ready_ok=1 ready_us=%llu mount_us=%llu "
         "setns_us=%llu topology_us=%llu source_pin_us=%llu policy_us=%llu "
         "propagation_check_us=%llu propagation_us=%llu "
         "mount_total_us=%llu mount_max_us=%llu "
         "rollback_us=%llu mi_snapshots=%u mi_read_us=%llu mi_parse_us=%llu "
         "total_us=%llu result=%d committed=%d",
         header.pid, plan.count,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(ready_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.setns_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.topology_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.source_pin_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             mount_result.policy_revalidate_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             mount_result.propagation_check_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.propagation_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.mount_total_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.mount_max_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.rollback_ns)),
         mount_result.mountinfo_snapshot_count,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             mount_result.mountinfo_read_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             mount_result.mountinfo_parse_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             pathguard::perf::ElapsedNs(handler_started))),
         mount_result.result,
         LoadSharedStatus(state) == MountState::kComplete ? 1 : 0);

    close(module_dir_fd);
    munmap(state, sizeof(*state));
    if (mount_result.result == 0) {
        LOGI("companion redirect mount applied: pid=%d rules=%u backend=%u",
             header.pid, plan.count, mount_result.backend);
    } else {
        LOGE("companion redirect mount failed: pid=%d errno=%d backend=%u reason=%u",
             header.pid, mount_result.result, mount_result.backend,
             mount_result.backend_reason);
    }
}

}  // namespace

REGISTER_ZYGISK_MODULE(PathGuardModule)
REGISTER_ZYGISK_COMPANION(CompanionHandler)
