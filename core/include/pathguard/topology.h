#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pathguard {

struct StorageTopologyAlias {
    std::uint64_t mount_id = 0;
    std::string path;
};

struct StorageTopologyMount {
    std::uint32_t user_id = 0;
    std::uint64_t mount_id = 0;
    std::string visible_root;
    std::string backend_root;
    std::string filesystem_type;
    std::vector<StorageTopologyAlias> aliases;
};

struct StorageTopology {
    bool supported = false;
    std::string unsupported_reason;
    std::vector<StorageTopologyMount> mounts;
};

bool ParseMountInfo(std::string_view mountinfo, StorageTopology* topology,
                    std::string* error);
bool ResolveVisibleStoragePath(const StorageTopology& topology, std::uint32_t user_id,
                               std::string_view logical_path, std::string* output,
                               std::string* error);
bool ResolveBackendStoragePath(const StorageTopology& topology, std::uint32_t user_id,
                               std::string_view logical_path, std::string* output,
                               std::string* error);

}  // namespace pathguard
