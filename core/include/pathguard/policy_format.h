#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pathguard::binary_format {

inline constexpr uint32_t kMagic = 0x424E4750;  // PGNB, little-endian.
inline constexpr uint16_t kFormatVersion = 5;
inline constexpr uint16_t kSchemaVersion = 2;
inline constexpr size_t kHeaderSize = 56;
inline constexpr size_t kPackageSize = 48;
inline constexpr size_t kMountRuleSize = 16;
inline constexpr size_t kEventRuleSize = 16;

inline constexpr size_t kFileSizeOffset = 8;
inline constexpr size_t kPayloadChecksumOffset = 12;
inline constexpr size_t kContentGenerationOffset = 16;
inline constexpr size_t kPackageCountOffset = 24;
inline constexpr size_t kMountRuleCountOffset = 28;
inline constexpr size_t kEventRuleCountOffset = 32;
inline constexpr size_t kPackageTableOffset = 36;
inline constexpr size_t kMountRuleTableOffset = 40;
inline constexpr size_t kEventRuleTableOffset = 44;
inline constexpr size_t kStringTableOffset = 48;
inline constexpr size_t kHeaderFlagsOffset = 52;

inline constexpr uint32_t kPolicyFlagAllowLegacyStringBind = UINT32_C(1) << 0;

inline constexpr size_t kPackageHashOffset = 0;
inline constexpr size_t kPackageNameOffset = 4;
inline constexpr size_t kPackageUsersOffset = 8;
inline constexpr size_t kPackageProcessesOffset = 12;
inline constexpr size_t kPackageFirstMountOffset = 16;
inline constexpr size_t kPackageMountCountOffset = 20;
inline constexpr size_t kPackageFirstEventOffset = 24;
inline constexpr size_t kPackageEventCountOffset = 28;
inline constexpr size_t kPackagePlanGenerationOffset = 32;
inline constexpr size_t kPackageFailureModeOffset = 40;
inline constexpr size_t kPackageMediaCompatOffset = 41;
inline constexpr size_t kPackageProviderCompatOffset = 42;

inline constexpr size_t kMountActionOffset = 0;
inline constexpr size_t kMountDepthOffset = 2;
inline constexpr size_t kMountFlagsOffset = 4;
inline constexpr size_t kMountVisiblePathOffset = 8;
inline constexpr size_t kMountBackingPathOffset = 12;

inline constexpr size_t kEventActionOffset = 0;
inline constexpr size_t kEventSourcePathOffset = 4;
inline constexpr size_t kEventTargetPathOffset = 8;
inline constexpr size_t kEventOptionsOffset = 12;

inline constexpr uint32_t kFnv1a32OffsetBasis = 2166136261u;
inline constexpr uint64_t kFnv1a64OffsetBasis = UINT64_C(14695981039346656037);
inline constexpr uint32_t kCrc32Initial = UINT32_C(0xffffffff);
inline constexpr uint32_t kCrc32XorOut = UINT32_C(0xffffffff);

inline uint32_t Fnv1a32(const uint8_t* data, size_t size) {
    uint32_t hash = kFnv1a32OffsetBasis;
    for (size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

inline uint64_t Fnv1a64(const uint8_t* data, size_t size) {
    uint64_t hash = kFnv1a64OffsetBasis;
    for (size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

inline uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = kCrc32Initial;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ kCrc32XorOut;
}

inline uint32_t PackageNameHash(const char* value, size_t size) {
    return Fnv1a32(reinterpret_cast<const uint8_t*>(value), size);
}

}  // namespace pathguard::binary_format
