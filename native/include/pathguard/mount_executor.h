#pragma once

#include <limits.h>
#include <stdint.h>
#include <sys/types.h>

#include "pathguard/mount_backend.h"

namespace pathguard {

struct PinnedIdentity {
    int fd = -1;
    dev_t device = 0;
    ino_t inode = 0;
};

struct CanonicalLocator {
    char path[PATH_MAX]{};
};

struct AppliedMount {
    MountBackendKind backend = MountBackendKind::kUnsupported;
    CanonicalLocator target;
    uint64_t mount_id = 0;
};

struct MountBackendProbeStep {
    int before_error = 0;
    uint64_t before_count = 0;
    int apply_error = 0;
    int stat_error = 0;
    uint8_t identity_match = 0;
    int after_error = 0;
    uint64_t after_count = 0;
    uint64_t mounted_id = 0;
    int verify_error = 0;
    int rollback_error = 0;
    int remaining_error = 0;
    uint64_t remaining_id = 0;
    uint8_t success = 0;
};

struct MountBackendProbe {
    MountBackendCapabilities capabilities;
    int error = 0;
    uint32_t uid = 0;
    uint32_t euid = 0;
    uint64_t cap_effective = 0;
    uint64_t cap_bounding = 0;
    uint64_t namespace_before = 0;
    uint64_t namespace_after = 0;
    int unshare_error = 0;
    int private_error = 0;
    char selinux_context[128]{};
    MountBackendProbeStep open_tree;
    MountBackendProbeStep proc_fd;
    MountBackendProbeStep legacy_string;
};

struct MountApplyTiming {
    uint64_t verify_pinned_ns = 0;
    uint64_t before_scan_ns = 0;
    uint64_t apply_raw_ns = 0;
    uint64_t verify_ns = 0;
    // Fine-grained breakdown of the verify step to locate the cold-mountinfo
    // cost: stat(target) vs mountinfo read (kernel seq_file generation) vs
    // parse loop.
    uint64_t verify_stat_ns = 0;
    uint64_t verify_mountinfo_read_ns = 0;
    uint64_t verify_mountinfo_parse_ns = 0;
};

int PinDirectory(const char* absolute_path, PinnedIdentity* identity);
void ClosePinnedIdentity(PinnedIdentity* identity);
int VerifyPinnedDirectory(const char* absolute_path,
                          const PinnedIdentity& identity);

MountBackendProbe ProbeDirectoryMountBackends(const char* source_path,
                                              const char* target_path);
int ApplyDirectoryMount(MountBackendKind backend,
                        const PinnedIdentity& source,
                        const PinnedIdentity& target,
                        const CanonicalLocator& source_locator,
                        const CanonicalLocator& target_locator,
                        AppliedMount* applied,
                        MountApplyTiming* timing = nullptr);
int RollbackDirectoryMount(const AppliedMount& applied);

int BindMountDirectoryFds(int source_fd, int target_fd);
int MoveMountDirectoryFds(int source_fd, int target_fd);
int UnmountDirectoryFd(int target_fd);

}  // namespace pathguard
