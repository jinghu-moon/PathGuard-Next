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
    uint32_t package_end = 0;
    uint32_t string_offset = 0;
};

inline uint32_t ReadLe32(const uint8_t* value) {
    uint32_t result = 0;
    for (int index = 0; index < 4; ++index) {
        result |= static_cast<uint32_t>(value[index]) << (index * 8);
    }
    return result;
}

inline bool ValidPolicyIndex(const PolicyIndexView& view) {
    return view.data != nullptr
        && view.package_offset <= view.package_end
        && view.package_end <= view.string_offset
        && view.string_offset <= view.size
        && view.package_count
            <= (view.package_end - view.package_offset) / kPackageSize;
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
        if (ReadLe32(entry + kPackageHashOffset) < expected_hash) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }

    for (size_t index = lower; index < view.package_count; ++index) {
        const auto* entry = view.data + view.package_offset + index * kPackageSize;
        if (ReadLe32(entry + kPackageHashOffset) != expected_hash) break;
        const uint32_t relative_offset = ReadLe32(entry + kPackageNameOffset);
        if (relative_offset >= view.size - view.string_offset) continue;
        const char* candidate = reinterpret_cast<const char*>(
            view.data + view.string_offset + relative_offset);
        const size_t remaining = view.size - view.string_offset - relative_offset;
        if (package_length < remaining && candidate[package_length] == '\0'
            && memcmp(candidate, package_name, package_length) == 0) {
            return entry;
        }
    }
    return nullptr;
}

}  // namespace pathguard::binary_format
