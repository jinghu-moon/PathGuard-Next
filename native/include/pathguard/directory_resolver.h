#pragma once

#include <stdint.h>

namespace pathguard {

struct DirectoryResolveResult {
    int fd = -1;
    int error = 0;
    uint64_t capability = 0;
};

DirectoryResolveResult OpenDirectoryRoot(const char* absolute_path);
DirectoryResolveResult ResolveDirectoryBeneath(int root_fd, const char* logical_path,
                                               bool force_component_walk);

}  // namespace pathguard
