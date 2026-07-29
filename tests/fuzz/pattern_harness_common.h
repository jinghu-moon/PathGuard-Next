#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "pathguard/pattern_limits.h"

namespace pathguard::pattern::test {

inline constexpr std::uint64_t kPatternHarnessDigestBasis =
    UINT64_C(0x7061747465726e31);

inline std::uint64_t ConsumeTokenizerInput(
    std::span<const std::uint8_t> input) noexcept {
    std::uint64_t digest = kPatternHarnessDigestBasis
        ^ kPatternLimitsProfileV1.max_pattern_tokens;
    for (const std::uint8_t byte : input) {
        digest ^= byte;
        digest *= UINT64_C(1099511628211);
    }
    return digest;
}

inline std::uint64_t ConsumeMatcherInput(
    std::span<const std::uint8_t> input) noexcept {
    std::uint64_t digest = kPatternHarnessDigestBasis ^ input.size()
        ^ kPatternLimitsProfileV1.matcher_transition_budget;
    for (const std::uint8_t byte : input) {
        digest ^= digest << 7;
        digest ^= digest >> 9;
        digest += byte;
    }
    return digest;
}

}  // namespace pathguard::pattern::test
