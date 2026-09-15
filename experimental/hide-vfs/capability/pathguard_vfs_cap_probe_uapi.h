/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PATHGUARD_VFS_CAP_PROBE_UAPI_H
#define PATHGUARD_VFS_CAP_PROBE_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PATHGUARD_VFS_CAP_ABI_VERSION 1U
#define PATHGUARD_VFS_CAP_STATE_UNSUPPORTED 0U
#define PATHGUARD_VFS_CAP_STATE_READY 1U
#define PATHGUARD_VFS_CAP_OP_LOOKUP (1ULL << 0)
#define PATHGUARD_VFS_CAP_OP_CREATE (1ULL << 1)
#define PATHGUARD_VFS_CAP_OP_MKDIR (1ULL << 2)
#define PATHGUARD_VFS_CAP_OP_MKNOD (1ULL << 3)
#define PATHGUARD_VFS_CAP_OP_SYMLINK (1ULL << 4)
#define PATHGUARD_VFS_CAP_OP_UNLINK (1ULL << 5)
#define PATHGUARD_VFS_CAP_OP_RMDIR (1ULL << 6)
#define PATHGUARD_VFS_CAP_OP_LINK (1ULL << 7)
#define PATHGUARD_VFS_CAP_OP_RENAME (1ULL << 8)
#define PATHGUARD_VFS_CAP_OP_KPROBE (1ULL << 9)
#define PATHGUARD_VFS_CAP_REQUIRED_OPS ((1ULL << 10) - 1ULL)

struct pathguard_vfs_cap_status {
    __u32 abi_version;
    __u32 size;
    __u32 state;
    __s32 last_error;
    __u64 available_ops;
    __u64 required_ops;
    char kernel_release[128];
};

#define PATHGUARD_VFS_CAP_IOC_MAGIC 0xB8
#define PATHGUARD_VFS_CAP_IOC_STATUS _IOR(PATHGUARD_VFS_CAP_IOC_MAGIC, 1, struct pathguard_vfs_cap_status)

#endif
