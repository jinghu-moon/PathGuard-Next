#pragma once

#include <stdint.h>

namespace pathguard {

using CapabilityBits = uint64_t;

inline constexpr CapabilityBits kCapabilityOpenAt2 = UINT64_C(1) << 0;
inline constexpr CapabilityBits kCapabilityComponentFdWalk = UINT64_C(1) << 1;
inline constexpr CapabilityBits kCapabilityProcFdMount = UINT64_C(1) << 2;
inline constexpr CapabilityBits kCapabilityOpenTreeMoveMount = UINT64_C(1) << 3;
inline constexpr CapabilityBits kCapabilityStringBindMount = UINT64_C(1) << 4;

inline constexpr CapabilityBits kCapabilityFanotifyFid = UINT64_C(1) << 8;
inline constexpr CapabilityBits kCapabilityFanotifyDfidName = UINT64_C(1) << 9;
inline constexpr CapabilityBits kCapabilityFanotifyPidfd = UINT64_C(1) << 10;
inline constexpr CapabilityBits kCapabilityFanotifyRenameTarget = UINT64_C(1) << 11;

inline constexpr CapabilityBits kCapabilityProviderCallerUid = UINT64_C(1) << 16;
inline constexpr CapabilityBits kCapabilityProviderQueryInsertMapping = UINT64_C(1) << 17;
inline constexpr CapabilityBits kCapabilityFuseCompletePath = UINT64_C(1) << 18;
inline constexpr CapabilityBits kCapabilityAppPathAdapter = UINT64_C(1) << 19;

using OperationMask = uint64_t;

inline constexpr OperationMask kOperationLookupStat = UINT64_C(1) << 0;
inline constexpr OperationMask kOperationAccess = UINT64_C(1) << 1;
inline constexpr OperationMask kOperationOpenRead = UINT64_C(1) << 2;
inline constexpr OperationMask kOperationOpenWrite = UINT64_C(1) << 3;
inline constexpr OperationMask kOperationCreate = UINT64_C(1) << 4;
inline constexpr OperationMask kOperationDirectoryIterate = UINT64_C(1) << 5;
inline constexpr OperationMask kOperationMkdir = UINT64_C(1) << 6;
inline constexpr OperationMask kOperationRename = UINT64_C(1) << 7;
inline constexpr OperationMask kOperationHardLink = UINT64_C(1) << 8;
inline constexpr OperationMask kOperationUnlink = UINT64_C(1) << 9;
inline constexpr OperationMask kOperationRmdir = UINT64_C(1) << 10;
inline constexpr OperationMask kOperationCanonicalPath = UINT64_C(1) << 11;
inline constexpr OperationMask kOperationReadlink = UINT64_C(1) << 12;
inline constexpr OperationMask kOperationMetadataMutation = UINT64_C(1) << 13;
inline constexpr OperationMask kOperationTruncate = UINT64_C(1) << 14;
inline constexpr OperationMask kOperationWatch = UINT64_C(1) << 15;
inline constexpr OperationMask kOperationProviderQuery = UINT64_C(1) << 16;
inline constexpr OperationMask kOperationProviderInsert = UINT64_C(1) << 17;
inline constexpr OperationMask kOperationMediaScan = UINT64_C(1) << 18;
inline constexpr OperationMask kOperationReverseMapping = UINT64_C(1) << 19;
inline constexpr OperationMask kOperationCloseWriteEvent = UINT64_C(1) << 20;
inline constexpr OperationMask kOperationMoveEvent = UINT64_C(1) << 21;
inline constexpr OperationMask kOperationDeleteEvent = UINT64_C(1) << 22;

inline constexpr OperationMask kKnownOperationMaskV1 =
    (UINT64_C(1) << 23) - 1;
inline constexpr OperationMask kProviderCompositeOperationsV1 =
    UINT64_C(0x000ffeff);
inline constexpr OperationMask kAppPathOperationsV1 = UINT64_C(0x0000ffff);
inline constexpr OperationMask kCompleteVfsOperationsV1 = UINT64_C(0x000fffff);

}  // namespace pathguard
