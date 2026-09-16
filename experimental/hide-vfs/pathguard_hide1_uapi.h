/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PATHGUARD_HIDE1_UAPI_H
#define PATHGUARD_HIDE1_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PATHGUARD_HIDE1_ABI_VERSION 2U
#define PATHGUARD_HIDE1_PATH_MAX 384U
#define PATHGUARD_HIDE1_NAME_MAX 255U

#define PATHGUARD_HIDE1_STATE_UNSUPPORTED 0U
#define PATHGUARD_HIDE1_STATE_INACTIVE 1U
#define PATHGUARD_HIDE1_STATE_ACTIVE 2U

#define PATHGUARD_HIDE1_LIFECYCLE_FREE       0U
#define PATHGUARD_HIDE1_LIFECYCLE_READY      1U
#define PATHGUARD_HIDE1_LIFECYCLE_RUNNING    2U
#define PATHGUARD_HIDE1_LIFECYCLE_STOP_NEW   3U
#define PATHGUARD_HIDE1_LIFECYCLE_RESTORE    4U
#define PATHGUARD_HIDE1_LIFECYCLE_DRAINING   5U

#define PATHGUARD_HIDE1_OP_LOOKUP       (1ULL << 0)
#define PATHGUARD_HIDE1_OP_ATOMIC_OPEN  (1ULL << 1)
#define PATHGUARD_HIDE1_OP_READDIR      (1ULL << 2)
#define PATHGUARD_HIDE1_OP_CREATE       (1ULL << 3)
#define PATHGUARD_HIDE1_OP_MKDIR        (1ULL << 4)
#define PATHGUARD_HIDE1_OP_MKNOD        (1ULL << 5)
#define PATHGUARD_HIDE1_OP_SYMLINK      (1ULL << 6)
#define PATHGUARD_HIDE1_OP_UNLINK       (1ULL << 7)
#define PATHGUARD_HIDE1_OP_RMDIR        (1ULL << 8)
#define PATHGUARD_HIDE1_OP_LINK         (1ULL << 9)
#define PATHGUARD_HIDE1_OP_RENAME       (1ULL << 10)
#define PATHGUARD_HIDE1_OP_REVALIDATE   (1ULL << 11)
#define PATHGUARD_HIDE1_REQUIRED_OPS    ((1ULL << 12) - 1ULL)

struct pathguard_hide1_rule {
    __u32 abi_version;
    __u32 size;
    __u32 target_uid;
    __s32 target_pid;
    __u64 expected_generation;
    char parent[PATHGUARD_HIDE1_PATH_MAX];
    char basename[PATHGUARD_HIDE1_NAME_MAX + 1U];
};

struct pathguard_hide1_status {
    __u32 abi_version;
    __u32 size;
    __u32 state;
    __u32 lifecycle;
    __s32 last_error;
    __u32 target_uid;
    __s32 target_pid;
    __u64 target_mnt_ns;
    __u64 generation;
    __u64 operation_mask;
    __u64 parent_inode;
    __u64 lookup_calls;
    __u64 lookup_hidden;
    __u64 atomic_open_calls;
    __u64 atomic_open_hidden;
    __u64 readdir_calls;
    __u64 readdir_filtered;
    __u64 d_revalidate_calls;
    __u64 d_revalidate_hidden;
    __u64 dentry_install_calls;
    __u64 dentry_install_success;
    __u64 dentry_install_failures;
    __u64 iop_active;
    __u64 fop_active;
    __u64 dop_active;
    __u64 fop_open_count;
    __u64 mutation_calls;
    __u64 mutation_blocked;
    __u64 mutation_original;
    __u64 mutation_unsupported;
    char kernel_release[128];
};

#define PATHGUARD_HIDE1_IOC_MAGIC 0xB7
#define PATHGUARD_HIDE1_IOC_INSTALL \
    _IOW(PATHGUARD_HIDE1_IOC_MAGIC, 1, struct pathguard_hide1_rule)
#define PATHGUARD_HIDE1_IOC_ENABLE \
    _IOW(PATHGUARD_HIDE1_IOC_MAGIC, 2, __u64)
#define PATHGUARD_HIDE1_IOC_DISABLE \
    _IO(PATHGUARD_HIDE1_IOC_MAGIC, 3)
#define PATHGUARD_HIDE1_IOC_CLEAR \
    _IO(PATHGUARD_HIDE1_IOC_MAGIC, 4)
#define PATHGUARD_HIDE1_IOC_STATUS \
    _IOR(PATHGUARD_HIDE1_IOC_MAGIC, 5, struct pathguard_hide1_status)

#endif
