// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read-only operation-table preflight. It resolves one parent path and
 * reports callback presence without exposing addresses or mutating VFS state.
 */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>

#include "pathguard_vfs_preflight_uapi.h"

#ifndef PATHGUARD_VFS_PREFLIGHT_EXPECTED_RELEASE
#define PATHGUARD_VFS_PREFLIGHT_EXPECTED_RELEASE \
	"6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"
#endif

static DEFINE_MUTEX(preflight_lock);
static struct pathguard_vfs_preflight_status preflight_status = {
	.abi_version = PATHGUARD_VFS_PREFLIGHT_ABI_VERSION,
	.size = sizeof(struct pathguard_vfs_preflight_status),
	.state = PATHGUARD_VFS_PREFLIGHT_STATE_EMPTY,
};

static u64 operation_mask(const struct inode *inode, const struct dentry *dentry)
{
	const struct inode_operations *iop = inode->i_op;
	const struct file_operations *fop = inode->i_fop;
	const struct dentry_operations *dop = dentry->d_op;
	u64 mask = 0;

	if (iop && iop->lookup) mask |= PATHGUARD_VFS_PREFLIGHT_OP_LOOKUP;
	if (iop && iop->atomic_open) mask |= PATHGUARD_VFS_PREFLIGHT_OP_ATOMIC_OPEN;
	if (fop && fop->iterate_shared) mask |= PATHGUARD_VFS_PREFLIGHT_OP_READDIR;
	if (iop && iop->create) mask |= PATHGUARD_VFS_PREFLIGHT_OP_CREATE;
	if (iop && iop->mkdir) mask |= PATHGUARD_VFS_PREFLIGHT_OP_MKDIR;
	if (iop && iop->mknod) mask |= PATHGUARD_VFS_PREFLIGHT_OP_MKNOD;
	if (iop && iop->symlink) mask |= PATHGUARD_VFS_PREFLIGHT_OP_SYMLINK;
	if (iop && iop->unlink) mask |= PATHGUARD_VFS_PREFLIGHT_OP_UNLINK;
	if (iop && iop->rmdir) mask |= PATHGUARD_VFS_PREFLIGHT_OP_RMDIR;
	if (iop && iop->link) mask |= PATHGUARD_VFS_PREFLIGHT_OP_LINK;
	if (iop && iop->rename) mask |= PATHGUARD_VFS_PREFLIGHT_OP_RENAME;
	if (dop && dop->d_revalidate) mask |= PATHGUARD_VFS_PREFLIGHT_OP_REVALIDATE;
	return mask;
}

static long preflight_ioctl(struct file *file, unsigned int command,
			    unsigned long argument)
{
	struct pathguard_vfs_preflight_request request;
	struct pathguard_vfs_preflight_status status;
	struct path path;
	struct inode *inode;
	int ret;

	(void)file;
	mutex_lock(&preflight_lock);
	switch (command) {
	case PATHGUARD_VFS_PREFLIGHT_IOC_SCAN:
		if (copy_from_user(&request, (void __user *)argument, sizeof(request))) {
			ret = -EFAULT;
			break;
		}
		if (request.abi_version != PATHGUARD_VFS_PREFLIGHT_ABI_VERSION ||
		    request.size != sizeof(request) || request.parent[0] != '/' ||
		    strnlen(request.parent, sizeof(request.parent)) >= sizeof(request.parent)) {
			ret = -EINVAL;
			break;
		}
		ret = kern_path(request.parent, LOOKUP_FOLLOW, &path);
		if (ret)
			break;
		inode = d_backing_inode(path.dentry);
		if (!inode || !S_ISDIR(inode->i_mode)) {
			path_put(&path);
			ret = -ENOTDIR;
			break;
		}
		memset(&preflight_status, 0, sizeof(preflight_status));
		preflight_status.abi_version = PATHGUARD_VFS_PREFLIGHT_ABI_VERSION;
		preflight_status.size = sizeof(preflight_status);
		strscpy(preflight_status.kernel_release, init_utsname()->release,
			sizeof(preflight_status.kernel_release));
		preflight_status.state = PATHGUARD_VFS_PREFLIGHT_STATE_READY;
		preflight_status.last_error = 0;
		preflight_status.operation_mask = operation_mask(inode, path.dentry);
		preflight_status.parent_inode = inode->i_ino;
		preflight_status.parent_mode = inode->i_mode;
		preflight_status.parent_dev_major = MAJOR(inode->i_sb->s_dev);
		preflight_status.parent_dev_minor = MINOR(inode->i_sb->s_dev);
		strscpy(preflight_status.filesystem, inode->i_sb->s_type->name,
			sizeof(preflight_status.filesystem));
		strscpy(preflight_status.parent, request.parent,
			sizeof(preflight_status.parent));
		path_put(&path);
		break;
	case PATHGUARD_VFS_PREFLIGHT_IOC_STATUS:
		status = preflight_status;
		ret = copy_to_user((void __user *)argument, &status, sizeof(status)) ?
			-EFAULT : 0;
		break;
	case PATHGUARD_VFS_PREFLIGHT_IOC_CLEAR:
		memset(&preflight_status, 0, sizeof(preflight_status));
		preflight_status.abi_version = PATHGUARD_VFS_PREFLIGHT_ABI_VERSION;
		preflight_status.size = sizeof(preflight_status);
		preflight_status.state = PATHGUARD_VFS_PREFLIGHT_STATE_EMPTY;
		ret = 0;
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	mutex_unlock(&preflight_lock);
	return ret;
}

static const struct file_operations preflight_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = preflight_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = preflight_ioctl,
#endif
};

static struct miscdevice preflight_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "pathguard_vfs_preflight",
	.fops = &preflight_fops,
	.mode = 0600,
};

static int __init pathguard_preflight_init(void)
{
	if (strcmp(init_utsname()->release, PATHGUARD_VFS_PREFLIGHT_EXPECTED_RELEASE) != 0)
		return -ENODEV;
	strscpy(preflight_status.kernel_release, init_utsname()->release,
		sizeof(preflight_status.kernel_release));
	return misc_register(&preflight_device);
}

static void __exit pathguard_preflight_exit(void)
{
	misc_deregister(&preflight_device);
}

module_init(pathguard_preflight_init);
module_exit(pathguard_preflight_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("PathGuard");
MODULE_DESCRIPTION("PathGuard read-only VFS operation-table preflight");
MODULE_VERSION("0.1.0-prototype");
