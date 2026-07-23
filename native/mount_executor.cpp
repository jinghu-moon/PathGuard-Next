#include "pathguard/mount_executor.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/mount.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "pathguard/directory_resolver.h"

namespace pathguard {
namespace {

uint64_t NowNsLocal() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull
        + static_cast<uint64_t>(ts.tv_nsec);
}

struct MountInfoIdentity {
    uint64_t mount_id = 0;
    uint64_t parent_id = 0;
    char root[PATH_MAX]{};
    char mountpoint[PATH_MAX]{};
    char filesystem[64]{};
    char device[64]{};
};

int BuildProcFdPath(int fd, char* output, size_t output_size) {
    if (fd < 0 || output == nullptr || output_size == 0) return EINVAL;
    const int written = snprintf(output, output_size, "/proc/self/fd/%d", fd);
    return written >= 0 && static_cast<size_t>(written) < output_size
        ? 0
        : ENAMETOOLONG;
}

bool SameObject(const struct stat& value, const PinnedIdentity& identity) {
    return S_ISDIR(value.st_mode) && value.st_dev == identity.device
        && value.st_ino == identity.inode;
}

struct MountInfoReadTiming {
    uint64_t read_ns = 0;
    uint64_t parse_ns = 0;
};

int ReadMountInfo(const char* expected_mountpoint, MountInfoIdentity* identity,
                  size_t* mount_count, MountInfoReadTiming* read_timing = nullptr) {
    if (expected_mountpoint == nullptr) return EINVAL;
    // P2: read the whole file in one pass (fd read into a growable buffer),
    // then parse from memory. This separates the kernel seq_file generation
    // cost (read) from the parse cost, and avoids per-line stdio overhead.
    const uint64_t read0 = NowNsLocal();
    const int fd = open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return errno;
    size_t capacity = 1 << 16;
    size_t length = 0;
    char* buffer = static_cast<char*>(malloc(capacity));
    if (buffer == nullptr) { close(fd); return ENOMEM; }
    while (true) {
        if (length + 4096 > capacity) {
            const size_t new_capacity = capacity * 2;
            char* grown = static_cast<char*>(realloc(buffer, new_capacity));
            if (grown == nullptr) { free(buffer); close(fd); return ENOMEM; }
            buffer = grown;
            capacity = new_capacity;
        }
        const ssize_t got = read(fd, buffer + length, capacity - length - 1);
        if (got < 0) {
            if (errno == EINTR) continue;
            free(buffer); close(fd); return errno;
        }
        if (got == 0) break;
        length += static_cast<size_t>(got);
    }
    close(fd);
    buffer[length] = '\0';
    if (read_timing != nullptr) read_timing->read_ns = NowNsLocal() - read0;

    const uint64_t parse0 = NowNsLocal();
    const size_t expected_len = strlen(expected_mountpoint);
    size_t count = 0;
    MountInfoIdentity latest;
    char* cursor = buffer;
    int result = 0;
    while (*cursor != '\0') {
        char* line = cursor;
        char* newline = strchr(cursor, '\n');
        if (newline != nullptr) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor = line + strlen(line);
        }
        ++count;
        // Hand-rolled field scan: mountinfo fields are space separated. We only
        // need field 1 (mount_id), 2 (parent_id), 3 (major:minor), 4 (root) and
        // 5 (mountpoint). Field 5 is compared by pointer+length against the
        // expected mountpoint so the common non-matching line costs a few
        // strtoull + a pointer walk, never a 4KB %s copy. This replaces the
        // per-line sscanf("%4095s") that dominated parse time (67ms -> µs).
        char* p = line;
        char* end = nullptr;
        const unsigned long long mount_id = strtoull(p, &end, 10);
        if (end == p) { continue; }
        p = end;
        while (*p == ' ') ++p;
        const unsigned long long parent_id = strtoull(p, &end, 10);
        if (end == p) { continue; }
        p = end;
        // skip field 3 (major:minor)
        while (*p == ' ') ++p;
        char* f3 = p;
        while (*p != ' ' && *p != '\0') ++p;
        if (*p == '\0') { continue; }
        // field 4 (root)
        while (*p == ' ') ++p;
        char* f4 = p;
        while (*p != ' ' && *p != '\0') ++p;
        char* f4_end = p;
        if (*p == '\0') { continue; }
        // field 5 (mountpoint)
        while (*p == ' ') ++p;
        char* f5 = p;
        while (*p != ' ' && *p != '\0') ++p;
        char* f5_end = p;
        const size_t f5_len = static_cast<size_t>(f5_end - f5);
        if (f5_len != expected_len
            || memcmp(f5, expected_mountpoint, f5_len) != 0) {
            continue;
        }
        // Match: extract the fields we retain. Filesystem is the token after
        // the " - " separator.
        const char* separator = strstr(p, " - ");
        if (separator == nullptr) { result = EBADMSG; break; }
        const char* fs = separator + 3;
        while (*fs == ' ') ++fs;
        const char* fs_end = fs;
        while (*fs_end != ' ' && *fs_end != '\0') ++fs_end;
        const size_t fs_len = static_cast<size_t>(fs_end - fs);
        latest.mount_id = mount_id;
        latest.parent_id = parent_id;
        const size_t f3_len = static_cast<size_t>(f4 - f3) - 1;
        const size_t f4_len = static_cast<size_t>(f4_end - f4);
        const size_t dev_copy = f3_len < sizeof(latest.device) - 1
            ? f3_len : sizeof(latest.device) - 1;
        memcpy(latest.device, f3, dev_copy);
        latest.device[dev_copy] = '\0';
        const size_t root_copy = f4_len < sizeof(latest.root) - 1
            ? f4_len : sizeof(latest.root) - 1;
        memcpy(latest.root, f4, root_copy);
        latest.root[root_copy] = '\0';
        const size_t mp_copy = f5_len < sizeof(latest.mountpoint) - 1
            ? f5_len : sizeof(latest.mountpoint) - 1;
        memcpy(latest.mountpoint, f5, mp_copy);
        latest.mountpoint[mp_copy] = '\0';
        const size_t fs_copy = fs_len < sizeof(latest.filesystem) - 1
            ? fs_len : sizeof(latest.filesystem) - 1;
        memcpy(latest.filesystem, fs, fs_copy);
        latest.filesystem[fs_copy] = '\0';
    }
    free(buffer);
    if (read_timing != nullptr) read_timing->parse_ns = NowNsLocal() - parse0;
    if (result != 0) return result;
    if (mount_count != nullptr) *mount_count = count;
    if (identity != nullptr) *identity = latest;
    return 0;
}

int VerifyAppliedMount(const char* target_path,
                       const PinnedIdentity& source,
                       size_t before_count,
                       MountInfoIdentity* mounted,
                       bool require_count_delta,
                       uint64_t* stat_ns = nullptr,
                       uint64_t* read_ns = nullptr,
                       uint64_t* parse_ns = nullptr) {
    const uint64_t stat0 = NowNsLocal();
    struct stat target_stat {};
    if (stat(target_path, &target_stat) != 0) return errno;
    if (stat_ns != nullptr) *stat_ns = NowNsLocal() - stat0;
    if (!SameObject(target_stat, source)) return EXDEV;
    size_t after_count = 0;
    MountInfoReadTiming rt;
    const int error = ReadMountInfo(target_path, mounted, &after_count, &rt);
    if (read_ns != nullptr) *read_ns = rt.read_ns;
    if (parse_ns != nullptr) *parse_ns = rt.parse_ns;
    if (error != 0) return error;
    if (mounted->mount_id == 0) return EBADMSG;
    // ADR-0005 line 61 requires the exact mountinfo delta only for the legacy
    // string-bind backend. strict_fd (line 51) requires mountinfo/source/target
    // identity, which SameObject + a live mountinfo entry for the target already
    // establish; it does not require the before/after line-count delta. Skipping
    // the delta lets strict drop the pre-mount before_scan entirely.
    if (require_count_delta && after_count != before_count + 1) return EBADMSG;
    return 0;
}

int ApplyRaw(MountBackendKind backend, int source_fd, int target_fd,
             const char* source_path, const char* target_path) {
    switch (backend) {
        case MountBackendKind::kStrictOpenTree:
            return MoveMountDirectoryFds(source_fd, target_fd);
        case MountBackendKind::kStrictProcFd:
            return BindMountDirectoryFds(source_fd, target_fd);
        case MountBackendKind::kLegacyString:
            return mount(source_path, target_path, nullptr, MS_BIND, nullptr) == 0
                ? 0 : errno;
        case MountBackendKind::kUnsupported:
            return ENOTSUP;
    }
    return EINVAL;
}

bool ProbeOne(MountBackendKind backend, const PinnedIdentity& source,
              const PinnedIdentity& target, const char* source_path,
              const char* target_path, MountBackendProbeStep* telemetry) {
    if (telemetry == nullptr) return false;
    *telemetry = {};
    size_t before_count = 0;
    telemetry->before_error = ReadMountInfo(
        target_path, nullptr, &before_count);
    telemetry->before_count = before_count;
    if (telemetry->before_error != 0) return false;
    telemetry->apply_error = ApplyRaw(
        backend, source.fd, target.fd, source_path, target_path);
    if (telemetry->apply_error != 0) {
        return false;
    }

    struct stat target_stat {};
    if (stat(target_path, &target_stat) != 0) {
        telemetry->stat_error = errno;
    } else {
        telemetry->identity_match = SameObject(target_stat, source) ? 1 : 0;
    }
    MountInfoIdentity mounted;
    size_t after_count = 0;
    telemetry->after_error = ReadMountInfo(
        target_path, &mounted, &after_count);
    telemetry->after_count = after_count;
    telemetry->mounted_id = mounted.mount_id;
    if (telemetry->stat_error != 0) {
        telemetry->verify_error = telemetry->stat_error;
    } else if (telemetry->identity_match == 0) {
        telemetry->verify_error = EXDEV;
    } else if (telemetry->after_error != 0) {
        telemetry->verify_error = telemetry->after_error;
    } else if (after_count != before_count + 1 || mounted.mount_id == 0) {
        telemetry->verify_error = EBADMSG;
    }
    telemetry->rollback_error = umount2(target_path, MNT_DETACH) == 0
        ? 0 : errno;
    MountInfoIdentity remaining;
    telemetry->remaining_error = ReadMountInfo(
        target_path, &remaining, nullptr);
    telemetry->remaining_id = remaining.mount_id;
    telemetry->success = telemetry->verify_error == 0
        && telemetry->rollback_error == 0
        && telemetry->remaining_error == 0
        && telemetry->remaining_id != telemetry->mounted_id;
    return telemetry->success != 0;
}

}  // namespace

int PinDirectory(const char* absolute_path, PinnedIdentity* identity) {
    if (absolute_path == nullptr || absolute_path[0] != '/' || identity == nullptr) {
        return EINVAL;
    }
    *identity = {};
    struct stat before {};
    if (lstat(absolute_path, &before) != 0) return errno;
    if (!S_ISDIR(before.st_mode) || S_ISLNK(before.st_mode)) return ENOTDIR;
    DirectoryResolveResult root = OpenDirectoryRoot("/");
    if (root.fd < 0) return root.error;
    DirectoryResolveResult resolved = ResolveDirectoryBeneath(
        root.fd, absolute_path + 1, true);
    close(root.fd);
    if (resolved.fd < 0) return resolved.error;
    const int fd = resolved.fd;
    struct stat pinned {};
    if (fstat(fd, &pinned) != 0 || !S_ISDIR(pinned.st_mode)
        || pinned.st_dev != before.st_dev || pinned.st_ino != before.st_ino) {
        const int error = errno == 0 ? ESTALE : errno;
        close(fd);
        return error;
    }
    identity->fd = fd;
    identity->device = pinned.st_dev;
    identity->inode = pinned.st_ino;
    return 0;
}

void ClosePinnedIdentity(PinnedIdentity* identity) {
    if (identity != nullptr && identity->fd >= 0) close(identity->fd);
    if (identity != nullptr) *identity = {};
}

int VerifyPinnedDirectory(const char* absolute_path,
                          const PinnedIdentity& identity) {
    struct stat value {};
    if (lstat(absolute_path, &value) != 0) return errno;
    return SameObject(value, identity) ? 0 : ESTALE;
}

MountBackendProbe ProbeDirectoryMountBackends(const char* source_path,
                                              const char* target_path) {
    MountBackendProbe probe;
    PinnedIdentity source;
    PinnedIdentity target;
    int error = PinDirectory(source_path, &source);
    if (error == 0) error = PinDirectory(target_path, &target);
    if (error != 0) {
        ClosePinnedIdentity(&source);
        ClosePinnedIdentity(&target);
        probe.error = error;
        return probe;
    }
    probe.capabilities.strict_actions = kMountActionRedirect;
    probe.capabilities.legacy_actions = kMountActionRedirect;
    probe.capabilities.primitives |= kCapabilityComponentFdWalk;
    if (ProbeOne(MountBackendKind::kStrictOpenTree, source, target,
                 source_path, target_path, &probe.open_tree)) {
        probe.capabilities.primitives |= kCapabilityOpenTreeMoveMount;
    }
    if (ProbeOne(MountBackendKind::kStrictProcFd, source, target,
                 source_path, target_path, &probe.proc_fd)) {
        probe.capabilities.primitives |= kCapabilityProcFdMount;
    }
    if (ProbeOne(MountBackendKind::kLegacyString, source, target,
                 source_path, target_path, &probe.legacy_string)) {
        probe.capabilities.primitives |= kCapabilityStringBindMount;
    }
    ClosePinnedIdentity(&source);
    ClosePinnedIdentity(&target);
    return probe;
}

int ApplyDirectoryMount(MountBackendKind backend,
                        const PinnedIdentity& source,
                        const PinnedIdentity& target,
                        const CanonicalLocator& source_locator,
                        const CanonicalLocator& target_locator,
                        AppliedMount* applied,
                        MountApplyTiming* timing) {
    if (applied == nullptr || source.fd < 0 || target.fd < 0) return EINVAL;
    if (backend == MountBackendKind::kLegacyString) {
        const uint64_t vp0 = NowNsLocal();
        int error = VerifyPinnedDirectory(source_locator.path, source);
        if (error != 0) return error;
        error = VerifyPinnedDirectory(target_locator.path, target);
        if (error != 0) return error;
        if (timing != nullptr) timing->verify_pinned_ns = NowNsLocal() - vp0;
    }
    // P0b: strict backends do not need the pre-mount full-table scan. ADR-0005
    // requires strict to verify mountinfo source/target identity AFTER mount
    // (line 51); the exact "+1 mountinfo delta" is a legacy-only requirement
    // (line 61). Only legacy reads a before baseline for the delta check.
    size_t before_count = 0;
    int error = 0;
    if (legacy) {
        const uint64_t bs0 = NowNsLocal();
        error = ReadMountInfo(target_locator.path, nullptr, &before_count);
        if (timing != nullptr) timing->before_scan_ns = NowNsLocal() - bs0;
        if (error != 0) return error;
    }
    const uint64_t ar0 = NowNsLocal();
    error = ApplyRaw(backend, source.fd, target.fd, source_locator.path,
                     target_locator.path);
    if (timing != nullptr) timing->apply_raw_ns = NowNsLocal() - ar0;
    if (error != 0) return error;
    MountInfoIdentity mounted;
    const uint64_t v0 = NowNsLocal();
    uint64_t verify_stat_ns = 0;
    uint64_t verify_read_ns = 0;
    uint64_t verify_parse_ns = 0;
    error = VerifyAppliedMount(target_locator.path, source, before_count,
                               &mounted, legacy, &verify_stat_ns,
                               &verify_read_ns, &verify_parse_ns);
    if (timing != nullptr) {
        timing->verify_ns = NowNsLocal() - v0;
        timing->verify_stat_ns = verify_stat_ns;
        timing->verify_read_ns = verify_read_ns;
        timing->verify_parse_ns = verify_parse_ns;
    }
    if (error != 0) {
        umount2(target_locator.path, MNT_DETACH);
        return error;
    }
    applied->backend = backend;
    strcpy(applied->target.path, target_locator.path);
    applied->mount_id = mounted.mount_id;
    return 0;
}

int RollbackDirectoryMount(const AppliedMount& applied) {
    MountInfoIdentity current;
    int error = ReadMountInfo(applied.target.path, &current, nullptr);
    if (error != 0) return error;
    if (current.mount_id == 0 || current.mount_id != applied.mount_id) return ESTALE;
    if (umount2(applied.target.path, MNT_DETACH) != 0) return errno;
    MountInfoIdentity remaining;
    error = ReadMountInfo(applied.target.path, &remaining, nullptr);
    if (error != 0) return error;
    return remaining.mount_id == applied.mount_id ? EBUSY : 0;
}

int BindMountDirectoryFds(int source_fd, int target_fd) {
    char source[64]{};
    char target[64]{};
    int error = BuildProcFdPath(source_fd, source, sizeof(source));
    if (error != 0) return error;
    error = BuildProcFdPath(target_fd, target, sizeof(target));
    if (error != 0) return error;
    return mount(source, target, nullptr, MS_BIND, nullptr) == 0 ? 0 : errno;
}

int MoveMountDirectoryFds(int source_fd, int target_fd) {
    if (source_fd < 0 || target_fd < 0) return EINVAL;
#if defined(__NR_open_tree) && defined(__NR_move_mount)
    const int tree_fd = static_cast<int>(syscall(
        __NR_open_tree, source_fd, "",
        OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH));
    if (tree_fd < 0) return errno;
    const int result = static_cast<int>(syscall(
        __NR_move_mount, tree_fd, "", target_fd, "",
        MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_T_EMPTY_PATH));
    const int error = result == 0 ? 0 : errno;
    close(tree_fd);
    return error;
#else
    return ENOSYS;
#endif
}

int UnmountDirectoryFd(int target_fd) {
    char target[64]{};
    const int error = BuildProcFdPath(target_fd, target, sizeof(target));
    if (error != 0) return error;
    return umount2(target, MNT_DETACH) == 0 ? 0 : errno;
}

}  // namespace pathguard
