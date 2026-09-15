/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PATHGUARD_VFS_PREFLIGHT_UAPI_H
#define PATHGUARD_VFS_PREFLIGHT_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PATHGUARD_VFS_PREFLIGHT_ABI_VERSION 1U
#define PATHGUARD_VFS_PREFLIGHT_PATH_MAX 384U
#define PATHGUARD_VFS_PREFLIGHT_FS_NAME_MAX 32U

#define PATHGUARD_VFS_PREFLIGHT_STATE_EMPTY 0U
#define PATHGUARD_VFS_PREFLIGHT_STATE_READY 1U

#define PATHGUARD_VFS_PREFLIGHT_OP_LOOKUP       (1ULL << 0)
#define PATHGUARD_VFS_PREFLIGHT_OP_ATOMIC_OPEN  (1ULL << 1)
#define PATHGUARD_VFS_PREFLIGHT_OP_READDIR      (1ULL << 2)
#define PATHGUARD_VFS_PREFLIGHT_OP_CREATE       (1ULL << 3)
#define PATHGUARD_VFS_PREFLIGHT_OP_MKDIR        (1ULL << 4)
#define PATHGUARD_VFS_PREFLIGHT_OP_MKNOD        (1ULL << 5)
#define PATHGUARD_VFS_PREFLIGHT_OP_SYMLINK      (1ULL << 6)
#define PATHGUARD_VFS_PREFLIGHT_OP_UNLINK       (1ULL << 7)
#define PATHGUARD_VFS_PREFLIGHT_OP_RMDIR        (1ULL << 8)
#define PATHGUARD_VFS_PREFLIGHT_OP_LINK         (1ULL << 9)
#define PATHGUARD_VFS_PREFLIGHT_OP_RENAME       (1ULL << 10)
#define PATHGUARD_VFS_PREFLIGHT_OP_REVALIDATE   (1ULL << 11)

struct pathguard_vfs_preflight_request {
	__u32 abi_version;
	__u32 size;
	char parent[PATHGUARD_VFS_PREFLIGHT_PATH_MAX];
};

struct pathguard_vfs_preflight_status {
	__u32 abi_version;
	__u32 size;
	__u32 state;
	__s32 last_error;
	__u64 operation_mask;
	__u64 parent_inode;
	__u32 parent_mode;
	__u32 parent_dev_major;
	__u32 parent_dev_minor;
	char filesystem[PATHGUARD_VFS_PREFLIGHT_FS_NAME_MAX];
	char parent[PATHGUARD_VFS_PREFLIGHT_PATH_MAX];
	char kernel_release[128];
};

#define PATHGUARD_VFS_PREFLIGHT_IOC_MAGIC 0xBB
#define PATHGUARD_VFS_PREFLIGHT_IOC_SCAN \
	_IOW(PATHGUARD_VFS_PREFLIGHT_IOC_MAGIC, 1, struct pathguard_vfs_preflight_request)
#define PATHGUARD_VFS_PREFLIGHT_IOC_STATUS \
	_IOR(PATHGUARD_VFS_PREFLIGHT_IOC_MAGIC, 2, struct pathguard_vfs_preflight_status)
#define PATHGUARD_VFS_PREFLIGHT_IOC_CLEAR \
	_IO(PATHGUARD_VFS_PREFLIGHT_IOC_MAGIC, 3)

#endif
