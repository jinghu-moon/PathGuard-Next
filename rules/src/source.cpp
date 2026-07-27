#include "pathguard/rules/source.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace pathguard::rules {
namespace {

bool HasUtf8Bom(std::string_view bytes) {
    return bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xefU
        && static_cast<unsigned char>(bytes[1]) == 0xbbU
        && static_cast<unsigned char>(bytes[2]) == 0xbfU;
}

std::uint32_t Utf8Width(std::string_view bytes, std::uint32_t offset,
                        std::uint32_t limit) {
    const unsigned char lead = static_cast<unsigned char>(bytes[offset]);
    std::uint32_t width = 1;
    if ((lead & 0xe0U) == 0xc0U) width = 2;
    else if ((lead & 0xf0U) == 0xe0U) width = 3;
    else if ((lead & 0xf8U) == 0xf0U) width = 4;
    if (offset + width > limit) return 1;
    for (std::uint32_t index = 1; index < width; ++index) {
        if ((static_cast<unsigned char>(bytes[offset + index]) & 0xc0U) != 0x80U) {
            return 1;
        }
    }
    return width;
}

}  // namespace

LineIndex::LineIndex(std::string_view bytes) : bytes_(bytes), line_starts_{0} {
    for (std::uint32_t offset = 0; offset < bytes.size(); ++offset) {
        if (bytes[offset] == '\n') line_starts_.push_back(offset + 1);
    }
}

std::optional<SourcePosition> LineIndex::PositionAt(std::uint32_t offset) const {
    if (offset > bytes_.size()) return std::nullopt;
    const auto next = std::upper_bound(line_starts_.begin(), line_starts_.end(),
                                       offset);
    const std::size_t line_index = static_cast<std::size_t>(next - line_starts_.begin() - 1);
    std::uint32_t cursor = line_starts_[line_index];
    if (line_index == 0 && HasUtf8Bom(bytes_) && cursor < 3 && offset >= 3) {
        cursor = 3;
    }
    std::uint32_t column = 1;
    while (cursor < offset) {
        if (bytes_[cursor] == '\r' && cursor + 1 < bytes_.size()
            && bytes_[cursor + 1] == '\n') {
            ++cursor;
            continue;
        }
        cursor += Utf8Width(bytes_, cursor, offset);
        ++column;
    }
    return SourcePosition{static_cast<std::uint32_t>(line_index + 1), column};
}

SourceBuffer::SourceBuffer(std::string file_name, std::string bytes)
    : file_name_(std::move(file_name)),
      bytes_(std::move(bytes)),
      line_index_(bytes_) {}

SourceBuffer::SourceBuffer(SourceBuffer&& other) noexcept
    : file_name_(std::move(other.file_name_)),
      bytes_(std::move(other.bytes_)),
      line_index_(bytes_) {}

SourceBuffer& SourceBuffer::operator=(SourceBuffer&& other) noexcept {
    if (this == &other) return *this;
    file_name_ = std::move(other.file_name_);
    bytes_ = std::move(other.bytes_);
    RebindLineIndex();
    return *this;
}

void SourceBuffer::RebindLineIndex() {
    line_index_ = LineIndex(bytes_);
}

std::optional<SourceBuffer> SourceBuffer::Create(std::string file_name,
                                                  std::string bytes,
                                                  const RulesLimits& limits,
                                                  Diagnostic* error) {
    if (bytes.size() > limits.max_source_bytes
        || bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        if (error != nullptr) {
            const std::size_t bounded = std::min<std::size_t>(
                bytes.size(), std::numeric_limits<std::uint32_t>::max());
            *error = {kResourceLimit, "rules.source_too_large",
                      {0, static_cast<std::uint32_t>(bounded)}, false};
        }
        return std::nullopt;
    }
    return SourceBuffer(std::move(file_name), std::move(bytes));
}

bool SourceBuffer::IsValidSpan(ByteSpan span) const {
    return span.begin <= span.end && span.end <= bytes_.size();
}

}  // namespace pathguard::rules
