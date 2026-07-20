#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pathguard::binary_format {

inline constexpr uint32_t kMagic = 0x424E4750;  // P G N B, little endian
inline constexpr uint16_t kFormatVersion = 3;
inline constexpr size_t kHeaderSize = 40;
inline constexpr size_t kPackageSize = 32;
inline constexpr size_t kRuleSize = 20;

inline constexpr size_t kPackageHashOffset = 0;
inline constexpr size_t kPackageNameOffset = 4;
inline constexpr size_t kPackageUsersOffset = 8;
inline constexpr size_t kPackageProcessesOffset = 12;
inline constexpr size_t kPackageFirstRuleOffset = 16;
inline constexpr size_t kPackageRuleCountOffset = 20;
inline constexpr size_t kPackageEnabledOffset = 24;
inline constexpr size_t kPackageMediaCompatOffset = 28;

inline uint32_t Fnv1a32(const uint8_t* data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619u;
    }
    return hash;
}

inline uint32_t PackageNameHash(const char* value, size_t size) {
    return Fnv1a32(reinterpret_cast<const uint8_t*>(value), size);
}

}  // namespace pathguard::binary_format
