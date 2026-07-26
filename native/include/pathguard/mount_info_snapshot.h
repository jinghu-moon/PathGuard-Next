#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

namespace pathguard {

struct MountPathIdentity {
    uint64_t mount_id = 0;
    uint64_t parent_mount_id = 0;
    dev_t device = 0;
    char root[PATH_MAX]{};
    char mountpoint[PATH_MAX]{};
    char filesystem[64]{};
};

enum MountPropagationFlag : uint8_t {
    kMountPropagationNone = 0,
    kMountPropagationShared = 1u << 0,
    kMountPropagationMaster = 1u << 1,
    kMountPropagationUnbindable = 1u << 2,
};

struct MountInfoSnapshotEntry {
    uint64_t mount_id = 0;
    uint64_t parent_mount_id = 0;
    dev_t device = 0;
    uint32_t root_offset = 0;
    uint32_t root_length = 0;
    uint32_t mountpoint_offset = 0;
    uint32_t mountpoint_length = 0;
    uint32_t filesystem_offset = 0;
    uint32_t filesystem_length = 0;
    uint8_t propagation = kMountPropagationNone;
};

// Owns one bounded, namespace-specific materialization of /proc/self/mountinfo.
// The backing mapping intentionally lives off-stack because Android mount tables
// can contain thousands of rows.
struct MountInfoSnapshot {
    void* mapping = nullptr;
    size_t mapping_size = 0;
    char* text = nullptr;
    size_t text_size = 0;
    MountInfoSnapshotEntry* entries = nullptr;
    size_t entry_count = 0;
    dev_t namespace_device = 0;
    ino_t namespace_inode = 0;
    uint64_t read_ns = 0;
    uint64_t parse_ns = 0;
};

int CaptureMountInfoSnapshot(MountInfoSnapshot* snapshot);
void DestroyMountInfoSnapshot(MountInfoSnapshot* snapshot);

int FindMountInfoPath(const MountInfoSnapshot& snapshot,
                      const char* absolute_path, bool exact_mountpoint,
                      MountPathIdentity* identity);
int MountInfoSnapshotRequiresPrivate(const MountInfoSnapshot& snapshot,
                                     const char* mountpoint,
                                     bool* requires_private);
bool MountInfoSnapshotMatchesCurrentNamespace(
    const MountInfoSnapshot& snapshot);

}  // namespace pathguard
