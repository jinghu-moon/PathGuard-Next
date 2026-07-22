#include "pathguard/mount_executor.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/mount.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "pathguard/directory_resolver.h"

namespace pathguard {
namespace {

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

int ReadMountInfo(const char* expected_mountpoint, MountInfoIdentity* identity,
                  size_t* mount_count, MountBackendProbeStep* telemetry = nullptr) {
    if (expected_mountpoint == nullptr) return EINVAL;
    FILE* input = fopen("/proc/self/mountinfo", "re");
    if (input == nullptr) return errno;
    size_t count = 0;
    MountInfoIdentity latest;
    char line[8192]{};
    while (fgets(line, sizeof(line), input) != nullptr) {
        ++count;
        unsigned long long mount_id = 0;
        unsigned long long parent_id = 0;
        char device[64]{};
        char root[PATH_MAX]{};
        char mountpoint[PATH_MAX]{};
        int consumed = 0;
        const bool truncated = strchr(line, '\n') == nullptr && !feof(input);
        if (truncated || sscanf(line, "%llu %llu %63s %4095s %4095s %*s %n",
                                &mount_id, &parent_id, device, root, mountpoint,
                                &consumed) < 5) {
            fclose(input);
            return EBADMSG;
        }
        if (strcmp(mountpoint, expected_mountpoint) != 0) continue;
        const char* separator = strstr(line, " - ");
        if (separator == nullptr
            || sscanf(separator + 3, "%63s", latest.filesystem) != 1) {
            fclose(input);
            return EBADMSG;
        }
        latest.mount_id = mount_id;
        latest.parent_id = parent_id;
        strcpy(latest.device, device);
        strcpy(latest.root, root);
        strcpy(latest.mountpoint, mountpoint);
    }
    const int read_error = ferror(input) ? EIO : 0;
    fclose(input);
    if (mount_count != nullptr) *mount_count = count;
    if (read_error != 0) return read_error;
    if (identity != nullptr) *identity = latest;
    return 0;
}

int VerifyAppliedMount(const char* target_path,
                       const PinnedIdentity& source,
                       size_t before_count,
                       MountInfoIdentity* mounted) {
    struct stat target_stat {};
    if (stat(target_path, &target_stat) != 0) return errno;
    if (!SameObject(target_stat, source)) return EXDEV;
    size_t after_count = 0;
    const int error = ReadMountInfo(target_path, mounted, &after_count);
    if (error != 0) return error;
    if (after_count != before_count + 1 || mounted->mount_id == 0) return EBADMSG;
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
        target_path, &mounted, &after_count, telemetry);
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
                        AppliedMount* applied) {
    if (applied == nullptr || source.fd < 0 || target.fd < 0) return EINVAL;
    if (backend == MountBackendKind::kLegacyString) {
        int error = VerifyPinnedDirectory(source_locator.path, source);
        if (error != 0) return error;
        error = VerifyPinnedDirectory(target_locator.path, target);
        if (error != 0) return error;
    }
    size_t before_count = 0;
    int error = ReadMountInfo(target_locator.path, nullptr, &before_count);
    if (error != 0) return error;
    error = ApplyRaw(backend, source.fd, target.fd, source_locator.path,
                     target_locator.path);
    if (error != 0) return error;
    MountInfoIdentity mounted;
    error = VerifyAppliedMount(target_locator.path, source, before_count, &mounted);
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
