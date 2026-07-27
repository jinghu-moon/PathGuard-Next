#pragma once

#include <cstdint>
#include <vector>

#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules_contract.h"

namespace pathguard::rules {

struct ArrowCandidate {
    ByteSpan rule;
    ByteSpan source;
    ByteSpan arrow;
    ByteSpan target;
    std::uint32_t array_frame_id = 0;
};

struct ArrowScanResult {
    std::vector<ArrowCandidate> candidates;
    std::vector<Diagnostic> diagnostics;
    std::uint32_t significant_tokens = 0;
    std::uint32_t max_frame_depth = 0;
    std::uint32_t bytes_consumed = 0;
    std::uint32_t open_frames_at_eof = 0;

    bool parser_allowed() const { return diagnostics.empty(); }
};

struct RulesLexResult : ArrowScanResult {
    std::uint32_t format_version = 0;
};

ArrowScanResult ScanArrowCandidates(const SourceBuffer& source,
                                    const RulesLimits& limits);
RulesLexResult AnalyzeRulesSource(const SourceBuffer& source,
                                  const RulesLimits& limits);

}  // namespace pathguard::rules
