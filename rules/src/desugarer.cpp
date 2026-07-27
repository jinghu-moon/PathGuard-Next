#include "pathguard/rules/desugarer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "pathguard/rules/arrow_scanner.h"

namespace pathguard::rules {
namespace {

constexpr std::string_view kPrefix = "{ from = ";
constexpr std::string_view kMiddle = ", to = ";
constexpr std::string_view kSuffix = " }";

DesugarResult Failure(const SourceBuffer& source, std::string_view code,
                      std::string_view message_key, ByteSpan span) {
    DesugarResult result;
    result.rewrite_map = RewriteMap::Identity(source.size());
    result.diagnostics.push_back({code, message_key, span, false});
    return result;
}

bool IsValidRewrite(const SourceBuffer& source, const ArrowRewrite& rewrite) {
    return rewrite.id != 0
        && source.IsValidSpan(rewrite.rule)
        && source.IsValidSpan(rewrite.source)
        && source.IsValidSpan(rewrite.arrow)
        && source.IsValidSpan(rewrite.target)
        && rewrite.rule.begin == rewrite.source.begin
        && rewrite.rule.end == rewrite.target.end
        && rewrite.source.begin < rewrite.source.end
        && rewrite.arrow.begin < rewrite.arrow.end
        && rewrite.target.begin < rewrite.target.end
        && rewrite.source.end <= rewrite.arrow.begin
        && rewrite.arrow.end <= rewrite.target.begin;
}

bool AddSize(std::size_t value, std::size_t increment, std::size_t* output) {
    if (increment > std::numeric_limits<std::size_t>::max() - value) return false;
    *output = value + increment;
    return true;
}

}  // namespace

RewriteMap::RewriteMap(std::vector<RewriteSegment> segments,
                       std::uint32_t generated_size,
                       std::uint32_t original_size)
    : segments_(std::move(segments)),
      generated_size_(generated_size),
      original_size_(original_size) {}

RewriteMap RewriteMap::Identity(std::uint32_t source_size) {
    return RewriteMap({}, source_size, source_size);
}

std::optional<RewriteMap> RewriteMap::Create(
    std::vector<RewriteSegment> segments,
    std::uint32_t generated_size,
    std::uint32_t original_size) {
    if (segments.empty()) return std::nullopt;
    std::uint32_t expected_begin = 0;
    for (const RewriteSegment& segment : segments) {
        if (segment.generated.begin != expected_begin
            || segment.generated.begin >= segment.generated.end
            || segment.generated.end > generated_size
            || segment.original.begin > segment.original.end
            || segment.original.end > original_size
            || (segment.kind == RewriteSegmentKind::kCopied
                && segment.generated.size() != segment.original.size())) {
            return std::nullopt;
        }
        expected_begin = segment.generated.end;
    }
    if (expected_begin != generated_size) return std::nullopt;
    return RewriteMap(std::move(segments), generated_size, original_size);
}

std::optional<ByteSpan> RewriteMap::MapGeneratedPosition(
    std::uint32_t offset) const {
    ++query_count_;
    return MapPositionUnchecked(offset);
}

std::optional<ByteSpan> RewriteMap::MapPositionUnchecked(
    std::uint32_t offset) const {
    if (offset > generated_size_) return std::nullopt;
    if (segments_.empty()) {
        if (generated_size_ != original_size_) return std::nullopt;
        return ByteSpan{offset, offset};
    }
    if (offset == generated_size_) {
        return ByteSpan{original_size_, original_size_};
    }
    const auto next = std::upper_bound(
        segments_.begin(), segments_.end(), offset,
        [](std::uint32_t value, const RewriteSegment& segment) {
            return value < segment.generated.begin;
        });
    if (next == segments_.begin()) return std::nullopt;
    const RewriteSegment& segment = *std::prev(next);
    if (offset >= segment.generated.end) return std::nullopt;
    if (segment.kind != RewriteSegmentKind::kCopied) return segment.original;
    const std::uint32_t mapped =
        segment.original.begin + (offset - segment.generated.begin);
    return ByteSpan{mapped, mapped};
}

std::optional<ByteSpan> RewriteMap::MapGeneratedSpan(ByteSpan span) const {
    ++query_count_;
    if (span.begin > span.end || span.end > generated_size_) return std::nullopt;
    if (span.begin == span.end) return MapPositionUnchecked(span.begin);
    if (segments_.empty()) {
        if (generated_size_ != original_size_) return std::nullopt;
        return span;
    }

    bool found = false;
    ByteSpan mapped{};
    for (const RewriteSegment& segment : segments_) {
        const std::uint32_t begin = std::max(span.begin, segment.generated.begin);
        const std::uint32_t end = std::min(span.end, segment.generated.end);
        if (begin >= end) continue;
        ByteSpan part = segment.original;
        if (segment.kind == RewriteSegmentKind::kCopied) {
            const std::uint32_t delta = begin - segment.generated.begin;
            part = {segment.original.begin + delta,
                    segment.original.begin + delta + (end - begin)};
        }
        if (!found) {
            mapped = part;
            found = true;
        } else {
            mapped.begin = std::min(mapped.begin, part.begin);
            mapped.end = std::max(mapped.end, part.end);
        }
    }
    return found ? std::optional<ByteSpan>(mapped) : std::nullopt;
}

DesugarResult EmitDesugaredSource(const SourceBuffer& source,
                                  std::span<const ArrowRewrite> rewrites,
                                  const RulesLimits& limits) {
    if (rewrites.empty()) {
        DesugarResult result;
        result.rewrite_map = RewriteMap::Identity(source.size());
        return result;
    }
    if (rewrites.size() > limits.max_rewrites) {
        return Failure(source, kResourceLimit, "rules.rewrite_limit",
                       rewrites.front().rule);
    }

    std::unordered_set<RuleId> ids;
    ids.reserve(rewrites.size());
    std::size_t generated_size = source.size();
    std::uint32_t previous_end = 0;
    for (const ArrowRewrite& rewrite : rewrites) {
        if (!IsValidRewrite(source, rewrite)
            || rewrite.rule.begin < previous_end
            || !ids.insert(rewrite.id).second) {
            return Failure(source, kDesugarInternal,
                           "rules.desugar_invalid_rewrite", rewrite.rule);
        }
        const std::size_t replacement_size = kPrefix.size()
            + rewrite.source.size() + kMiddle.size() + rewrite.target.size()
            + kSuffix.size();
        generated_size -= rewrite.rule.size();
        if (!AddSize(generated_size, replacement_size, &generated_size)
            || generated_size > limits.max_generated_bytes
            || generated_size > std::numeric_limits<std::uint32_t>::max()) {
            return Failure(source, kResourceLimit, "rules.generated_too_large",
                           rewrite.rule);
        }
        previous_end = rewrite.rule.end;
    }

    DesugarResult result;
    result.generated_storage.emplace();
    std::string& output = *result.generated_storage;
    output.reserve(generated_size);
    result.redirects.reserve(rewrites.size());
    std::vector<RewriteSegment> segments;
    segments.reserve(std::min<std::size_t>(limits.max_rewrite_segments,
                                           rewrites.size() * 6 + 1));

    auto append_segment = [&](std::string_view bytes, ByteSpan original,
                              RewriteSegmentKind kind) -> bool {
        if (bytes.empty()) return true;
        if (segments.size() >= limits.max_rewrite_segments) return false;
        const std::uint32_t begin = static_cast<std::uint32_t>(output.size());
        output.append(bytes);
        const std::uint32_t end = static_cast<std::uint32_t>(output.size());
        segments.push_back({{begin, end}, original, kind});
        return true;
    };
    auto append_copied = [&](ByteSpan span) -> bool {
        return append_segment(source.bytes().substr(span.begin, span.size()), span,
                              RewriteSegmentKind::kCopied);
    };

    std::uint32_t source_cursor = 0;
    for (const ArrowRewrite& rewrite : rewrites) {
        if (!append_copied({source_cursor, rewrite.rule.begin})) {
            return Failure(source, kResourceLimit, "rules.rewrite_segment_limit",
                           rewrite.rule);
        }
        GeneratedRedirect generated;
        generated.id = rewrite.id;
        generated.original_rule = rewrite.rule;
        generated.original_source = rewrite.source;
        generated.original_arrow = rewrite.arrow;
        generated.original_target = rewrite.target;
        generated.generated_table.begin =
            static_cast<std::uint32_t>(output.size());
        if (!append_segment(kPrefix, rewrite.rule,
                            RewriteSegmentKind::kSyntheticRule)) {
            return Failure(source, kResourceLimit, "rules.rewrite_segment_limit",
                           rewrite.rule);
        }
        generated.generated_source.begin =
            static_cast<std::uint32_t>(output.size());
        if (!append_copied(rewrite.source)) {
            return Failure(source, kResourceLimit, "rules.rewrite_segment_limit",
                           rewrite.rule);
        }
        generated.generated_source.end =
            static_cast<std::uint32_t>(output.size());
        if (!append_segment(kMiddle, rewrite.arrow,
                            RewriteSegmentKind::kSyntheticArrow)) {
            return Failure(source, kResourceLimit, "rules.rewrite_segment_limit",
                           rewrite.rule);
        }
        generated.generated_target.begin =
            static_cast<std::uint32_t>(output.size());
        if (!append_copied(rewrite.target)) {
            return Failure(source, kResourceLimit, "rules.rewrite_segment_limit",
                           rewrite.rule);
        }
        generated.generated_target.end =
            static_cast<std::uint32_t>(output.size());
        if (!append_segment(kSuffix, rewrite.rule,
                            RewriteSegmentKind::kSyntheticRule)) {
            return Failure(source, kResourceLimit, "rules.rewrite_segment_limit",
                           rewrite.rule);
        }
        generated.generated_table.end =
            static_cast<std::uint32_t>(output.size());
        result.redirects.push_back(generated);
        source_cursor = rewrite.rule.end;
    }
    if (!append_copied({source_cursor, source.size()})
        || output.size() != generated_size) {
        return Failure(source, kDesugarInternal, "rules.desugar_emit_mismatch",
                       {source_cursor, source.size()});
    }
    auto rewrite_map = RewriteMap::Create(
        std::move(segments), static_cast<std::uint32_t>(output.size()),
        source.size());
    if (!rewrite_map) {
        return Failure(source, kDesugarInternal, "rules.desugar_map_invalid",
                       {0, source.size()});
    }
    result.rewrite_map = std::move(*rewrite_map);
    return result;
}

DesugarResult DesugarRulesSource(const SourceBuffer& source,
                                 const RulesLimits& limits) {
    RulesLexResult lexical = AnalyzeRulesSource(source, limits);
    if (!lexical.parser_allowed()) {
        DesugarResult result;
        result.rewrite_map = RewriteMap::Identity(source.size());
        result.diagnostics = std::move(lexical.diagnostics);
        return result;
    }
    std::vector<ArrowRewrite> rewrites;
    rewrites.reserve(lexical.candidates.size());
    for (std::size_t index = 0; index < lexical.candidates.size(); ++index) {
        const ArrowCandidate& candidate = lexical.candidates[index];
        rewrites.push_back({static_cast<RuleId>(index + 1), candidate.rule,
                            candidate.source, candidate.arrow, candidate.target});
    }
    return EmitDesugaredSource(source, rewrites, limits);
}

}  // namespace pathguard::rules
