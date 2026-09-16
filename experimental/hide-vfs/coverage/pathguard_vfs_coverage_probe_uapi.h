/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PATHGUARD_VFS_COVERAGE_PROBE_UAPI_H
#define PATHGUARD_VFS_COVERAGE_PROBE_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PATHGUARD_VFS_COVERAGE_ABI_VERSION 2U
#define PATHGUARD_VFS_COVERAGE_MAX_PROBES 16U
#define PATHGUARD_VFS_COVERAGE_NAME_SIZE 32U

#define PATHGUARD_VFS_COVERAGE_STATE_READY 1U
#define PATHGUARD_VFS_COVERAGE_STATE_PARTIAL 2U
#define PATHGUARD_VFS_COVERAGE_STATE_UNSUPPORTED 3U

struct pathguard_vfs_coverage_counter {
	char name[PATHGUARD_VFS_COVERAGE_NAME_SIZE];
	__u64 hits;
	__u64 nmissed;
	__u32 registered;
	__s32 register_error;
	__u32 required;
	__u32 reserved;
};

struct pathguard_vfs_coverage_status {
	__u32 abi_version;
	__u32 size;
	__u32 state;
	__s32 last_error;
	__u32 probe_count;
	__u32 registered_count;
	__u32 required_count;
	__u32 required_registered_count;
	char kernel_release[128];
	struct pathguard_vfs_coverage_counter counters[PATHGUARD_VFS_COVERAGE_MAX_PROBES];
};

#define PATHGUARD_VFS_COVERAGE_IOC_MAGIC 0xBA
#define PATHGUARD_VFS_COVERAGE_IOC_STATUS \
	_IOR(PATHGUARD_VFS_COVERAGE_IOC_MAGIC, 1, struct pathguard_vfs_coverage_status)

#endif
