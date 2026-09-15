// SPDX-License-Identifier: Apache-2.0
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "pathguard_vfs_trace_probe_uapi.h"

int main(void)
{
	struct pathguard_vfs_trace_status status;
	int fd;
	unsigned int index;

	fd = open("/dev/pathguard_vfs_trace_probe", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	if (ioctl(fd, PATHGUARD_VFS_TRACE_IOC_STATUS, &status) < 0) {
		perror("ioctl");
		close(fd);
		return 1;
	}
	printf("abi_version=%" PRIu32 " size=%" PRIu32 " state=%" PRIu32
	       " last_error=%" PRId32 " probes=%" PRIu32 " registered=%" PRIu32
	       " release=%s\n",
	       status.abi_version, status.size, status.state, status.last_error,
	       status.probe_count, status.registered_count, status.kernel_release);
	for (index = 0; index < status.probe_count &&
		 index < PATHGUARD_VFS_TRACE_MAX_PROBES; ++index)
		printf("probe[%u]=%s registered=%" PRIu32 " hits=%" PRIu64
		       " nmissed=%" PRIu64 "\n",
		       index, status.counters[index].name,
		       status.counters[index].registered,
		       status.counters[index].hits,
		       status.counters[index].nmissed);
	close(fd);
	return 0;
}
