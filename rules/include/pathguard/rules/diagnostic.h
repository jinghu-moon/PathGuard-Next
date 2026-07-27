#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pathguard::rules {

using RuleId = std::uint32_t;

struct ByteSpan {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;

    std::uint32_t size() const { return end - begin; }
    bool operator==(const ByteSpan&) const = default;
};

enum class DiagnosticSeverity : std::uint8_t {
    kError,
    kWarning,
};

enum class DiagnosticPhase : std::uint8_t {
    kSource,
    kLex,
    kParse,
    kDecode,
    kSemantic,
    kAdmission,
    kPublish,
    kInternal,
};

struct RelatedSpan {
    ByteSpan span;
    std::string message_key;
};

struct Diagnostic {
    std::string_view code;
    std::string_view message_key;
    ByteSpan primary;
    bool omitted = false;
    DiagnosticSeverity severity = DiagnosticSeverity::kError;
    DiagnosticPhase phase = DiagnosticPhase::kLex;
    std::vector<RelatedSpan> related;
    std::string field_path;
    std::vector<std::string> arguments;
};

class SourceBuffer;

std::string RenderDiagnosticText(const Diagnostic& diagnostic,
                                 const SourceBuffer& source);
std::string RenderDiagnosticJson(const Diagnostic& diagnostic,
                                 const SourceBuffer& source);

}  // namespace pathguard::rules
