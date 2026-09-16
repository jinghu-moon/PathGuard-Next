// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "pathguard_hide1_uapi.h"

static int parse_u32(const char *text, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno || *text == '\0' || *end != '\0' || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

static int parse_i32(const char *text, int32_t *value)
{
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno || *text == '\0' || *end != '\0' || parsed < INT32_MIN ||
        parsed > INT32_MAX)
        return -1;
    *value = (int32_t)parsed;
    return 0;
}

static int parse_u64(const char *text, uint64_t *value)
{
    char *end;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || *text == '\0' || *end != '\0')
        return -1;
    *value = (uint64_t)parsed;
    return 0;
}

static int print_status(int fd)
{
    struct pathguard_hide1_status status;

    if (ioctl(fd, PATHGUARD_HIDE1_IOC_STATUS, &status) < 0) {
        perror("status");
        return 1;
    }
    printf("abi_version=%" PRIu32 " size=%" PRIu32 " state=%" PRIu32
           " last_error=%" PRId32 " target_uid=%" PRIu32
           " target_pid=%" PRId32
           " target_mnt_ns=%" PRIu64 " generation=%" PRIu64
           " operation_mask=0x%016" PRIx64 " parent_inode=%" PRIu64
           " lookup=%" PRIu64 "/%" PRIu64
           " atomic_open=%" PRIu64 "/%" PRIu64
           " readdir=%" PRIu64 "/%" PRIu64
           " revalidate=%" PRIu64 "/%" PRIu64
           " dentry_install=%" PRIu64 "/%" PRIu64 "/%" PRIu64
           " release=%s\n",
           status.abi_version, status.size, status.state, status.last_error,
           status.target_uid, status.target_pid,
           (uint64_t)status.target_mnt_ns,
           (uint64_t)status.generation, (uint64_t)status.operation_mask,
           (uint64_t)status.parent_inode,
           (uint64_t)status.lookup_calls, (uint64_t)status.lookup_hidden,
           (uint64_t)status.atomic_open_calls,
           (uint64_t)status.atomic_open_hidden,
           (uint64_t)status.readdir_calls,
           (uint64_t)status.readdir_filtered,
           (uint64_t)status.d_revalidate_calls,
           (uint64_t)status.d_revalidate_hidden,
           (uint64_t)status.dentry_install_calls,
           (uint64_t)status.dentry_install_success,
           (uint64_t)status.dentry_install_failures,
           status.kernel_release);
    return 0;
}

static int install_rule(int fd, int argc, char **argv)
{
    struct pathguard_hide1_rule rule = {
        .abi_version = PATHGUARD_HIDE1_ABI_VERSION,
        .size = sizeof(rule),
    };
    uint64_t generation;

    if (argc != 7 || parse_u32(argv[2], &rule.target_uid) ||
        parse_i32(argv[3], &rule.target_pid) ||
        parse_u64(argv[4], &generation) ||
        strlen(argv[5]) >= sizeof(rule.parent) ||
        strlen(argv[6]) >= sizeof(rule.basename)) {
        return 2;
    }
    rule.expected_generation = generation;
    strcpy(rule.parent, argv[5]);
    strcpy(rule.basename, argv[6]);
    if (ioctl(fd, PATHGUARD_HIDE1_IOC_INSTALL, &rule) < 0) {
        perror("install");
        return 1;
    }
    return print_status(fd);
}

int main(int argc, char **argv)
{
    uint64_t generation;
    int fd;
    int result = 2;

    if (argc < 2) {
        fprintf(stderr, "usage: %s status|install|enable|disable|clear ...\n",
                argv[0]);
        return 2;
    }
    fd = open("/dev/pathguard_hide1", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    if (strcmp(argv[1], "status") == 0 && argc == 2) {
        result = print_status(fd);
    } else if (strcmp(argv[1], "install") == 0) {
        result = install_rule(fd, argc, argv);
    } else if (strcmp(argv[1], "enable") == 0 && argc == 3 &&
               parse_u64(argv[2], &generation) == 0) {
        if (ioctl(fd, PATHGUARD_HIDE1_IOC_ENABLE, &generation) < 0) {
            perror("enable");
            result = 1;
        } else {
            result = print_status(fd);
        }
    } else if (strcmp(argv[1], "disable") == 0 && argc == 2) {
        if (ioctl(fd, PATHGUARD_HIDE1_IOC_DISABLE) < 0) {
            perror("disable");
            result = 1;
        } else {
            result = print_status(fd);
        }
    } else if (strcmp(argv[1], "clear") == 0 && argc == 2) {
        if (ioctl(fd, PATHGUARD_HIDE1_IOC_CLEAR) < 0) {
            perror("clear");
            result = 1;
        } else {
            result = print_status(fd);
        }
    }
    if (result == 2)
        fprintf(stderr, "invalid command or arguments\n");
    close(fd);
    return result;
}
