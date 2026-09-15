// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read-only VFS capability probe. It records the presence of exported
 * android16-6.12 symbols and never changes VFS operation tables or policy.
 */
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>

#include "pathguard_vfs_cap_probe_uapi.h"

#ifndef PATHGUARD_VFS_CAP_EXPECTED_RELEASE
#define PATHGUARD_VFS_CAP_EXPECTED_RELEASE \
    "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"
#endif

extern struct dentry *lookup_one_len(const char *name, struct dentry *base,
                                     int len);

static struct pathguard_vfs_cap_status cap_status = {
    .abi_version = PATHGUARD_VFS_CAP_ABI_VERSION,
    .size = sizeof(struct pathguard_vfs_cap_status),
    .state = PATHGUARD_VFS_CAP_STATE_UNSUPPORTED,
    .required_ops = PATHGUARD_VFS_CAP_REQUIRED_OPS,
};
static volatile unsigned long cap_symbol_sink;

static long cap_ioctl(struct file *file, unsigned int command,
                      unsigned long argument)
{
    (void)file;
    if (command != PATHGUARD_VFS_CAP_IOC_STATUS)
        return -ENOTTY;
    return copy_to_user((void __user *)argument, &cap_status,
                        sizeof(cap_status)) ? -EFAULT : 0;
}

static const struct file_operations cap_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = cap_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = cap_ioctl,
#endif
};

static struct miscdevice cap_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "pathguard_vfs_cap_probe",
    .fops = &cap_fops,
    .mode = 0600,
};

static u64 detect_available_ops(void)
{
    u64 available = 0;

    cap_symbol_sink = (unsigned long)&lookup_one_len;
    available |= PATHGUARD_VFS_CAP_OP_LOOKUP;
    cap_symbol_sink = (unsigned long)&vfs_create;
    available |= PATHGUARD_VFS_CAP_OP_CREATE;
    cap_symbol_sink = (unsigned long)&vfs_mkdir;
    available |= PATHGUARD_VFS_CAP_OP_MKDIR;
    cap_symbol_sink = (unsigned long)&vfs_mknod;
    available |= PATHGUARD_VFS_CAP_OP_MKNOD;
    cap_symbol_sink = (unsigned long)&vfs_symlink;
    available |= PATHGUARD_VFS_CAP_OP_SYMLINK;
    cap_symbol_sink = (unsigned long)&vfs_unlink;
    available |= PATHGUARD_VFS_CAP_OP_UNLINK;
    cap_symbol_sink = (unsigned long)&vfs_rmdir;
    available |= PATHGUARD_VFS_CAP_OP_RMDIR;
    cap_symbol_sink = (unsigned long)&vfs_link;
    available |= PATHGUARD_VFS_CAP_OP_LINK;
    cap_symbol_sink = (unsigned long)&vfs_rename;
    available |= PATHGUARD_VFS_CAP_OP_RENAME;
    cap_symbol_sink = (unsigned long)&register_kprobe;
    cap_symbol_sink = (unsigned long)&unregister_kprobe;
    available |= PATHGUARD_VFS_CAP_OP_KPROBE;
    return available;
}

static int __init cap_init(void)
{
    int ret;

    if (strcmp(init_utsname()->release, PATHGUARD_VFS_CAP_EXPECTED_RELEASE) != 0)
        return -ENODEV;

    strscpy(cap_status.kernel_release, init_utsname()->release,
            sizeof(cap_status.kernel_release));
    cap_status.available_ops = detect_available_ops();
    cap_status.last_error =
        cap_status.available_ops == PATHGUARD_VFS_CAP_REQUIRED_OPS ? 0 : -EOPNOTSUPP;
    cap_status.state = cap_status.last_error == 0
                           ? PATHGUARD_VFS_CAP_STATE_READY
                           : PATHGUARD_VFS_CAP_STATE_UNSUPPORTED;
    ret = misc_register(&cap_device);
    if (ret)
        return ret;
    pr_info("pathguard_vfs_cap_probe: available_ops=0x%llx required_ops=0x%llx state=%u\n",
            cap_status.available_ops, cap_status.required_ops, cap_status.state);
    return 0;
}

static void __exit cap_exit(void)
{
    misc_deregister(&cap_device);
}

module_init(cap_init);
module_exit(cap_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("PathGuard");
MODULE_DESCRIPTION("PathGuard read-only VFS capability probe");
MODULE_VERSION("0.1.0-prototype");
