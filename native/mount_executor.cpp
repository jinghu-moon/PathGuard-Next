#include "pathguard/mount_executor.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/mount.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace pathguard {
namespace {

constexpr char kDenyFilesystem[] = "pathguard_deny";

int BuildProcFdPath(int fd, char* output, size_t output_size) {
    if (fd < 0 || output == nullptr || output_size == 0) return EINVAL;
    const int written = snprintf(output, output_size, "/proc/self/fd/%d", fd);
    return written >= 0 && static_cast<size_t>(written) < output_size
        ? 0
        : ENAMETOOLONG;
}

}  // namespace

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
}

int MountDenyDirectoryFd(int target_fd) {
    char target[64]{};
    const int error = BuildProcFdPath(target_fd, target, sizeof(target));
    if (error != 0) return error;
    constexpr unsigned long kDenyMountFlags = MS_NOSUID | MS_NODEV | MS_NOEXEC;
    return mount(kDenyFilesystem, target, "tmpfs", kDenyMountFlags,
                 "mode=000,size=4096") == 0
        ? 0
        : errno;
}

int UnmountDirectoryFd(int target_fd) {
    char target[64]{};
    const int error = BuildProcFdPath(target_fd, target, sizeof(target));
    if (error != 0) return error;
    return umount2(target, MNT_DETACH) == 0 ? 0 : errno;
}

}  // namespace pathguard
