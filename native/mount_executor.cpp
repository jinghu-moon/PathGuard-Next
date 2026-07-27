#include "pathguard/mount_executor.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
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

bool IsPathOnMount(const char* path, const char* mountpoint) {
    if (path == nullptr || mountpoint == nullptr || path[0] != '/') return false;
    if (strcmp(mountpoint, "/") == 0) return true;
    const size_t length = strlen(mountpoint);
    return strncmp(path, mountpoint, length) == 0
        && (path[length] == '\0' || path[length] == '/');
}

int BuildExpectedBindRoot(const char* absolute_path,
                          const char* mountpoint, const char* root,
                          char* output, size_t capacity) {
    if (!IsPathOnMount(absolute_path, mountpoint)) return EXDEV;
    const char* suffix = absolute_path + strlen(mountpoint);
    if (strcmp(mountpoint, "/") == 0) suffix = absolute_path;
    int written = 0;
    if (suffix[0] == '\0') {
        written = snprintf(output, capacity, "%s", root);
    } else if (strcmp(root, "/") == 0) {
        written = snprintf(output, capacity, "%s", suffix);
    } else {
        written = snprintf(output, capacity, "%s%s%s", root,
                           suffix[0] == '/' ? "" : "/", suffix);
    }
    return written > 0 && static_cast<size_t>(written) < capacity
        ? 0 : ENAMETOOLONG;
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
    MountInfoSnapshot before_snapshot;
    MountInfoSnapshot after_snapshot;
    MountInfoSnapshot remaining_snapshot;
    telemetry->before_error = CaptureMountInfoSnapshot(&before_snapshot);
    telemetry->before_count = before_snapshot.entry_count;
    if (telemetry->before_error != 0) return false;
    telemetry->apply_error = ApplyRaw(
        backend, source.fd, target.fd, source_path, target_path);
    if (telemetry->apply_error != 0) {
        DestroyMountInfoSnapshot(&before_snapshot);
        return false;
    }

    telemetry->after_error = CaptureMountInfoSnapshot(&after_snapshot);
    telemetry->after_count = after_snapshot.entry_count;
    AppliedMount applied;
    applied.backend = backend;
    snprintf(applied.target.path, sizeof(applied.target.path), "%s", target_path);
    CanonicalLocator target_locator;
    snprintf(target_locator.path, sizeof(target_locator.path), "%s", target_path);
    if (telemetry->after_error == 0) {
        const MountError verify = VerifyDirectoryMount(
            before_snapshot, after_snapshot, source, target,
            target_locator, &applied, nullptr);
        telemetry->verify_error = verify.error;
        if (telemetry->verify_error == 0
            && after_snapshot.entry_count != before_snapshot.entry_count + 1) {
            telemetry->verify_error = EBADMSG;
        }
    } else {
        telemetry->verify_error = telemetry->after_error;
    }
    struct stat target_stat {};
    if (stat(target_path, &target_stat) != 0) telemetry->stat_error = errno;
    telemetry->identity_match = telemetry->stat_error == 0
        && SameObject(target_stat, source) ? 1 : 0;
    telemetry->mounted_id = applied.mount_id;
    if (telemetry->after_error == 0) {
        MountRollbackResult rollback = ValidateRollbackDirectoryMount(
            applied, after_snapshot);
        if (rollback.ok()) rollback = UnmountValidatedDirectoryMount(applied);
        telemetry->rollback_error = rollback.failure.error;
    } else {
        telemetry->rollback_error = telemetry->after_error;
    }
    if (telemetry->rollback_error == 0) {
        telemetry->remaining_error = CaptureMountInfoSnapshot(
            &remaining_snapshot);
        if (telemetry->remaining_error == 0) {
            MountPathIdentity remaining;
            const int find_error = FindMountInfoPath(
                remaining_snapshot, target_path, true, &remaining);
            telemetry->remaining_error = find_error;
            telemetry->remaining_id = find_error == 0 ? remaining.mount_id : 0;
            const MountRollbackResult verify = VerifyRollbackDirectoryMount(
                applied, remaining_snapshot);
            if (!verify.ok()) telemetry->remaining_error = verify.failure.error;
        }
    }
    telemetry->success = telemetry->verify_error == 0
        && telemetry->identity_match != 0
        && telemetry->rollback_error == 0
        && (telemetry->remaining_error == 0 || telemetry->remaining_error == ENOENT)
        && telemetry->remaining_id != telemetry->mounted_id;
    DestroyMountInfoSnapshot(&remaining_snapshot);
    DestroyMountInfoSnapshot(&after_snapshot);
    DestroyMountInfoSnapshot(&before_snapshot);
    return telemetry->success != 0;
}

}  // namespace

int PinDirectory(const MountInfoSnapshot& snapshot, const char* absolute_path,
                 PinnedIdentity* identity) {
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
    MountPathIdentity mount;
    const int mount_error = FindMountInfoPath(
        snapshot, absolute_path, false, &mount);
    if (mount_error != 0 || mount.device != pinned.st_dev) {
        ClosePinnedIdentity(identity);
        return mount_error != 0 ? mount_error : EXDEV;
    }
    identity->mount.mount_id = mount.mount_id;
    identity->mount.parent_mount_id = mount.parent_mount_id;
    identity->mount.device = mount.device;
    snprintf(identity->mount.root, sizeof(identity->mount.root), "%s", mount.root);
    snprintf(identity->mount.mountpoint, sizeof(identity->mount.mountpoint), "%s",
             mount.mountpoint);
    snprintf(identity->mount.filesystem, sizeof(identity->mount.filesystem), "%s",
             mount.filesystem);
    const int root_error = BuildExpectedBindRoot(
        absolute_path, mount.mountpoint, mount.root, identity->expected_bind_root,
        sizeof(identity->expected_bind_root));
    if (root_error != 0) {
        ClosePinnedIdentity(identity);
        return root_error;
    }
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
    MountInfoSnapshot snapshot;
    int error = CaptureMountInfoSnapshot(&snapshot);
    if (error == 0) error = PinDirectory(snapshot, source_path, &source);
    if (error == 0) error = PinDirectory(snapshot, target_path, &target);
    DestroyMountInfoSnapshot(&snapshot);
    if (error != 0) {
        ClosePinnedIdentity(&source);
        ClosePinnedIdentity(&target);
        probe.error = error;
        return probe;
    }
    probe.capabilities.strict_actions =
        kMountActionRedirect | kMountActionDenyAnchor;
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

MountApplyResult ApplyDirectoryMountRaw(
    MountBackendKind backend, uint32_t operation_id,
    const PinnedIdentity& source, const PinnedIdentity& target,
    const CanonicalLocator& source_locator,
    const CanonicalLocator& target_locator, MountApplyTiming* timing) {
    MountApplyResult result;
    result.mount.backend = backend;
    result.mount.operation_id = operation_id;
    snprintf(result.mount.target.path, sizeof(result.mount.target.path), "%s",
             target_locator.path);
    result.failure.backend = backend;
    result.failure.operation_id = operation_id;
    if (source.fd < 0 || target.fd < 0) {
        result.failure.stage = MountOperationStage::kPreflight;
        result.failure.error = EINVAL;
        return result;
    }
    const bool legacy = backend == MountBackendKind::kLegacyString;
    if (legacy) {
        const uint64_t vp0 = NowNsLocal();
        int error = VerifyPinnedDirectory(source_locator.path, source);
        if (error != 0) {
            result.failure.stage = MountOperationStage::kPreflight;
            result.failure.error = error;
            return result;
        }
        error = VerifyPinnedDirectory(target_locator.path, target);
        if (error != 0) {
            result.failure.stage = MountOperationStage::kPreflight;
            result.failure.error = error;
            return result;
        }
        if (timing != nullptr) timing->verify_pinned_ns = NowNsLocal() - vp0;
    }
    int error = 0;
    const uint64_t ar0 = NowNsLocal();
    error = ApplyRaw(backend, source.fd, target.fd, source_locator.path,
                     target_locator.path);
    if (timing != nullptr) timing->apply_raw_ns = NowNsLocal() - ar0;
    if (error != 0) {
        result.failure.stage = MountOperationStage::kApply;
        result.failure.error = error;
        return result;
    }
    result.failure.mutation_happened = 1;
    result.failure = {};
    result.failure.backend = backend;
    result.failure.operation_id = operation_id;
    result.failure.mutation_happened = 1;
    return result;
}

MountError VerifyDirectoryMount(
    const MountInfoSnapshot& before_snapshot,
    const MountInfoSnapshot& after_snapshot,
    const PinnedIdentity& source, const PinnedIdentity& target,
    const CanonicalLocator& target_locator, AppliedMount* applied,
    MountApplyTiming* timing) {
    MountError failure;
    failure.stage = MountOperationStage::kVerify;
    failure.backend = applied != nullptr
        ? applied->backend : MountBackendKind::kUnsupported;
    failure.operation_id = applied != nullptr ? applied->operation_id : 0;
    failure.mutation_happened = 1;
    if (applied == nullptr || target_locator.path[0] == '\0'
        || before_snapshot.mapping == nullptr
        || after_snapshot.mapping == nullptr
        || before_snapshot.namespace_device != after_snapshot.namespace_device
        || before_snapshot.namespace_inode != after_snapshot.namespace_inode
        || !MountInfoSnapshotMatchesCurrentNamespace(after_snapshot)) {
        failure.error = ESTALE;
        return failure;
    }

    const uint64_t verify_started = NowNsLocal();
    MountPathIdentity mounted;
    int error = FindMountInfoPath(
        after_snapshot, target_locator.path, true, &mounted);
    applied->mount_id = mounted.mount_id;
    failure.actual_mount_id = mounted.mount_id;
    if (timing != nullptr) {
        timing->verify_mountinfo_read_ns = after_snapshot.read_ns;
        timing->verify_mountinfo_parse_ns = after_snapshot.parse_ns;
    }

    const bool legacy = applied->backend == MountBackendKind::kLegacyString;
    if (error == 0 && legacy) {
        const uint64_t stat_started = NowNsLocal();
        struct stat target_stat {};
        if (stat(target_locator.path, &target_stat) != 0) {
            error = errno;
        } else if (!SameObject(target_stat, source)) {
            error = EXDEV;
        }
        if (timing != nullptr) {
            timing->verify_stat_ns = NowNsLocal() - stat_started;
        }
        if (error == 0
            && after_snapshot.entry_count != before_snapshot.entry_count + 1) {
            error = EBADMSG;
        }
    }
    if (error == 0
        && (mounted.mount_id == 0 || mounted.root[0] == '\0'
            || strcmp(mounted.root, source.expected_bind_root) != 0
            || mounted.device != source.device
            || strcmp(mounted.filesystem, source.mount.filesystem) != 0)) {
        error = EXDEV;
    }
    const uint64_t expected_parent = strcmp(
        target_locator.path, target.mount.mountpoint) == 0
        ? target.mount.parent_mount_id : target.mount.mount_id;
    if (error == 0
        && (expected_parent == 0
            || mounted.parent_mount_id != expected_parent)) {
        error = EXDEV;
    }
    if (timing != nullptr) {
        timing->verify_ns = NowNsLocal() - verify_started;
    }
    if (error != 0) {
        failure.error = error;
        return failure;
    }
    failure.stage = MountOperationStage::kNone;
    failure.error = 0;
    failure.identity_confirmed = 1;
    return failure;
}

MountRollbackResult ValidateRollbackDirectoryMount(
    const AppliedMount& applied, const MountInfoSnapshot& snapshot) {
    MountRollbackResult result;
    result.failure.backend = applied.backend;
    result.failure.operation_id = applied.operation_id;
    result.failure.expected_mount_id = applied.mount_id;
    result.failure.mutation_happened = 1;
    if (applied.mount_id == 0) {
        result.failure.stage = MountOperationStage::kRollbackIdentity;
        result.failure.error = ESTALE;
        return result;
    }
    MountPathIdentity current;
    const int error = MountInfoSnapshotMatchesCurrentNamespace(snapshot)
        ? FindMountInfoPath(snapshot, applied.target.path, true, &current)
        : ESTALE;
    if (error != 0 || current.mount_id == 0
        || current.mount_id != applied.mount_id) {
        result.failure.stage = MountOperationStage::kRollbackIdentity;
        result.failure.error = error != 0 ? error : ESTALE;
        result.failure.actual_mount_id = current.mount_id;
        return result;
    }
    result.failure.identity_confirmed = 1;
    result.failure.actual_mount_id = current.mount_id;
    result.failure.error = 0;
    return result;
}

MountRollbackResult UnmountValidatedDirectoryMount(const AppliedMount& applied) {
    MountRollbackResult result;
    result.failure.backend = applied.backend;
    result.failure.operation_id = applied.operation_id;
    result.failure.expected_mount_id = applied.mount_id;
    result.failure.actual_mount_id = applied.mount_id;
    result.failure.mutation_happened = 1;
    result.failure.identity_confirmed = 1;
    if (umount2(applied.target.path, MNT_DETACH) != 0) {
        result.failure.stage = MountOperationStage::kRollbackUnmount;
        result.failure.error = errno;
        return result;
    }
    result.failure.stage = MountOperationStage::kNone;
    result.failure.error = 0;
    return result;
}

MountRollbackResult VerifyRollbackDirectoryMount(
    const AppliedMount& applied, const MountInfoSnapshot& snapshot) {
    MountRollbackResult result;
    result.failure.backend = applied.backend;
    result.failure.operation_id = applied.operation_id;
    result.failure.expected_mount_id = applied.mount_id;
    result.failure.mutation_happened = 1;
    result.failure.identity_confirmed = 1;
    MountPathIdentity remaining;
    const int error = MountInfoSnapshotMatchesCurrentNamespace(snapshot)
        ? FindMountInfoPath(snapshot, applied.target.path, true, &remaining)
        : ESTALE;
    if (error != 0 && error != ENOENT) {
        result.failure.stage = MountOperationStage::kRollbackVerify;
        result.failure.error = error;
        return result;
    }
    if (error == 0 && remaining.mount_id == applied.mount_id) {
        result.failure.stage = MountOperationStage::kRollbackVerify;
        result.failure.error = EBUSY;
        result.failure.actual_mount_id = remaining.mount_id;
        return result;
    }
    result.failure = {};
    result.failure.backend = applied.backend;
    result.failure.operation_id = applied.operation_id;
    result.failure.expected_mount_id = applied.mount_id;
    result.failure.mutation_happened = 1;
    result.failure.identity_confirmed = 1;
    return result;
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
