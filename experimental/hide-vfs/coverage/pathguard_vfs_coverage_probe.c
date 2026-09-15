// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read-only call-path coverage probe. It observes namei and FUSE entry points
 * with kprobes and only increments counters; it never changes VFS behavior.
 */
#include <linux/atomic.h>
#include <linux/kprobes.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>

#include "pathguard_vfs_coverage_probe_uapi.h"

#ifndef PATHGUARD_VFS_COVERAGE_EXPECTED_RELEASE
#define PATHGUARD_VFS_COVERAGE_EXPECTED_RELEASE \
	"6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"
#endif

struct pathguard_coverage_entry {
	struct kprobe probe;
	atomic64_t hits;
	const char *name;
};

static int coverage_pre_handler(struct kprobe *probe, struct pt_regs *regs)
{
	struct pathguard_coverage_entry *entry;

	(void)regs;
	entry = container_of(probe, struct pathguard_coverage_entry, probe);
	atomic64_inc(&entry->hits);
	return 0;
}

static struct pathguard_coverage_entry coverage_entries[] = {
	{
		.probe = { .symbol_name = "path_openat", .pre_handler = coverage_pre_handler },
		.name = "path_openat",
	},
	{
		.probe = { .symbol_name = "fuse_atomic_open", .pre_handler = coverage_pre_handler },
		.name = "fuse_atomic_open",
	},
	{
		.probe = { .symbol_name = "iterate_dir", .pre_handler = coverage_pre_handler },
		.name = "iterate_dir",
	},
	{
		.probe = { .symbol_name = "fuse_readdir", .pre_handler = coverage_pre_handler },
		.name = "fuse_readdir",
	},
	{
		.probe = { .symbol_name = "fuse_dentry_revalidate", .pre_handler = coverage_pre_handler },
		.name = "fuse_dentry_revalidate",
	},
	{
		.probe = { .symbol_name = "do_filp_open", .pre_handler = coverage_pre_handler },
		.name = "do_filp_open",
	},
};

static struct pathguard_vfs_coverage_status coverage_status = {
	.abi_version = PATHGUARD_VFS_COVERAGE_ABI_VERSION,
	.size = sizeof(struct pathguard_vfs_coverage_status),
	.probe_count = ARRAY_SIZE(coverage_entries),
};

static long coverage_ioctl(struct file *file, unsigned int command,
			   unsigned long argument)
{
	struct pathguard_vfs_coverage_status snapshot;
	unsigned int index;

	(void)file;
	if (command != PATHGUARD_VFS_COVERAGE_IOC_STATUS)
		return -ENOTTY;
	snapshot = coverage_status;
	for (index = 0; index < ARRAY_SIZE(coverage_entries); ++index) {
		snapshot.counters[index].hits = atomic64_read(&coverage_entries[index].hits);
		snapshot.counters[index].nmissed = READ_ONCE(coverage_entries[index].probe.nmissed);
		snapshot.counters[index].registered = coverage_entries[index].probe.addr != NULL;
		strscpy(snapshot.counters[index].name, coverage_entries[index].name,
			sizeof(snapshot.counters[index].name));
	}
	return copy_to_user((void __user *)argument, &snapshot, sizeof(snapshot)) ?
		-EFAULT : 0;
}

static const struct file_operations coverage_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = coverage_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = coverage_ioctl,
#endif
};

static struct miscdevice coverage_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "pathguard_vfs_coverage_probe",
	.fops = &coverage_fops,
	.mode = 0600,
};

static void unregister_entries(unsigned int count)
{
	while (count > 0) {
		--count;
		if (coverage_entries[count].probe.addr != NULL)
			unregister_kprobe(&coverage_entries[count].probe);
	}
}

static int __init pathguard_coverage_init(void)
{
	unsigned int index;
	int ret;

	if (strcmp(init_utsname()->release, PATHGUARD_VFS_COVERAGE_EXPECTED_RELEASE) != 0)
		return -ENODEV;
	strscpy(coverage_status.kernel_release, init_utsname()->release,
		sizeof(coverage_status.kernel_release));
	for (index = 0; index < ARRAY_SIZE(coverage_entries); ++index) {
		ret = register_kprobe(&coverage_entries[index].probe);
		if (ret) {
			coverage_status.last_error = ret;
			unregister_entries(index);
			pr_err("pathguard_vfs_coverage_probe: register %s failed: %d\n",
			       coverage_entries[index].name, ret);
			return ret;
		}
		++coverage_status.registered_count;
	}
	coverage_status.state = 1;
	coverage_status.last_error = 0;
	ret = misc_register(&coverage_device);
	if (ret) {
		unregister_entries(ARRAY_SIZE(coverage_entries));
		coverage_status.registered_count = 0;
		return ret;
	}
	pr_info("pathguard_vfs_coverage_probe: registered=%u release=%s\n",
		coverage_status.registered_count, coverage_status.kernel_release);
	return 0;
}

static void __exit pathguard_coverage_exit(void)
{
	misc_deregister(&coverage_device);
	unregister_entries(ARRAY_SIZE(coverage_entries));
}

module_init(pathguard_coverage_init);
module_exit(pathguard_coverage_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("PathGuard");
MODULE_DESCRIPTION("PathGuard read-only VFS call-path coverage probe");
MODULE_VERSION("0.1.0-prototype");
