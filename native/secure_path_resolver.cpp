#include "pathguard/secure_path_resolver.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pathguard/directory_resolver.h"

namespace pathguard {
namespace {

bool StorageRelative(const char* absolute, char* root, size_t root_capacity,
                     const char** relative) noexcept {
    if (absolute == nullptr || root == nullptr || relative == nullptr) return false;
    const char* user = nullptr;
    const char* separator = nullptr;
    const char* root_prefix = nullptr;
    size_t root_prefix_size = 0;
    if (strncmp(absolute, "/storage/emulated/", 18) == 0) {
        user = absolute + 18;
        root_prefix = "/storage/emulated/";
        root_prefix_size = 18;
    } else if (strncmp(absolute, "/data/media/", 12) == 0) {
        user = absolute + 12;
        root_prefix = "/data/media/";
        root_prefix_size = 12;
    } else if (strncmp(absolute, "/mnt/user/", 10) == 0) {
        const char* mount_user = absolute + 10;
        const char* mount_end = strchr(mount_user, '/');
        if (mount_end == nullptr || strncmp(mount_end, "/emulated/", 10) != 0) {
            return false;
        }
        user = mount_end + 10;
        const char* storage_end = strchr(user, '/');
        if (storage_end == nullptr
            || static_cast<size_t>(mount_end - mount_user)
                != static_cast<size_t>(storage_end - user)
            || memcmp(mount_user, user, storage_end - user) != 0) return false;
        separator = storage_end;
        const int written = snprintf(root, root_capacity, "/mnt/user/%.*s/emulated/%.*s",
            static_cast<int>(mount_end - mount_user), mount_user,
            static_cast<int>(storage_end - user), user);
        if (written <= 0 || static_cast<size_t>(written) >= root_capacity) return false;
        *relative = separator + 1;
        return (*relative)[0] != '\0';
    } else {
        return false;
    }
    separator = strchr(user, '/');
    if (separator == nullptr || separator == user || separator[1] == '\0') return false;
    for (const char* p = user; p < separator; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    const int written = snprintf(root, root_capacity, "%.*s%.*s",
        static_cast<int>(root_prefix_size), root_prefix,
        static_cast<int>(separator - user), user);
    if (written <= 0 || static_cast<size_t>(written) >= root_capacity) return false;
    *relative = separator + 1;
    return true;
}

bool SplitParent(const char* relative, char* parent, size_t parent_capacity,
                 char* basename, size_t basename_capacity) noexcept {
    if (relative == nullptr || relative[0] == '\0') return false;
    const char* last = strrchr(relative, '/');
    const char* name = last == nullptr ? relative : last + 1;
    const size_t name_size = strlen(name);
    if (name_size == 0 || name_size >= basename_capacity
        || (name_size == 1 && name[0] == '.')
        || (name_size == 2 && name[0] == '.' && name[1] == '.')) return false;
    memcpy(basename, name, name_size + 1);
    const size_t parent_size = last == nullptr ? 0 : static_cast<size_t>(last - relative);
    if (parent_size >= parent_capacity) return false;
    if (parent_size != 0) memcpy(parent, relative, parent_size);
    parent[parent_size] = '\0';
    return true;
}

DirectoryResolveResult ResolveOrCreateParents(
        int root_fd, const char* relative, bool force_walk) noexcept {
    DirectoryResolveResult resolved = ResolveDirectoryBeneath(
        root_fd, relative, force_walk);
    if (resolved.fd >= 0 || resolved.error != ENOENT || relative[0] == '\0') {
        return resolved;
    }
    int current = fcntl(root_fd, F_DUPFD_CLOEXEC, 0);
    if (current < 0) return {-1, errno, 0};
    const char* component = relative;
    while (*component != '\0') {
        const char* slash = strchr(component, '/');
        const size_t size = slash == nullptr ? strlen(component)
                                             : static_cast<size_t>(slash - component);
        if (size == 0 || size > NAME_MAX) { close(current); return {-1, EINVAL, 0}; }
        char name[NAME_MAX + 1]{};
        memcpy(name, component, size);
        int next = openat(current, name, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT) {
            if (mkdirat(current, name, 0770) != 0 && errno != EEXIST) {
                const int error = errno; close(current); return {-1, error, 0};
            }
            next = openat(current, name, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        if (next < 0) { const int error = errno; close(current); return {-1, error, 0}; }
        close(current);
        current = next;
        if (slash == nullptr) break;
        component = slash + 1;
    }
    return {current, 0, kCapabilityComponentFdWalk};
}

}  // namespace

SecureResolvedPath ResolveStoragePathParent(
        const char* absolute_path, bool create_parents,
        ResolverProbeCache* probe_cache) noexcept {
    SecureResolvedPath output;
    char root[PATH_MAX]{};
    char parent[PATH_MAX]{};
    const char* relative = nullptr;
    if (!StorageRelative(absolute_path, root, sizeof(root), &relative)
        || !SplitParent(relative, parent, sizeof(parent), output.basename,
                        sizeof(output.basename))) {
        output.error = EINVAL;
        return output;
    }
    DirectoryResolveResult root_result = OpenDirectoryRoot(root);
    if (root_result.fd < 0) { output.error = root_result.error; return output; }
    const bool force_walk = probe_cache != nullptr
        && probe_cache->mode() == ResolverMode::kComponentWalk;
    DirectoryResolveResult parent_result = create_parents
        ? ResolveOrCreateParents(root_result.fd, parent, force_walk)
        : ResolveDirectoryBeneath(root_result.fd, parent, force_walk);
    close(root_result.fd);
    if (probe_cache != nullptr) {
        if ((parent_result.capability & kCapabilityOpenAt2) != 0) {
            probe_cache->ObserveOpenAt2(0, true);
        } else if ((parent_result.capability & kCapabilityComponentFdWalk) != 0) {
            probe_cache->ObserveOpenAt2(ENOSYS, true);
        }
    }
    output.parent_fd = parent_result.fd;
    output.error = parent_result.error;
    return output;
}

void CloseSecureResolvedPath(SecureResolvedPath* path) noexcept {
    if (path == nullptr) return;
    if (path->parent_fd >= 0) close(path->parent_fd);
    path->parent_fd = -1;
}

bool BuildPinnedProcPath(const SecureResolvedPath& path, char* output,
                         size_t capacity) noexcept {
    if (!path.ok() || output == nullptr || capacity == 0) return false;
    const int written = snprintf(output, capacity, "/proc/self/fd/%d/%s",
                                 path.parent_fd, path.basename);
    return written > 0 && static_cast<size_t>(written) < capacity;
}

}  // namespace pathguard
