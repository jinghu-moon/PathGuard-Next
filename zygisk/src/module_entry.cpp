#include <android/log.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
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

#include "pathguard/media_query_hook.hpp"
#include "pathguard/mount_transaction.h"
#include "pathguard/perf_clock.hpp"
#include "pathguard/policy_format.h"
#include "pathguard/policy_index.h"
#include "zygisk.hpp"

namespace {

constexpr char kLogTag[] = "PathGuard";
constexpr char kPolicyPath[] = "run/policy.bin";
constexpr char kDenyFilesystem[] = "pathguard_deny";
constexpr uint32_t kPolicyMagic = pathguard::binary_format::kMagic;
constexpr uint16_t kPolicyFormatVersion = pathguard::binary_format::kFormatVersion;
constexpr size_t kPolicyHeaderSize = pathguard::binary_format::kHeaderSize;
constexpr size_t kPackageEntrySize = pathguard::binary_format::kPackageSize;
constexpr size_t kMountRuleEntrySize = pathguard::binary_format::kMountRuleSize;
constexpr size_t kEventRuleEntrySize = pathguard::binary_format::kEventRuleSize;
constexpr uint32_t kBootstrapMagic = 0x50474250;
constexpr uint32_t kBootstrapVersion = 4;
constexpr uint32_t kCompanionResultMagic = 0x52534750;
constexpr uint32_t kCompanionResultVersion = 1;
constexpr uint32_t kSharedStateMagic = 0x53534750;
constexpr uint32_t kSharedStateVersion = 2;
constexpr size_t kMaxMountRules = 64;
constexpr size_t kMaxPlanPathBytes = 64 * 1024;
constexpr int kProcessReadyTimeoutMs = 5000;
constexpr int kCompanionIoTimeoutMs = 5000;
constexpr int kAppResultTimeoutMs = 300;
constexpr int kChildTerminateGraceMs = 1000;
#ifndef PATHGUARD_TEST_MOUNT_DELAY_MS
#define PATHGUARD_TEST_MOUNT_DELAY_MS 0
#endif
#ifndef PATHGUARD_TEST_PRE_LEASE_DELAY_MS
#define PATHGUARD_TEST_PRE_LEASE_DELAY_MS 0
#endif

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

struct PlannedMount {
    uint8_t action = 0;
    uint32_t visible_path = 0;
    uint32_t backing_path = 0;
};

struct ProcessPlan {
    uint64_t snapshot_generation = 0;
    uint64_t plan_generation = 0;
    uint32_t count = 0;
    uint32_t path_bytes = 1;
    bool media_compat = false;
    PlannedMount mounts[kMaxMountRules]{};
    char paths[kMaxPlanPathBytes]{};
};

struct BootstrapHeader {
    uint32_t magic;
    uint32_t version;
    int32_t pid;
    int32_t uid;
    uint32_t rule_count;
    uint64_t snapshot_generation;
    uint64_t plan_generation;
};

struct PolicyLoadPerf {
    uint64_t open_map_ns = 0;
    uint64_t lookup_ns = 0;
    uint64_t unmap_ns = 0;
};

struct MountPerfResult {
    int32_t result = EIO;
    uint32_t rule_count = 0;
    uint64_t setns_ns = 0;
    uint64_t propagation_ns = 0;
    uint64_t mount_total_ns = 0;
    uint64_t mount_max_ns = 0;
    uint64_t rollback_ns = 0;
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
    uint32_t reserved = 0;
    CompanionResult result;
};

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
            || ReadLe16(entry + 42) != 0 || ReadLe32(entry + 44) != 0) {
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
    if (plan == nullptr) return false;
    *plan = {};
    plan->path_bytes = 1;
    const uint64_t open_map_started = pathguard::perf::NowNs();
    const int policy_fd = openat(module_dir, kPolicyPath, O_RDONLY | O_CLOEXEC);
    if (policy_fd < 0) {
        if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
        return false;
    }

    struct stat file_stat {};
    if (fstat(policy_fd, &file_stat) != 0 || file_stat.st_size < static_cast<off_t>(kPolicyHeaderSize)) {
        close(policy_fd);
        if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
        return false;
    }
    const size_t size = static_cast<size_t>(file_stat.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, policy_fd, 0);
    close(policy_fd);
    if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
    if (mapping == MAP_FAILED) return false;

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
        && ReadLe32(data + 52) == 0
        && pathguard::binary_format::Crc32(data + kPolicyHeaderSize,
                                          size - kPolicyHeaderSize)
            == payload_checksum
        && ValidPackageIndex(data, size, package_count, package_offset,
                             string_offset)
        && ValidPolicyTables(data, size, package_count, mount_count, event_count,
                             package_offset, mount_offset, event_offset,
                             string_offset);
    if (!valid_header) {
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
    if (entry == nullptr) return finish(false);
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
    if (failure_mode != 0 || media_compat > 1 || first_event > event_count
        || app_event_count > event_count - first_event || app_event_count != 0
        || ReadLe16(entry + 42) != 0 || ReadLe32(entry + 44) != 0) {
        return finish(false);
    }
    plan->snapshot_generation = ReadLe64(
        data + pathguard::binary_format::kContentGenerationOffset);
    plan->plan_generation = ReadLe64(
        entry + pathguard::binary_format::kPackagePlanGenerationOffset);
    plan->media_compat = media_compat == 1;
    if (package_name == nullptr || users == nullptr || processes == nullptr
        || !ProcessMatches(package_name, processes, process_name)
        || !ListContains(users, user_id)) {
        return finish(false);
    }
    if (first_rule > mount_count || rule_count > mount_count - first_rule
        || rule_count > kMaxMountRules) {
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
        if (action != 0 || rule[1] != 0 || ReadLe16(rule + 6) != 0
            || visible == nullptr || backing == nullptr
            || strlen(visible) >= PATH_MAX || strlen(backing) >= PATH_MAX) {
            plan->count = 0;
            return finish(false);
        }
        PlannedMount& mount = plan->mounts[plan->count++];
        mount.action = action;
        if (!StorePlanPath(plan, visible, &mount.visible_path)
            || !StorePlanPath(plan, backing, &mount.backing_path)) {
            plan->count = 0;
            return finish(false);
        }
    }
    return finish(plan->count > 0);
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

bool BuildUserStoragePath(const char* logical_path, uid_t uid,
                           char* output, size_t output_size) {
    if (!IsAllowedTarget(logical_path) || output == nullptr || output_size == 0) {
        return false;
    }

    const unsigned user_id = static_cast<unsigned>(uid) / 100000u;
    const int written = snprintf(output, output_size, "/storage/emulated/%u/%s",
                                 user_id, logical_path);
    return written >= 0 && static_cast<size_t>(written) < output_size;
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

uint32_t StateValue(MountState state) {
    return static_cast<uint32_t>(state);
}

MountState LoadSharedStatus(const SharedMountState* state) {
    return static_cast<MountState>(
        __atomic_load_n(&state->status, __ATOMIC_ACQUIRE));
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

enum class CancelDisposition {
    kPreMutation,
    kApplying,
    kTerminal,
};

CancelDisposition RequestSharedCancel(SharedMountState* state) {
    while (true) {
        const MountState current = LoadSharedStatus(state);
        if (current == MountState::kPending
            && TransitionSharedState(state, MountState::kPending,
                                     MountState::kCancelRequested)) {
            return CancelDisposition::kPreMutation;
        }
        if (current == MountState::kApplying
            && TransitionSharedState(state, MountState::kApplying,
                                     MountState::kCancelRequested)) {
            return CancelDisposition::kApplying;
        }
        if (pathguard::IsMountTransactionTerminal(current)) {
            return CancelDisposition::kTerminal;
        }
        if (current == MountState::kCancelRequested) {
            return CancelDisposition::kApplying;
        }
    }
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

MountPerfResult ApplyProcessPlan(pid_t pid, uid_t uid, const ProcessPlan& plan,
                                 SharedMountState* state) {
    MountPerfResult perf;
    perf.rule_count = plan.count;
    if (IsCancelRequested(state)) {
        perf.result = ECANCELED;
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
    close(namespace_fd);
    perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);

#if PATHGUARD_TEST_PRE_LEASE_DELAY_MS > 0
    LOGI("transaction test pre-lease delay: pid=%d delay_ms=%d",
         pid, PATHGUARD_TEST_PRE_LEASE_DELAY_MS);
    usleep(static_cast<useconds_t>(PATHGUARD_TEST_PRE_LEASE_DELAY_MS) * 1000u);
#endif
    if (!AcquireMutationLease(state)) {
        perf.result = ECANCELED;
        return perf;
    }

    const uint64_t propagation_started = pathguard::perf::NowNs();
    if (mount(nullptr, "/storage", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
        LOGE("storage mount propagation isolation unavailable: errno=%d", errno);
    }
    perf.propagation_ns = pathguard::perf::ElapsedNs(propagation_started);

    char applied_targets[kMaxMountRules][PATH_MAX]{};
    size_t applied_count = 0;
    int error = 0;
    const uint64_t mounts_started = pathguard::perf::NowNs();
    for (size_t rule_index = 0; rule_index < plan.count && error == 0; ++rule_index) {
        if (IsCancelRequested(state)) {
            error = ECANCELED;
            break;
        }
        const uint64_t mount_started = pathguard::perf::NowNs();
        char canonical_path[PATH_MAX]{};
        const char* visible_path = PlanPath(
            plan, plan.mounts[rule_index].visible_path);
        if (plan.mounts[rule_index].action != 0 || visible_path == nullptr
            || !BuildUserStoragePath(visible_path, uid,
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
#if PATHGUARD_TEST_MOUNT_DELAY_MS > 0
        if (applied_count == 1) {
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
    if (error != 0) {
        const uint64_t rollback_started = pathguard::perf::NowNs();
        while (applied_count > 0) {
            umount2(applied_targets[--applied_count], MNT_DETACH);
        }
        perf.rollback_ns = pathguard::perf::ElapsedNs(rollback_started);
    }
    perf.result = error;
    if (error != 0) {
        CompanionResult result = state->result;
        result.mount = perf;
        const MountState current = LoadSharedStatus(state);
        if (current == MountState::kCancelRequested) {
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
    const uint64_t deadline = pathguard::perf::NowNs()
        + static_cast<uint64_t>(timeout_ms) * 1000000u;
    while (true) {
        const MountState current = LoadSharedStatus(state);
        if (pathguard::MountTransactionHasResult(current)) return true;
        if (current == MountState::kCancelled) return false;
        const uint64_t now = pathguard::perf::NowNs();
        if (now >= deadline) {
            const CancelDisposition disposition = RequestSharedCancel(state);
            if (disposition == CancelDisposition::kPreMutation) return false;
            if (disposition == CancelDisposition::kTerminal) {
                return pathguard::MountTransactionHasResult(LoadSharedStatus(state));
            }
            while (true) {
                const MountState applying_state = LoadSharedStatus(state);
                if (pathguard::MountTransactionHasResult(applying_state)) return true;
                if (applying_state == MountState::kCancelled) return false;
                syscall(SYS_futex, &state->status, FUTEX_WAIT,
                        StateValue(applying_state), nullptr, nullptr, 0);
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

bool SendPlanWithSharedFd(int fd, jint uid, const ProcessPlan& plan, int shared_fd) {
    BootstrapHeader header{};
    header.magic = kBootstrapMagic;
    header.version = kBootstrapVersion;
    header.pid = getpid();
    header.uid = uid;
    header.rule_count = plan.count;
    header.snapshot_generation = plan.snapshot_generation;
    header.plan_generation = plan.plan_generation;
    char control[CMSG_SPACE(sizeof(int))]{};
    iovec header_iov{&header, sizeof(header)};
    msghdr message{};
    message.msg_iov = &header_iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr* descriptor = CMSG_FIRSTHDR(&message);
    descriptor->cmsg_level = SOL_SOCKET;
    descriptor->cmsg_type = SCM_RIGHTS;
    descriptor->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(descriptor), &shared_fd, sizeof(shared_fd));
    const ssize_t sent = sendmsg(fd, &message, MSG_NOSIGNAL);
    if (sent != static_cast<ssize_t>(sizeof(header))) return false;
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

bool ReceiveBootstrap(int fd, BootstrapHeader* header, int* shared_fd) {
    if (header == nullptr || shared_fd == nullptr) return false;
    *shared_fd = -1;
    char control[CMSG_SPACE(sizeof(int))]{};
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
            && descriptor->cmsg_len >= CMSG_LEN(sizeof(int))) {
            memcpy(shared_fd, CMSG_DATA(descriptor), sizeof(int));
            break;
        }
    }
    return *shared_fd >= 0;
}

class PathGuardModule final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
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
        const bool matched = module_dir >= 0
            && LoadProcessPlan(module_dir, process_name, args->uid, &plan, &policy_perf);
        if (module_dir >= 0) close(module_dir);
        env_->ReleaseStringUTFChars(args->nice_name, process_name);
        if (!matched) {
            Unload();
            return;
        }

        media_query_hook_required_ = plan.media_compat;
        media_plan_ = plan;
        media_uid_ = args->uid;
        const uint64_t hook_ns = 0;

        const uint64_t connect_started = pathguard::perf::NowNs();
        const int companion_fd = api_->connectCompanion();
        const uint64_t connect_ns = pathguard::perf::ElapsedNs(connect_started);
        if (companion_fd < 0) {
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
            Unload();
            return;
        }
        const uint64_t send_started = pathguard::perf::NowNs();
        if (!SendPlanWithSharedFd(companion_fd, args->uid, plan, shared_fd)) {
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
        close(companion_fd);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        const uint64_t post_started = pathguard::perf::NowNs();
        if (mount_request_sent_ && mount_shared_state_ != nullptr) {
            CompanionResult result;
            const uint64_t result_started = pathguard::perf::NowNs();
            const bool received = WaitForSharedResult(mount_shared_state_, kAppResultTimeoutMs);
            const uint64_t result_ns = pathguard::perf::ElapsedNs(result_started);
            if (received) result = mount_shared_state_->result;
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
            if (valid && result.mount.result == 0) {
                LOGI("deny mount active: pid=%d", getpid());
                if (media_query_hook_required_) {
                    char primary_paths[kMaxMountRules][PATH_MAX]{};
                    const char* deny_paths[kMaxMountRules]{};
                    for (uint32_t index = 0; index < media_plan_.count; ++index) {
                        const char* visible = PlanPath(
                            media_plan_, media_plan_.mounts[index].visible_path);
                        if (visible == nullptr || !BuildUserStoragePath(
                                visible, 0,
                                primary_paths[index], sizeof(primary_paths[index]))) {
                            media_query_hook_required_ = false;
                            break;
                        }
                        deny_paths[index] = primary_paths[index];
                    }
                    const uint64_t hook_started = pathguard::perf::NowNs();
                    if (media_query_hook_required_) {
                        media_query_hook_installed_ = pathguard::media_query::Install(
                            api_, env_, deny_paths, media_plan_.count, media_uid_);
                    }
                    LOGI("perf media_hook_install installed=%d elapsed_us=%llu",
                         media_query_hook_installed_ ? 1 : 0,
                         static_cast<unsigned long long>(pathguard::perf::NsToUs(
                             pathguard::perf::ElapsedNs(hook_started))));
                    if (!media_query_hook_installed_) {
                        LOGE("media query hook unavailable after successful mount");
                    }
                }
            } else {
                LOGE("deny mount failed or timed out; fail-open: pid=%d", getpid());
            }
        }
        if (!media_query_hook_installed_) Unload();
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs*) override { Unload(); }

private:
    void Unload() { api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }

    zygisk::Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool media_query_hook_installed_ = false;
    bool media_query_hook_required_ = false;
    bool mount_request_sent_ = false;
    SharedMountState* mount_shared_state_ = nullptr;
    ProcessPlan media_plan_;
    jint media_uid_ = 0;
};

void CompanionHandler(int client) {
    const uint64_t handler_started = pathguard::perf::NowNs();
    SetSocketTimeout(client, kCompanionIoTimeoutMs);
    BootstrapHeader header{};
    int shared_fd = -1;
    ProcessPlan plan;
    if (!ReceiveBootstrap(client, &header, &shared_fd)
        || header.magic != kBootstrapMagic
        || header.version != kBootstrapVersion
        || header.pid <= 0
        || header.uid < 10000
        || header.rule_count == 0
        || header.rule_count > kMaxMountRules) {
        if (shared_fd >= 0) close(shared_fd);
        return;
    }
    SharedMountState* state = MapSharedState(shared_fd);
    close(shared_fd);
    if (state == nullptr) return;
    plan.count = header.rule_count;
    plan.path_bytes = 1;
    plan.snapshot_generation = header.snapshot_generation;
    plan.plan_generation = header.plan_generation;
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
            munmap(state, sizeof(*state));
            return;
        }
        visible[visible_length] = '\0';
        backing[backing_length] = '\0';
        if (mount.action != 0 || backing_length != 0
            || !IsAllowedTarget(visible)
            || !StorePlanPath(&plan, visible, &mount.visible_path)
            || !StorePlanPath(&plan, backing, &mount.backing_path)) {
            munmap(state, sizeof(*state));
            return;
        }
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
        munmap(state, sizeof(*state));
        return;
    }
    state->result.ready_ns = ready_ns;

    int result_sockets[2];
    MountPerfResult mount_result;
    bool mount_result_received = false;
    const uint64_t mount_started = pathguard::perf::NowNs();
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, result_sockets) == 0) {
        const pid_t child = fork();
        if (child == 0) {
            close(result_sockets[0]);
            const MountPerfResult child_result = ApplyProcessPlan(
                header.pid, static_cast<uid_t>(header.uid), plan, state);
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
                    const timeval no_timeout{0, 0};
                    setsockopt(result_sockets[0], SOL_SOCKET, SO_RCVTIMEO,
                               &no_timeout, sizeof(no_timeout));
                    mount_result_received = ReadFully(
                        result_sockets[0], &mount_result, sizeof(mount_result));
                    if (!mount_result_received) {
                        mount_result = {};
                        mount_result.result = EIO;
                    }
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

    LOGI("perf companion pid=%d rules=%u ready_ok=1 ready_us=%llu mount_us=%llu "
         "setns_us=%llu propagation_us=%llu mount_total_us=%llu mount_max_us=%llu "
         "rollback_us=%llu total_us=%llu result=%d committed=%d",
         header.pid, plan.count,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(ready_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.setns_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.propagation_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.mount_total_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.mount_max_ns)),
          static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.rollback_ns)),
          static_cast<unsigned long long>(pathguard::perf::NsToUs(
              pathguard::perf::ElapsedNs(handler_started))),
          mount_result.result,
          LoadSharedStatus(state) == MountState::kComplete ? 1 : 0);

    munmap(state, sizeof(*state));
    if (mount_result.result == 0) {
        LOGI("companion deny mount applied: pid=%d rules=%u", header.pid, plan.count);
    } else {
        LOGE("companion deny mount failed: pid=%d errno=%d", header.pid, mount_result.result);
    }
}

}  // namespace

REGISTER_ZYGISK_MODULE(PathGuardModule)
REGISTER_ZYGISK_COMPANION(CompanionHandler)
