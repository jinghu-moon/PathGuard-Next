/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Read-only KernelPatch capability probe.
 *
 * This KPM deliberately does not install a hook, call a resolved kernel
 * function, or change any VFS state.  It only asks KernelPatch's exported
 * kallsyms resolver whether the fixed candidate symbols exist on the running
 * kernel.  A successful result is therefore a capability observation, not a
 * hide backend admission.
 */
#include <stdint.h>

#include <compiler.h>
#include <kallsyms.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>

#define PATHGUARD_KPM_CANDIDATE_COUNT 18U

KPM_NAME("pathguard-kpm-cap-probe");
KPM_VERSION("0.1.0-prototype");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("PathGuard");
KPM_DESCRIPTION("Read-only KernelPatch symbol capability probe");

struct capability_symbol {
	const char *name;
	unsigned long bit;
	unsigned long address;
};

static struct capability_symbol symbols[] = {
	{ "path_lookupat", 1UL << 0, 0 },
	{ "filename_lookup", 1UL << 1, 0 },
	{ "lookup_fast", 1UL << 2, 0 },
	{ "lookup_slow", 1UL << 3, 0 },
	{ "do_filp_open", 1UL << 4, 0 },
	{ "iterate_dir", 1UL << 5, 0 },
	{ "vfs_create", 1UL << 6, 0 },
	{ "vfs_mkdir", 1UL << 7, 0 },
	{ "vfs_mknod", 1UL << 8, 0 },
	{ "vfs_symlink", 1UL << 9, 0 },
	{ "vfs_link", 1UL << 10, 0 },
	{ "vfs_unlink", 1UL << 11, 0 },
	{ "vfs_rmdir", 1UL << 12, 0 },
	{ "vfs_rename", 1UL << 13, 0 },
	{ "vfs_getattr", 1UL << 14, 0 },
	{ "fuse_lookup", 1UL << 15, 0 },
	{ "fuse_atomic_open", 1UL << 16, 0 },
	{ "fuse_readdir", 1UL << 17, 0 },
};

static unsigned long available_mask;
static unsigned int found_count;
static int probe_error;

static unsigned int append_text(char *buffer, unsigned int offset,
				unsigned int limit, const char *text)
{
	unsigned int index = 0;

	while (text[index] != '\0' && offset + 1U < limit) {
		buffer[offset++] = text[index++];
	}
	buffer[offset] = '\0';
	return offset;
}

static unsigned int append_unsigned(char *buffer, unsigned int offset,
				    unsigned int limit, unsigned int value)
{
	char digits[10];
	unsigned int count = 0;

	do {
		digits[count++] = (char)('0' + (value % 10U));
		value /= 10U;
	} while (value != 0U && count < sizeof(digits));

	while (count > 0U && offset + 1U < limit) {
		buffer[offset++] = digits[--count];
	}
	buffer[offset] = '\0';
	return offset;
}

static unsigned int append_hex(char *buffer, unsigned int offset,
				       unsigned int limit, unsigned long value)
{
	static const char digits[] = "0123456789abcdef";
	char reversed[sizeof(unsigned long) * 2U];
	unsigned int count = 0;

	do {
		reversed[count++] = digits[value & 0xfUL];
		value >>= 4;
	} while (value != 0UL && count < sizeof(reversed));

	while (count > 0U && offset + 1U < limit) {
		buffer[offset++] = reversed[--count];
	}
	buffer[offset] = '\0';
	return offset;
}

static unsigned int build_status(char *buffer, unsigned int limit)
{
	unsigned int offset = 0;
	unsigned int index;

	offset = append_text(buffer, offset, limit,
				     "state=capability_only;found=");
	offset = append_unsigned(buffer, offset, limit, found_count);
	offset = append_text(buffer, offset, limit, "/");
	offset = append_unsigned(buffer, offset, limit,
					 PATHGUARD_KPM_CANDIDATE_COUNT);
	offset = append_text(buffer, offset, limit, ";mask=0x");
	offset = append_hex(buffer, offset, limit, available_mask);
	offset = append_text(buffer, offset, limit, ";error=");
	offset = append_unsigned(buffer, offset, limit,
					 (unsigned int)(probe_error < 0 ? -probe_error : probe_error));
	offset = append_text(buffer, offset, limit, ";symbols=");

	for (index = 0; index < PATHGUARD_KPM_CANDIDATE_COUNT; ++index) {
		if (index != 0U)
			offset = append_text(buffer, offset, limit, ",");
		offset = append_text(buffer, offset, limit, symbols[index].name);
		offset = append_text(buffer, offset, limit,
					 symbols[index].address != 0UL ? "=1" : "=0");
	}
	offset = append_text(buffer, offset, limit, "\n");
	return offset;
}

static long capability_probe_init(const char *args, const char *event,
					 void *__user reserved)
{
	unsigned int index;

	(void)args;
	(void)event;
	(void)reserved;
	available_mask = 0UL;
	found_count = 0U;
	probe_error = 0;

	if (kallsyms_lookup_name == 0) {
		probe_error = -1;
		pr_err("pathguard-kpm-cap-probe: kallsyms resolver unavailable\n");
		return 0;
	}

	for (index = 0; index < PATHGUARD_KPM_CANDIDATE_COUNT; ++index) {
		/* Address is observed only long enough to classify symbol presence. */
		symbols[index].address = kallsyms_lookup_name(symbols[index].name);
		if (symbols[index].address != 0UL) {
			available_mask |= symbols[index].bit;
			++found_count;
		}
	}

	pr_info("pathguard-kpm-cap-probe: found=%u/%u mask=0x%lx\n",
		found_count, PATHGUARD_KPM_CANDIDATE_COUNT, available_mask);
	return 0;
}

static long capability_probe_control0(const char *args, char *__user out_msg,
					      int outlen)
{
	char response[768];
	unsigned int length;

	if (args == 0 || strcmp(args, "status") != 0 || outlen <= 0)
		return -22;

	length = build_status(response, sizeof(response));
	if ((unsigned int)outlen < length)
		length = (unsigned int)outlen;
	return compat_copy_to_user(out_msg, response, (int)length) ? -14 : (long)length;
}

static long capability_probe_exit(void *__user reserved)
{
	(void)reserved;
	available_mask = 0UL;
	found_count = 0U;
	probe_error = 0;
	pr_info("pathguard-kpm-cap-probe: unloaded\n");
	return 0;
}

KPM_INIT(capability_probe_init);
KPM_CTL0(capability_probe_control0);
KPM_EXIT(capability_probe_exit);
