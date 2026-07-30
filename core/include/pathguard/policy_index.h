#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pathguard/policy_format.h"

namespace pathguard::binary_format {

struct PolicyIndexView {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t package_count = 0;
    uint32_t package_offset = 0;
    uint32_t string_index_offset = 0;
    uint32_t string_data_offset = 0;
    uint32_t string_count = 0;
};

inline uint32_t ReadLe32(const uint8_t* value) {
    uint32_t result = 0;
    for (int index = 0; index < 4; ++index) {
        result |= static_cast<uint32_t>(value[index]) << (index * 8);
    }
    return result;
}

inline bool ValidPolicyIndex(const PolicyIndexView& view) {
    return view.data != nullptr && view.package_count > 0
        && view.package_count <= kMaxPackageCount
        && view.string_count > 0 && view.string_count <= kMaxStringCount
        && view.package_offset == kHeaderSize
        && static_cast<uint64_t>(view.package_offset)
                + static_cast<uint64_t>(view.package_count) * kPackageSize
            <= view.string_index_offset
        && static_cast<uint64_t>(view.string_index_offset)
                + static_cast<uint64_t>(view.string_count) * kStringIndexSize
            <= view.string_data_offset
        && view.string_data_offset <= view.size;
}

inline bool StringById(const PolicyIndexView& view, uint32_t id,
                       const char** bytes, size_t* length) {
    if (!ValidPolicyIndex(view) || id >= view.string_count
        || bytes == nullptr || length == nullptr) return false;
    const uint8_t* row = view.data + view.string_index_offset
        + static_cast<size_t>(id) * kStringIndexSize;
    const uint32_t offset = ReadLe32(row);
    const uint32_t count = ReadLe32(row + 4);
    if (static_cast<uint64_t>(view.string_data_offset) + offset + count
        > view.size) return false;
    *bytes = reinterpret_cast<const char*>(
        view.data + view.string_data_offset + offset);
    *length = count;
    return true;
}

inline const uint8_t* FindPackageEntry(const PolicyIndexView& view,
                                       const char* package_name,
                                       size_t package_length) {
    if (!ValidPolicyIndex(view) || package_name == nullptr || package_length == 0) {
        return nullptr;
    }
    const uint32_t expected_hash = PackageNameHash(package_name, package_length);
    size_t lower = 0;
    size_t upper = view.package_count;
    while (lower < upper) {
        const size_t middle = lower + (upper - lower) / 2;
        const auto* entry = view.data + view.package_offset + middle * kPackageSize;
        if (ReadLe32(entry) < expected_hash) lower = middle + 1;
        else upper = middle;
    }
    for (size_t index = lower; index < view.package_count; ++index) {
        const auto* entry = view.data + view.package_offset + index * kPackageSize;
        if (ReadLe32(entry) != expected_hash) break;
        const char* candidate = nullptr;
        size_t candidate_length = 0;
        if (StringById(view, ReadLe32(entry + 4), &candidate, &candidate_length)
            && candidate_length == package_length
            && memcmp(candidate, package_name, package_length) == 0) {
            return entry;
        }
    }
    return nullptr;
}

}  // namespace pathguard::binary_format
