#include "pathguard/rules/diagnostic.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

#include "pathguard/rules/source.h"

namespace pathguard::rules {
namespace {

std::string_view SeverityName(DiagnosticSeverity severity) {
    return severity == DiagnosticSeverity::kWarning ? "warning" : "error";
}

std::string_view PhaseName(DiagnosticPhase phase) {
    switch (phase) {
        case DiagnosticPhase::kSource: return "source";
        case DiagnosticPhase::kLex: return "lex";
        case DiagnosticPhase::kParse: return "parse";
        case DiagnosticPhase::kDecode: return "decode";
        case DiagnosticPhase::kSemantic: return "semantic";
        case DiagnosticPhase::kAdmission: return "admission";
        case DiagnosticPhase::kPublish: return "publish";
        case DiagnosticPhase::kInternal: return "internal";
    }
    return "internal";
}

std::string EscapeJson(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (byte < 0x20U) {
                    output += "\\u00";
                    output.push_back(hex[byte >> 4U]);
                    output.push_back(hex[byte & 0x0fU]);
                } else {
                    output.push_back(static_cast<char>(byte));
                }
        }
    }
    return output;
}

SourcePosition PositionOrDefault(const SourceBuffer& source,
                                 std::uint32_t offset) {
    return source.line_index().PositionAt(offset).value_or(SourcePosition{});
}

}  // namespace

std::string RenderDiagnosticText(const Diagnostic& diagnostic,
                                 const SourceBuffer& source) {
    const SourcePosition position = PositionOrDefault(source,
                                                      diagnostic.primary.begin);
    std::ostringstream output;
    output << source.file_name() << ':' << position.line << ':' << position.column
           << ": " << SeverityName(diagnostic.severity) << '[' << diagnostic.code
           << "] " << diagnostic.message_key;
    if (!diagnostic.field_path.empty()) {
        output << " (" << diagnostic.field_path << ')';
    }
    return output.str();
}

std::string RenderDiagnosticJson(const Diagnostic& diagnostic,
                                 const SourceBuffer& source) {
    const SourcePosition begin = PositionOrDefault(source,
                                                   diagnostic.primary.begin);
    const SourcePosition end = PositionOrDefault(source, diagnostic.primary.end);
    std::ostringstream output;
    output << "{\"file\":\"" << EscapeJson(source.file_name())
           << "\",\"code\":\"" << EscapeJson(diagnostic.code)
           << "\",\"severity\":\"" << SeverityName(diagnostic.severity)
           << "\",\"phase\":\"" << PhaseName(diagnostic.phase)
           << "\",\"message_key\":\"" << EscapeJson(diagnostic.message_key)
           << "\",\"field_path\":\"" << EscapeJson(diagnostic.field_path)
           << "\",\"span\":{\"begin\":" << diagnostic.primary.begin
           << ",\"end\":" << diagnostic.primary.end
           << ",\"begin_line\":" << begin.line
           << ",\"begin_column\":" << begin.column
           << ",\"end_line\":" << end.line
           << ",\"end_column\":" << end.column << "}}";
    return output.str();
}

}  // namespace pathguard::rules
