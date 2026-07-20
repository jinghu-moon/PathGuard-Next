#include "pathguard/directory_resolver.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/openat2.h>
#include <stddef.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "pathguard/capabilities.h"

namespace pathguard {
namespace {

bool ValidLogicalPath(const char* path) {
    if (path == nullptr || path[0] == '/' || strchr(path, '\\') != nullptr) return false;
    if (path[0] == '\0') return true;
    const char* component = path;
    while (*component != '\0') {
        const char* separator = strchr(component, '/');
        const size_t length = separator == nullptr
            ? strlen(component)
            : static_cast<size_t>(separator - component);
        if (length == 0 || length > NAME_MAX
            || (length == 1 && component[0] == '.')
            || (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (separator == nullptr) break;
        component = separator + 1;
    }
    return true;
}

DirectoryResolveResult WalkDirectoryComponents(int root_fd, const char* logical_path) {
    DirectoryResolveResult result;
    int current = fcntl(root_fd, F_DUPFD_CLOEXEC, 0);
    if (current < 0) {
        result.error = errno;
        return result;
    }
    if (logical_path[0] == '\0') {
        result.fd = current;
        result.capability = kCapabilityComponentFdWalk;
        return result;
    }

    const char* component = logical_path;
    while (*component != '\0') {
        const char* separator = strchr(component, '/');
        const size_t length = separator == nullptr
            ? strlen(component)
            : static_cast<size_t>(separator - component);
        char name[NAME_MAX + 1]{};
        memcpy(name, component, length);
        const int next = openat(
            current, name, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            result.error = errno;
            close(current);
            return result;
        }
        close(current);
        current = next;
        if (separator == nullptr) break;
        component = separator + 1;
    }
    result.fd = current;
    result.capability = kCapabilityComponentFdWalk;
    return result;
}

bool OpenAt2Unavailable(int error) {
    return error == ENOSYS || error == EINVAL || error == EPERM || error == EACCES;
}

}  // namespace

DirectoryResolveResult OpenDirectoryRoot(const char* absolute_path) {
    DirectoryResolveResult result;
    if (absolute_path == nullptr || absolute_path[0] != '/') {
        result.error = EINVAL;
        return result;
    }
    result.fd = open(
        absolute_path, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (result.fd < 0) result.error = errno;
    return result;
}

DirectoryResolveResult ResolveDirectoryBeneath(int root_fd, const char* logical_path,
                                               bool force_component_walk) {
    DirectoryResolveResult result;
    if (root_fd < 0 || !ValidLogicalPath(logical_path)) {
        result.error = EINVAL;
        return result;
    }
    if (!force_component_walk && logical_path[0] != '\0') {
        open_how how{};
        how.flags = O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
        how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
        result.fd = static_cast<int>(syscall(
            __NR_openat2, root_fd, logical_path, &how, sizeof(how)));
        if (result.fd >= 0) {
            result.capability = kCapabilityOpenAt2;
            return result;
        }
        result.error = errno;
        if (!OpenAt2Unavailable(result.error)) return result;
    }
    return WalkDirectoryComponents(root_fd, logical_path);
}

}  // namespace pathguard
