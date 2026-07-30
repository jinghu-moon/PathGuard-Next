#pragma once

#include <limits.h>

#include "pathguard/resolver_probe_cache.h"

namespace pathguard {

struct SecureResolvedPath {
    int parent_fd = -1;
    int error = 0;
    char basename[NAME_MAX + 1]{};

    bool ok() const noexcept { return parent_fd >= 0 && basename[0] != '\0'; }
};

// Resolves a storage path to a pinned parent directory and a single basename.
// The returned descriptor must be released with CloseSecureResolvedPath.
SecureResolvedPath ResolveStoragePathParent(
    const char* absolute_path, bool create_parents,
    ResolverProbeCache* probe_cache) noexcept;
void CloseSecureResolvedPath(SecureResolvedPath* path) noexcept;
bool BuildPinnedProcPath(const SecureResolvedPath& path, char* output,
                         size_t capacity) noexcept;

}  // namespace pathguard
