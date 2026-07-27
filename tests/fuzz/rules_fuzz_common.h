#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "pathguard/rules/arrow_scanner.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace pathguard::rules::fuzz_test {

inline std::string ReadSeed(std::string_view name) {
    const std::filesystem::path path = std::filesystem::path(PATHGUARD_SOURCE_DIR)
        / "tests" / "fuzz" / "seeds" / name;
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

inline std::uint64_t Next(std::uint64_t* state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

inline std::string Mutate(std::string input, std::uint64_t* state) {
    static constexpr std::string_view alphabet =
        "[]{}=,#.\\\"'-> \t\r\nabcXYZ012\x80\xEF\xBB\xBF";
    const std::size_t operations = 1 + Next(state) % 4;
    for (std::size_t index = 0; index < operations; ++index) {
        const std::size_t action = Next(state) % 3;
        const char value = alphabet[Next(state) % alphabet.size()];
        if (action == 0 && input.size() < 2048) {
            const std::size_t offset = input.empty() ? 0 : Next(state) % (input.size() + 1);
            input.insert(input.begin() + static_cast<std::ptrdiff_t>(offset), value);
        } else if (action == 1 && !input.empty()) {
            input[Next(state) % input.size()] = value;
        } else if (!input.empty()) {
            input.erase(input.begin()
                + static_cast<std::ptrdiff_t>(Next(state) % input.size()));
        }
    }
    return input;
}

inline bool HasValidInvariants(const SourceBuffer& source,
                               const ArrowScanResult& result,
                               const RulesLimits& limits,
                               bool require_full_consumption_when_clean) {
    if (result.max_frame_depth > limits.max_container_depth
        || result.significant_tokens > limits.max_tokens_or_nodes
        || result.bytes_consumed > source.size()
        || result.open_frames_at_eof > limits.max_container_depth
        || result.candidates.size() > limits.max_rewrites
        || result.diagnostics.size()
            > std::max<std::size_t>(limits.max_diagnostics, 1)
        || (!result.diagnostics.empty() && !result.candidates.empty())) {
        return false;
    }
    if (require_full_consumption_when_clean && result.diagnostics.empty()) {
        if (result.bytes_consumed != source.size()) return false;
    }
    for (const Diagnostic& diagnostic : result.diagnostics) {
        if (!source.IsValidSpan(diagnostic.primary)) return false;
    }
    for (const ArrowCandidate& candidate : result.candidates) {
        if (!source.IsValidSpan(candidate.rule)
            || !source.IsValidSpan(candidate.source)
            || !source.IsValidSpan(candidate.arrow)
            || !source.IsValidSpan(candidate.target)
            || candidate.rule.begin != candidate.source.begin
            || candidate.rule.end != candidate.target.end) {
            return false;
        }
    }
    return true;
}

inline void Validate(std::string bytes, const RulesLimits& limits,
                     bool require_full_consumption_when_clean) {
    Diagnostic source_error;
    auto source = SourceBuffer::Create("fuzz.toml", std::move(bytes), limits,
                                       &source_error);
    assert(source.has_value());
    const ArrowScanResult result = ScanArrowCandidates(*source, limits);
    assert(HasValidInvariants(*source, result, limits,
                              require_full_consumption_when_clean));
}

inline void VerifyFaultInjection(const std::string& seed,
                                 const RulesLimits& limits) {
    Diagnostic source_error;
    auto source = SourceBuffer::Create("fault.toml", seed, limits, &source_error);
    assert(source.has_value());

    ArrowScanResult bad_offset;
    bad_offset.bytes_consumed = source->size();
    bad_offset.candidates.push_back({{0, source->size()}, {0, source->size()},
                                     {source->size(), source->size() + 1},
                                     {0, source->size()}, 1});
    assert(!HasValidInvariants(*source, bad_offset, limits, true));

    ArrowScanResult bad_frame;
    bad_frame.bytes_consumed = source->size();
    bad_frame.max_frame_depth = static_cast<std::uint32_t>(
        limits.max_container_depth + 1);
    assert(!HasValidInvariants(*source, bad_frame, limits, true));

    ArrowScanResult bad_eof_progress;
    bad_eof_progress.bytes_consumed = source->size() - 1;
    assert(!HasValidInvariants(*source, bad_eof_progress, limits, true));
}

}  // namespace pathguard::rules::fuzz_test
