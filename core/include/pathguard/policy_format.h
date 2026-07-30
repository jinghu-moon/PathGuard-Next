#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pathguard/capabilities.h"

namespace pathguard::binary_format {

inline constexpr uint32_t kMagic = 0x424E4750;  // PGNB, little-endian.
inline constexpr uint16_t kFormatVersion = 6;
inline constexpr uint16_t kSchemaVersion = 3;
inline constexpr uint8_t kOperationMaskVersion = 1;
inline constexpr uint32_t kInvalidId = UINT32_MAX;

inline constexpr size_t kHeaderSize = 128;
inline constexpr size_t kPackageSize = 64;
inline constexpr size_t kScopeRefSize = 8;
inline constexpr size_t kSelectorSize = 40;
inline constexpr size_t kActionSize = 48;
inline constexpr size_t kPatternSize = 24;
inline constexpr size_t kPatternTokenSize = 8;
inline constexpr size_t kCharacterClassSize = 24;
inline constexpr size_t kSelectorExceptRefSize = 8;
inline constexpr size_t kStringIndexSize = 8;

inline constexpr size_t kFileSizeOffset = 12;
inline constexpr size_t kPayloadChecksumOffset = 16;
inline constexpr size_t kHeaderFlagsOffset = 20;
inline constexpr size_t kContentGenerationOffset = 24;
inline constexpr size_t kPackageCountOffset = 32;
inline constexpr size_t kScopeRefCountOffset = 36;
inline constexpr size_t kSelectorCountOffset = 40;
inline constexpr size_t kActionCountOffset = 44;
inline constexpr size_t kPatternCountOffset = 48;
inline constexpr size_t kTokenCountOffset = 52;
inline constexpr size_t kClassCountOffset = 56;
inline constexpr size_t kExceptRefCountOffset = 60;
inline constexpr size_t kStringCountOffset = 64;
inline constexpr size_t kStringBytesOffset = 68;
inline constexpr size_t kPackageTableOffset = 72;
inline constexpr size_t kScopeRefTableOffset = 76;
inline constexpr size_t kSelectorTableOffset = 80;
inline constexpr size_t kActionTableOffset = 84;
inline constexpr size_t kPatternTableOffset = 88;
inline constexpr size_t kPatternTokenTableOffset = 92;
inline constexpr size_t kCharacterClassTableOffset = 96;
inline constexpr size_t kSelectorExceptRefTableOffset = 100;
inline constexpr size_t kStringIndexTableOffset = 104;
inline constexpr size_t kStringDataOffset = 108;
inline constexpr size_t kFailureModeOffset = 112;
inline constexpr size_t kOperationMaskVersionOffset = 113;

inline constexpr uint32_t kPolicyFlagAllowLegacyStringBind = UINT32_C(1) << 0;
inline constexpr uint32_t kPackageFlagAllUsers = UINT32_C(1) << 0;
inline constexpr uint32_t kPackageFlagAllProcesses = UINT32_C(1) << 1;
inline constexpr uint32_t kPackageFlagProviderEnabled = UINT32_C(1) << 2;
inline constexpr uint16_t kPatternFlagDegenerate = UINT16_C(1) << 0;
inline constexpr uint32_t kCharacterClassFlagNegated = UINT32_C(1) << 0;

inline constexpr uint64_t kKnownCapabilityMask =
    kCapabilityOpenAt2 | kCapabilityComponentFdWalk
    | kCapabilityProcFdMount | kCapabilityOpenTreeMoveMount
    | kCapabilityStringBindMount | kCapabilityFanotifyFid
    | kCapabilityFanotifyDfidName | kCapabilityFanotifyPidfd
    | kCapabilityFanotifyRenameTarget | kCapabilityProviderCallerUid
    | kCapabilityProviderQueryInsertMapping | kCapabilityFuseCompletePath
    | kCapabilityAppPathAdapter;
inline constexpr uint64_t kKnownOperationMask = kKnownOperationMaskV1;

inline constexpr size_t kMaxPolicyFileSize = 2 * 1024 * 1024;
inline constexpr uint32_t kMaxPackageCount = 1024;
inline constexpr uint32_t kMaxScopeRefCount = 32768;
inline constexpr uint32_t kMaxSelectorCount = 16384;
inline constexpr uint32_t kMaxActionCount = 32768;
inline constexpr uint32_t kMaxPatternCount = 32768;
inline constexpr uint32_t kMaxTokenCount = 65536;
inline constexpr uint32_t kMaxClassCount = 16384;
inline constexpr uint32_t kMaxExceptRefCount = 32768;
inline constexpr uint32_t kMaxStringCount = 32768;
inline constexpr uint32_t kMaxStringBytes = 1024 * 1024;
inline constexpr uint32_t kMaxUsersPerPackage = 32;
inline constexpr uint32_t kMaxProcessesPerPackage = 64;
inline constexpr uint32_t kMaxSelectorsPerPackage = 256;
inline constexpr uint32_t kMaxActionsPerPackage = 512;
inline constexpr uint32_t kMaxPatternTokens = 64;
inline constexpr uint32_t kMaxPatternTokensPerPackage = 4096;
inline constexpr uint32_t kMaxExceptPerSelector = 8;
inline constexpr uint32_t kMaxExceptPerPackage = 256;
inline constexpr uint32_t kMaxDegeneratePerRoot = 16;
inline constexpr uint32_t kMaxDegeneratePerPackage = 32;
inline constexpr uint32_t kMaxCandidatesPerBucket = 64;

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
