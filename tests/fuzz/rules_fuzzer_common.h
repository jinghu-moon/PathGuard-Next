#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

#include "pathguard/rules/arrow_scanner.h"
#include "pathguard/rules/source.h"

namespace pathguard::rules::fuzzer {

[[noreturn]] inline void FailInvariant() {
    std::abort();
}

inline void CheckResult(const SourceBuffer& source,
                        const ArrowScanResult& result,
                        const RulesLimits& limits) {
    if (result.max_frame_depth > limits.max_container_depth
        || result.significant_tokens > limits.max_tokens_or_nodes
        || result.bytes_consumed > source.size()
        || result.open_frames_at_eof > limits.max_container_depth
        || result.candidates.size() > limits.max_rewrites
        || result.diagnostics.size()
            > (limits.max_diagnostics == 0 ? 1 : limits.max_diagnostics)
        || (!result.diagnostics.empty() && !result.candidates.empty())) {
        FailInvariant();
    }
    for (const Diagnostic& diagnostic : result.diagnostics) {
        if (!source.IsValidSpan(diagnostic.primary)) FailInvariant();
    }
    for (const ArrowCandidate& candidate : result.candidates) {
        if (!source.IsValidSpan(candidate.rule)
            || !source.IsValidSpan(candidate.source)
            || !source.IsValidSpan(candidate.arrow)
            || !source.IsValidSpan(candidate.target)
            || candidate.rule.begin != candidate.source.begin
            || candidate.rule.end != candidate.target.end) {
            FailInvariant();
        }
    }
}

inline std::optional<SourceBuffer> MakeSource(const std::uint8_t* data,
                                               std::size_t size,
                                               const RulesLimits& limits) {
    Diagnostic error;
    return SourceBuffer::Create(
        "fuzz.toml",
        std::string(reinterpret_cast<const char*>(data), size),
        limits, &error);
}

}  // namespace pathguard::rules::fuzzer
