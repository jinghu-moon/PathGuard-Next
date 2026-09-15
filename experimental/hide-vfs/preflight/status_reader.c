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
	       " fs=%s parent=%s release=%s\n", status.abi_version, status.size,
	       status.state, status.last_error, status.operation_mask,
	       status.parent_inode, status.parent_mode, status.parent_dev_major,
	       status.parent_dev_minor, status.filesystem, status.parent,
	       status.kernel_release);
	close(fd);
	return 0;
}
