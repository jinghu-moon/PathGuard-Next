// SPDX-License-Identifier: Apache-2.0
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "pathguard_vfs_preflight_uapi.h"

int main(int argc, char **argv)
{
	struct pathguard_vfs_preflight_request request = {
		.abi_version = PATHGUARD_VFS_PREFLIGHT_ABI_VERSION,
		.size = sizeof(request),
	};
	struct pathguard_vfs_preflight_status status;
	int fd;

	if (argc != 2 || strlen(argv[1]) >= sizeof(request.parent)) {
		fprintf(stderr, "usage: %s /absolute/parent\n", argv[0]);
		return 2;
	}
	fprintf(stderr, "request_size=%zu path_size=%zu ioctl_scan=0x%lx\n",
		sizeof(request), sizeof(request.parent),
		(unsigned long)PATHGUARD_VFS_PREFLIGHT_IOC_SCAN);
	strcpy(request.parent, argv[1]);
	fd = open("/dev/pathguard_vfs_preflight", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	if (ioctl(fd, PATHGUARD_VFS_PREFLIGHT_IOC_SCAN, &request) < 0) {
		perror("scan");
		close(fd);
		return 1;
	}
	if (ioctl(fd, PATHGUARD_VFS_PREFLIGHT_IOC_STATUS, &status) < 0) {
		perror("status");
		close(fd);
		return 1;
	}
	printf("abi_version=%" PRIu32 " size=%" PRIu32 " state=%" PRIu32
	       " last_error=%" PRId32 " operation_mask=0x%016" PRIx64
	       " parent_inode=%" PRIu64 " mode=0%o dev=%" PRIu32 ":%" PRIu32
	       " mount=0x%016" PRIx64 " sb=0x%016" PRIx64
	       " inode=0x%016" PRIx64 " dentry=0x%016" PRIx64
	       " i_op=0x%016" PRIx64 " f_op=0x%016" PRIx64
	       " d_op=0x%016" PRIx64 " fs=%s parent=%s release=%s\n", status.abi_version, status.size,
	       status.state, status.last_error, (uint64_t)status.operation_mask,
	       (uint64_t)status.parent_inode, status.parent_mode, status.parent_dev_major,
	       status.parent_dev_minor, (uint64_t)status.mount_address,
	       (uint64_t)status.superblock_address, (uint64_t)status.inode_address,
	       (uint64_t)status.dentry_address, (uint64_t)status.i_op_address,
	       (uint64_t)status.f_op_address, (uint64_t)status.d_op_address,
	       status.filesystem, status.parent,
	       status.kernel_release);
	close(fd);
	return 0;
}
