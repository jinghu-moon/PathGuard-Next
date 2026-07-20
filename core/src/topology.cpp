#include "pathguard/topology.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pathguard {
namespace {

struct MountRecord {
    std::uint64_t mount_id = 0;
    std::string root;
    std::string mountpoint;
    std::string filesystem_type;
    std::string source;
};

bool SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

bool Unsupported(StorageTopology* topology, std::string* error,
                 std::string message) {
    topology->supported = false;
    topology->unsupported_reason = message;
    return SetError(error, std::move(message));
}

bool ParseUnsigned(std::string_view value, std::uint64_t* output) {
    if (value.empty()) return false;
    const char* first = value.data();
    const char* last = first + value.size();
    const auto result = std::from_chars(first, last, *output);
    return result.ec == std::errc() && result.ptr == last;
}

bool DecodeMountInfoPath(std::string_view input, std::string* output) {
    output->clear();
    output->reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] != '\\') {
            output->push_back(input[index]);
            continue;
        }
        if (index + 3 >= input.size()
            || input[index + 1] < '0' || input[index + 1] > '7'
            || input[index + 2] < '0' || input[index + 2] > '7'
            || input[index + 3] < '0' || input[index + 3] > '7') {
            return false;
        }
        const unsigned value = (static_cast<unsigned>(input[index + 1] - '0') << 6)
            | (static_cast<unsigned>(input[index + 2] - '0') << 3)
            | static_cast<unsigned>(input[index + 3] - '0');
        if (value == 0 || value > 0x7f) return false;
        output->push_back(static_cast<char>(value));
        index += 3;
    }
    return true;
}

bool IsDecimal(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char item) {
        return item >= '0' && item <= '9';
    });
}

bool ParseUserId(std::string_view value, std::uint32_t* user_id) {
    std::uint64_t parsed = 0;
    return IsDecimal(value) && ParseUnsigned(value, &parsed) && parsed <= UINT32_MAX
        && ((*user_id = static_cast<std::uint32_t>(parsed)), true);
}

bool ParsePrimaryRoot(std::string_view mountpoint, std::uint32_t* user_id) {
    constexpr std::string_view prefix = "/storage/emulated/";
    return mountpoint.rfind(prefix, 0) == 0
        && ParseUserId(mountpoint.substr(prefix.size()), user_id);
}

bool ParseUserMountpoint(std::string_view mountpoint, std::string_view prefix,
                         std::string_view suffix, std::uint32_t* user_id) {
    if (mountpoint.rfind(prefix, 0) != 0) return false;
    const std::size_t separator = mountpoint.find('/', prefix.size());
    if (separator == std::string_view::npos
        || mountpoint.substr(separator) != suffix) {
        return false;
    }
    return ParseUserId(mountpoint.substr(
        prefix.size(), separator - prefix.size()), user_id);
}

bool IsSafeLogicalPath(std::string_view path) {
    if (path.empty()) return true;
    if (path.front() == '/' || path.back() == '/') return false;
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view component = path.substr(
            start, end == std::string_view::npos ? path.size() - start : end - start);
        if (component.empty() || component == "." || component == ".."
            || component.find('\\') != std::string_view::npos
            || component.find('{') != std::string_view::npos
            || component.find('}') != std::string_view::npos) {
            return false;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

bool DeriveBackendRoot(std::string_view source, std::uint32_t user_id,
                       std::string* backend_root) {
    if (source == "/data/media") {
        *backend_root = "/data/media/" + std::to_string(user_id);
        return true;
    }
    const std::string expected = "/data/media/" + std::to_string(user_id);
    if (source == expected) {
        *backend_root = expected;
        return true;
    }
    return false;
}

StorageTopologyMount* FindUserMount(StorageTopology* topology,
                                    std::uint32_t user_id) {
    for (StorageTopologyMount& mount : topology->mounts) {
        if (mount.user_id == user_id) return &mount;
    }
    return nullptr;
}

const StorageTopologyMount* FindUserMount(const StorageTopology& topology,
                                          std::uint32_t user_id) {
    for (const StorageTopologyMount& mount : topology.mounts) {
        if (mount.user_id == user_id) return &mount;
    }
    return nullptr;
}

void AddAlias(StorageTopologyMount* mount, std::uint64_t mount_id,
              const std::string& path) {
    const auto existing = std::find_if(
        mount->aliases.begin(), mount->aliases.end(),
        [mount_id, &path](const StorageTopologyAlias& alias) {
            return alias.mount_id == mount_id && alias.path == path;
        });
    if (existing == mount->aliases.end()) {
        mount->aliases.push_back({mount_id, path});
    }
}

bool ResolvePath(const StorageTopology& topology, std::uint32_t user_id,
                 std::string_view logical_path, bool backend, std::string* output,
                 std::string* error) {
    if (output == nullptr || !topology.supported) {
        return SetError(error, topology.unsupported_reason.empty()
            ? "storage topology is unsupported" : topology.unsupported_reason);
    }
    if (!IsSafeLogicalPath(logical_path)) {
        return SetError(error, "invalid logical storage path");
    }
    const StorageTopologyMount* mount = FindUserMount(topology, user_id);
    if (mount == nullptr) return SetError(error, "storage user is not mounted");
    const std::string& root = backend ? mount->backend_root : mount->visible_root;
    *output = root;
    if (!logical_path.empty()) {
        output->push_back('/');
        output->append(logical_path);
    }
    return true;
}

}  // namespace

bool ParseMountInfo(std::string_view mountinfo, StorageTopology* topology,
                    std::string* error) {
    if (topology == nullptr) return false;
    *topology = {};
    std::istringstream stream{std::string(mountinfo)};
    std::string line;
    std::vector<MountRecord> records;
    std::size_t line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        if (line.empty()) continue;
        std::istringstream fields(line);
        std::vector<std::string> tokens;
        std::string token;
        while (fields >> token) tokens.push_back(std::move(token));
        const auto separator = std::find(tokens.begin(), tokens.end(), "-");
        if (tokens.size() < 7 || separator == tokens.end()
            || separator - tokens.begin() < 6
            || separator + 2 >= tokens.end()) {
            return Unsupported(topology, error, "malformed mountinfo line "
                + std::to_string(line_number));
        }
        MountRecord record;
        if (!ParseUnsigned(tokens[0], &record.mount_id)
            || !DecodeMountInfoPath(tokens[3], &record.root)
            || !DecodeMountInfoPath(tokens[4], &record.mountpoint)
            || !DecodeMountInfoPath(*(separator + 2), &record.source)
            || record.mountpoint.empty() || record.mountpoint.front() != '/') {
            return Unsupported(topology, error, "invalid mountinfo line "
                + std::to_string(line_number));
        }
        record.filesystem_type = *(separator + 1);
        records.push_back(std::move(record));
    }

    for (const MountRecord& record : records) {
        std::uint32_t user_id = 0;
        if (!ParsePrimaryRoot(record.mountpoint, &user_id)) continue;
        std::string backend_root;
        if (!DeriveBackendRoot(record.source, user_id, &backend_root)) continue;
        StorageTopologyMount* existing = FindUserMount(topology, user_id);
        if (existing != nullptr) {
            if (existing->mount_id != record.mount_id
                || existing->backend_root != backend_root) {
                return Unsupported(topology, error,
                    "conflicting storage roots for user " + std::to_string(user_id));
            }
            continue;
        }
        topology->mounts.push_back({user_id, record.mount_id, record.mountpoint,
                                    backend_root, record.filesystem_type, {}});
    }

    const MountRecord* aggregate_visible = nullptr;
    for (const MountRecord& record : records) {
        if (record.mountpoint != "/storage/emulated") continue;
        if (aggregate_visible != nullptr
            && aggregate_visible->mount_id != record.mount_id) {
            return Unsupported(topology, error,
                               "conflicting aggregate storage roots");
        }
        aggregate_visible = &record;
    }

    std::unordered_map<std::uint32_t, const MountRecord*> user_visible_aliases;
    std::unordered_map<std::uint32_t, const MountRecord*> backend_evidence;
    for (const MountRecord& record : records) {
        std::uint32_t user_id = 0;
        if (ParseUserMountpoint(record.mountpoint, "/mnt/user/", "/emulated",
                                &user_id)) {
            user_visible_aliases.emplace(user_id, &record);
        }
        if (ParseUserMountpoint(record.mountpoint, "/mnt/pass_through/", "/emulated",
                                &user_id)) {
            std::string backend_root;
            if (DeriveBackendRoot(record.source, user_id, &backend_root)) {
                backend_evidence.emplace(user_id, &record);
            }
        }
    }

    for (const auto& item : user_visible_aliases) {
        const std::uint32_t user_id = item.first;
        StorageTopologyMount* mount = FindUserMount(topology, user_id);
        if (mount == nullptr) {
            if (aggregate_visible == nullptr) {
                return Unsupported(topology, error,
                    "missing aggregate visible storage root");
            }
            const auto backend = backend_evidence.find(user_id);
            if (backend == backend_evidence.end()) {
                return Unsupported(topology, error,
                    "missing backend evidence for user " + std::to_string(user_id));
            }
            std::string backend_root;
            if (!DeriveBackendRoot(backend->second->source, user_id, &backend_root)) {
                return Unsupported(topology, error,
                    "invalid backend evidence for user " + std::to_string(user_id));
            }
            topology->mounts.push_back({
                user_id,
                aggregate_visible->mount_id,
                "/storage/emulated/" + std::to_string(user_id),
                backend_root,
                aggregate_visible->filesystem_type,
                {},
            });
            mount = &topology->mounts.back();
        }
        AddAlias(mount, item.second->mount_id, item.second->mountpoint);
    }

    for (const MountRecord& record : records) {
        if (record.mountpoint != "/sdcard"
            && record.mountpoint != "/storage/self/primary") {
            continue;
        }
        for (StorageTopologyMount& mount : topology->mounts) {
            if (mount.user_id == 0) AddAlias(&mount, record.mount_id, record.mountpoint);
        }
    }

    std::sort(topology->mounts.begin(), topology->mounts.end(),
              [](const StorageTopologyMount& lhs, const StorageTopologyMount& rhs) {
        return lhs.user_id < rhs.user_id;
    });
    if (topology->mounts.empty()) {
        return Unsupported(topology, error,
                           "no supported emulated storage root found");
    }
    topology->supported = true;
    topology->unsupported_reason.clear();
    return true;
}

bool ResolveVisibleStoragePath(const StorageTopology& topology, std::uint32_t user_id,
                               std::string_view logical_path, std::string* output,
                               std::string* error) {
    return ResolvePath(topology, user_id, logical_path, false, output, error);
}

bool ResolveBackendStoragePath(const StorageTopology& topology, std::uint32_t user_id,
                               std::string_view logical_path, std::string* output,
                               std::string* error) {
    return ResolvePath(topology, user_id, logical_path, true, output, error);
}

}  // namespace pathguard
