#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "pathguard/pattern_limits.h"
#include "pathguard/pattern.h"

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
    const std::string_view pattern(
        reinterpret_cast<const char*>(input.data()), input.size());
    const auto compiled = pathguard::pattern::CompilePattern(pattern);
    digest ^= static_cast<std::uint64_t>(compiled.error);
    if (compiled.ok()) {
        digest ^= compiled.program->token_count;
        digest ^= static_cast<std::uint64_t>(compiled.program->specificity) << 16U;
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
    if (input.empty()) return digest;
    const std::size_t payload_size = input.size() - 1;
    const std::size_t pattern_size = input.front() % (payload_size + 1);
    const std::string_view pattern(
        reinterpret_cast<const char*>(input.data() + 1), pattern_size);
    const std::string_view path(
        reinterpret_cast<const char*>(input.data() + 1 + pattern_size),
        payload_size - pattern_size);
    const auto compiled = pathguard::pattern::CompilePattern(pattern);
    digest ^= static_cast<std::uint64_t>(compiled.error);
    if (compiled.ok()) {
        pathguard::pattern::PatternMatchScratch scratch;
        digest ^= static_cast<std::uint64_t>(pathguard::pattern::MatchPattern(
            *compiled.program, path, &scratch));
    }
    return digest;
}

}  // namespace pathguard::pattern::test
