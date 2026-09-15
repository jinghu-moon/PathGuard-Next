/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PATHGUARD_VFS_TRACE_PROBE_UAPI_H
#define PATHGUARD_VFS_TRACE_PROBE_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PATHGUARD_VFS_TRACE_ABI_VERSION 1U
#define PATHGUARD_VFS_TRACE_MAX_PROBES 6U
#define PATHGUARD_VFS_TRACE_NAME_SIZE 32U

#define PATHGUARD_VFS_TRACE_STATE_FAILED 0U
#define PATHGUARD_VFS_TRACE_STATE_READY 1U

struct pathguard_vfs_trace_counter {
	char name[PATHGUARD_VFS_TRACE_NAME_SIZE];
	__u64 hits;
	__u64 nmissed;
	__u32 registered;
	__u32 reserved;
};

struct pathguard_vfs_trace_status {
	__u32 abi_version;
	__u32 size;
	__u32 state;
	__s32 last_error;
	__u32 probe_count;
	__u32 registered_count;
	char kernel_release[128];
	struct pathguard_vfs_trace_counter counters[PATHGUARD_VFS_TRACE_MAX_PROBES];
};

#define PATHGUARD_VFS_TRACE_IOC_MAGIC 0xB9
#define PATHGUARD_VFS_TRACE_IOC_STATUS \
	_IOR(PATHGUARD_VFS_TRACE_IOC_MAGIC, 1, struct pathguard_vfs_trace_status)

#endif
