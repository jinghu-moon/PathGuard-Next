#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules_contract.h"

namespace pathguard::rules {

struct ArrowRewrite {
    RuleId id = 0;
    ByteSpan rule;
    ByteSpan source;
    ByteSpan arrow;
    ByteSpan target;
};

enum class RewriteSegmentKind : std::uint8_t {
    kCopied,
    kSyntheticRule,
    kSyntheticArrow,
};

struct RewriteSegment {
    ByteSpan generated;
    ByteSpan original;
    RewriteSegmentKind kind = RewriteSegmentKind::kCopied;
};

class RewriteMap {
public:
    static RewriteMap Identity(std::uint32_t source_size);
    static std::optional<RewriteMap> Create(
        std::vector<RewriteSegment> segments,
        std::uint32_t generated_size,
        std::uint32_t original_size);

    std::optional<ByteSpan> MapGeneratedPosition(std::uint32_t offset) const;
    std::optional<ByteSpan> MapGeneratedSpan(ByteSpan span) const;
    const std::vector<RewriteSegment>& segments() const { return segments_; }
    std::uint64_t query_count() const { return query_count_; }

private:
    RewriteMap(std::vector<RewriteSegment> segments,
               std::uint32_t generated_size,
               std::uint32_t original_size);
    std::optional<ByteSpan> MapPositionUnchecked(std::uint32_t offset) const;

    std::vector<RewriteSegment> segments_;
    std::uint32_t generated_size_ = 0;
    std::uint32_t original_size_ = 0;
    mutable std::uint64_t query_count_ = 0;
};

struct GeneratedRedirect {
    RuleId id = 0;
    ByteSpan original_rule;
    ByteSpan original_source;
    ByteSpan original_arrow;
    ByteSpan original_target;
    ByteSpan generated_table;
    ByteSpan generated_source;
    ByteSpan generated_target;
};

struct DesugarResult {
    std::optional<std::string> generated_storage;
    RewriteMap rewrite_map = RewriteMap::Identity(0);
    std::vector<GeneratedRedirect> redirects;
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.empty(); }
    bool rewritten() const { return generated_storage.has_value(); }
    std::string_view parser_input(const SourceBuffer& source) const {
        return generated_storage ? std::string_view(*generated_storage)
                                 : source.bytes();
    }
};

DesugarResult EmitDesugaredSource(const SourceBuffer& source,
                                  std::span<const ArrowRewrite> rewrites,
                                  const RulesLimits& limits);
DesugarResult DesugarRulesSource(const SourceBuffer& source,
                                 const RulesLimits& limits);

}  // namespace pathguard::rules
