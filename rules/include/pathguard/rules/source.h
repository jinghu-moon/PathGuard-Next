#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules_contract.h"

namespace pathguard::rules {

struct SourcePosition {
    std::uint32_t line = 1;
    std::uint32_t column = 1;

    bool operator==(const SourcePosition&) const = default;
};

class LineIndex {
public:
    std::optional<SourcePosition> PositionAt(std::uint32_t offset) const;

private:
    friend class SourceBuffer;

    explicit LineIndex(std::string_view bytes);

    std::string_view bytes_;
    std::vector<std::uint32_t> line_starts_;
};

class SourceBuffer {
public:
    static std::optional<SourceBuffer> Create(std::string file_name,
                                               std::string bytes,
                                               const RulesLimits& limits,
                                               Diagnostic* error);

    SourceBuffer(SourceBuffer&& other) noexcept;
    SourceBuffer& operator=(SourceBuffer&& other) noexcept;
    SourceBuffer(const SourceBuffer&) = delete;
    SourceBuffer& operator=(const SourceBuffer&) = delete;

    std::string_view file_name() const { return file_name_; }
    std::string_view bytes() const { return bytes_; }
    std::uint32_t size() const { return static_cast<std::uint32_t>(bytes_.size()); }
    const LineIndex& line_index() const { return line_index_; }
    bool IsValidSpan(ByteSpan span) const;

private:
    SourceBuffer(std::string file_name, std::string bytes);
    void RebindLineIndex();

    std::string file_name_;
    std::string bytes_;
    LineIndex line_index_;
};

}  // namespace pathguard::rules
