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

}  // namespace pathguard
