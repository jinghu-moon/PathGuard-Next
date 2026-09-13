// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal module-loader probe. It deliberately installs no hooks, devices,
 * callbacks, or policy state.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/utsname.h>

static volatile char pathguard_probe_release_byte;

static int __init pathguard_probe_init(void)
{
    pathguard_probe_release_byte = init_utsname()->release[0];
    return 0;
}

static void __exit pathguard_probe_exit(void)
{
}

module_init(pathguard_probe_init);
module_exit(pathguard_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PathGuard");
MODULE_DESCRIPTION("PathGuard android16-6.12 LKM loader probe");
MODULE_VERSION("0.1.0");
