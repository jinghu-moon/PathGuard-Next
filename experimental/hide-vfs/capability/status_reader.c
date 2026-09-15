// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "pathguard_vfs_cap_probe_uapi.h"

int main(void)
{
    struct pathguard_vfs_cap_status status;
    int fd = open("/dev/pathguard_vfs_cap_probe", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        return 2;
    }
    if (ioctl(fd, PATHGUARD_VFS_CAP_IOC_STATUS, &status) < 0) {
        perror("ioctl");
        close(fd);
        return 3;
    }
    close(fd);
    printf("abi_version=%u size=%u state=%u last_error=%d available_ops=0x%llx required_ops=0x%llx release=%s\n",
           status.abi_version, status.size, status.state, status.last_error,
           (unsigned long long)status.available_ops,
           (unsigned long long)status.required_ops, status.kernel_release);
    return 0;
}
