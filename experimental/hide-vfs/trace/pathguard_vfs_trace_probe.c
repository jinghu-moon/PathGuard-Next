// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read-only VFS entry trace probe. It registers kprobes only to count calls;
 * handlers neither inspect nor modify registers, return values, dentries, or
 * inode operation tables.
 */
#include <linux/atomic.h>
#include <linux/kprobes.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>

#include "pathguard_vfs_trace_probe_uapi.h"

#ifndef PATHGUARD_VFS_TRACE_EXPECTED_RELEASE
#define PATHGUARD_VFS_TRACE_EXPECTED_RELEASE \
	"6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"
#endif

struct pathguard_trace_entry {
	struct kprobe probe;
	atomic64_t hits;
	const char *name;
};

static int trace_pre_handler(struct kprobe *probe, struct pt_regs *regs)
{
	struct pathguard_trace_entry *entry;

	(void)regs;
	entry = container_of(probe, struct pathguard_trace_entry, probe);
	atomic64_inc(&entry->hits);
	return 0;
}

static struct pathguard_trace_entry trace_entries[] = {
	{
		.probe = { .symbol_name = "lookup_one_len", .pre_handler = trace_pre_handler },
		.name = "lookup_one_len",
	},
	{
		.probe = { .symbol_name = "vfs_create", .pre_handler = trace_pre_handler },
		.name = "vfs_create",
	},
	{
		.probe = { .symbol_name = "vfs_mkdir", .pre_handler = trace_pre_handler },
		.name = "vfs_mkdir",
	},
	{
		.probe = { .symbol_name = "vfs_unlink", .pre_handler = trace_pre_handler },
		.name = "vfs_unlink",
	},
	{
		.probe = { .symbol_name = "vfs_rmdir", .pre_handler = trace_pre_handler },
		.name = "vfs_rmdir",
	},
	{
		.probe = { .symbol_name = "vfs_rename", .pre_handler = trace_pre_handler },
		.name = "vfs_rename",
	},
};

static struct pathguard_vfs_trace_status trace_status = {
	.abi_version = PATHGUARD_VFS_TRACE_ABI_VERSION,
	.size = sizeof(struct pathguard_vfs_trace_status),
	.probe_count = ARRAY_SIZE(trace_entries),
};

static long trace_ioctl(struct file *file, unsigned int command,
			unsigned long argument)
{
	struct pathguard_vfs_trace_status snapshot;
	unsigned int index;

	(void)file;
	if (command != PATHGUARD_VFS_TRACE_IOC_STATUS)
		return -ENOTTY;

	snapshot = trace_status;
	for (index = 0; index < ARRAY_SIZE(trace_entries); ++index) {
		snapshot.counters[index].hits = atomic64_read(&trace_entries[index].hits);
		snapshot.counters[index].nmissed = READ_ONCE(trace_entries[index].probe.nmissed);
		snapshot.counters[index].registered = trace_entries[index].probe.addr != NULL;
		strscpy(snapshot.counters[index].name, trace_entries[index].name,
			sizeof(snapshot.counters[index].name));
	}
	return copy_to_user((void __user *)argument, &snapshot, sizeof(snapshot)) ?
		-EFAULT : 0;
}

static const struct file_operations trace_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = trace_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = trace_ioctl,
#endif
};

static struct miscdevice trace_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "pathguard_vfs_trace_probe",
	.fops = &trace_fops,
	.mode = 0600,
};

static void unregister_entries(unsigned int count)
{
	while (count > 0) {
		--count;
		if (trace_entries[count].probe.addr != NULL)
			unregister_kprobe(&trace_entries[count].probe);
	}
}

static int __init pathguard_trace_init(void)
{
	unsigned int index;
	int ret;

	if (strcmp(init_utsname()->release, PATHGUARD_VFS_TRACE_EXPECTED_RELEASE) != 0)
		return -ENODEV;
	strscpy(trace_status.kernel_release, init_utsname()->release,
		sizeof(trace_status.kernel_release));

	for (index = 0; index < ARRAY_SIZE(trace_entries); ++index) {
		ret = register_kprobe(&trace_entries[index].probe);
		if (ret) {
			trace_status.last_error = ret;
			unregister_entries(index);
			pr_err("pathguard_vfs_trace_probe: register %s failed: %d\n",
			       trace_entries[index].name, ret);
			return ret;
		}
		++trace_status.registered_count;
	}

	trace_status.state = PATHGUARD_VFS_TRACE_STATE_READY;
	trace_status.last_error = 0;
	ret = misc_register(&trace_device);
	if (ret) {
		unregister_entries(ARRAY_SIZE(trace_entries));
		trace_status.registered_count = 0;
		return ret;
	}
	pr_info("pathguard_vfs_trace_probe: registered=%u release=%s\n",
		trace_status.registered_count, trace_status.kernel_release);
	return 0;
}

static void __exit pathguard_trace_exit(void)
{
	misc_deregister(&trace_device);
	unregister_entries(ARRAY_SIZE(trace_entries));
}

module_init(pathguard_trace_init);
module_exit(pathguard_trace_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("PathGuard");
MODULE_DESCRIPTION("PathGuard read-only VFS kprobe trace probe");
MODULE_VERSION("0.1.0-prototype");
