#include <string>

#include "pathguard/topology.h"
#include "test_assert.h"

int main() {
    const std::string mountinfo =
        "100 1 0:117 /0 /storage/emulated/0 rw,nosuid,nodev - sdcardfs /data/media rw\n"
        "100 1 0:117 /0 /sdcard rw,nosuid,nodev - sdcardfs /data/media rw\n"
        "101 1 0:118 /1 /storage/emulated/10 rw,nosuid,nodev - sdcardfs /data/media rw\n"
        "102 1 0:119 / /mnt/user/0/emulated/primary rw - sdcardfs /data/media rw\n";
    pathguard::StorageTopology topology;
    std::string error;
    assert(pathguard::ParseMountInfo(mountinfo, &topology, &error));
    assert(topology.supported);
    assert(topology.mounts.size() == 2);
    assert(topology.mounts[0].user_id == 0);
    assert(topology.mounts[0].visible_root == "/storage/emulated/0");
    assert(topology.mounts[0].backend_root == "/data/media/0");
    assert(topology.mounts[0].aliases.size() == 1);
    assert(topology.mounts[0].aliases[0].path == "/sdcard");

    std::string resolved;
    assert(pathguard::ResolveVisibleStoragePath(
        topology, 0, "DCIM/Camera", &resolved, &error));
    assert(resolved == "/storage/emulated/0/DCIM/Camera");
    assert(pathguard::ResolveBackendStoragePath(
        topology, 0, "PathGuard/com.example/DCIM", &resolved, &error));
    assert(resolved == "/data/media/0/PathGuard/com.example/DCIM");
    assert(pathguard::ResolveVisibleStoragePath(
        topology, 10, "Download", &resolved, &error));
    assert(resolved == "/storage/emulated/10/Download");
    assert(!pathguard::ResolveBackendStoragePath(
        topology, 0, "../escape", &resolved, &error));
    assert(!pathguard::ResolveBackendStoragePath(
        topology, 0, "/absolute", &resolved, &error));
    assert(!pathguard::ResolveBackendStoragePath(
        topology, 99, "Download", &resolved, &error));

    pathguard::StorageTopology unsupported;
    assert(!pathguard::ParseMountInfo(
        "100 1 0:117 /0 /storage/emulated/0 rw - ext4 /dev/block rw\n",
        &unsupported, &error));
    assert(!pathguard::ParseMountInfo(
        "100 1 0:117 /0 /storage/emulated/0 rw - sdcardfs /data/media\\040broken rw\n",
        &unsupported, &error));

    const std::string conflicting =
        "100 1 0:117 /0 /storage/emulated/0 rw - sdcardfs /data/media rw\n"
        "101 1 0:118 /0 /storage/emulated/0 rw - sdcardfs /data/media rw\n";
    assert(!pathguard::ParseMountInfo(conflicting, &unsupported, &error));

    const std::string alioth_mountinfo =
        "74441 47 0:118 / /mnt/runtime/default/emulated rw shared:46 - sdcardfs /data/media rw\n"
        "74570 47 0:119 / /mnt/user/0/emulated rw shared:47 - fuse /dev/fuse rw\n"
        "74569 117 0:119 / /storage/emulated rw shared:47 - fuse /dev/fuse rw\n"
        "74694 47 0:118 / /mnt/pass_through/0/emulated rw shared:46 - sdcardfs /data/media rw\n";
    assert(pathguard::ParseMountInfo(alioth_mountinfo, &topology, &error));
    assert(topology.mounts.size() == 1);
    assert(topology.mounts[0].mount_id == 74569);
    assert(topology.mounts[0].visible_root == "/storage/emulated/0");
    assert(topology.mounts[0].backend_root == "/data/media/0");
    assert(topology.mounts[0].filesystem_type == "fuse");
    assert(topology.mounts[0].aliases.size() == 1);
    assert(topology.mounts[0].aliases[0].mount_id == 74570);
    assert(topology.mounts[0].aliases[0].path == "/mnt/user/0/emulated");

    const std::string missing_backend =
        "74570 47 0:119 / /mnt/user/0/emulated rw - fuse /dev/fuse rw\n"
        "74569 117 0:119 / /storage/emulated rw - fuse /dev/fuse rw\n";
    assert(!pathguard::ParseMountInfo(missing_backend, &unsupported, &error));
    assert(unsupported.unsupported_reason == "missing backend evidence for user 0");
    return 0;
}
