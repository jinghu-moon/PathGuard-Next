#include "hide_probe_contract.h"

#include <array>
#include <charconv>

namespace pathguard::hide_probe {
namespace {

std::string_view StatusName(ProbeStatus status) {
    switch (status) {
        case ProbeStatus::kObserved:
            return "observed";
        case ProbeStatus::kUnsupported:
            return "unsupported";
        case ProbeStatus::kSetupError:
            return "setup_error";
    }
    return "setup_error";
}

void AppendInteger(std::string* output, std::int64_t value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec == std::errc{}) {
        output->append(buffer.data(), result.ptr);
    }
}

}  // namespace

std::string EscapeJson(std::string_view value) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string output;
    output.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    output += "\\u00";
                    output.push_back(kHex[ch >> 4]);
                    output.push_back(kHex[ch & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return output;
}

std::string RenderObservationJson(const Observation& observation) {
    std::string output;
    output.reserve(192 + observation.path.size());
    output += "{\"schema\":";
    AppendInteger(&output, kProbeSchemaVersion);
    output += ",\"kind\":\"observation\",\"test\":\"";
    output += EscapeJson(observation.test);
    output += "\",\"surface\":\"";
    output += EscapeJson(observation.surface);
    output += "\",\"path\":\"";
    output += EscapeJson(observation.path);
    output += "\",\"return_value\":";
    AppendInteger(&output, observation.return_value);
    output += ",\"errno\":";
    AppendInteger(&output, observation.error_number);
    output += ",\"side_effect\":";
    output += observation.side_effect ? "true" : "false";
    output += ",\"status\":\"";
    output += StatusName(observation.status);
    output += "\"}";
    return output;
}

bool IsAllowedSandboxPath(std::string_view path) {
    constexpr std::array<std::string_view, 3> kPrefixes = {
        "/data/local/tmp/pathguard-hide-h0-",
        "/data/user/0/dev.pathguard.hideprobe/no_backup/pathguard-hide-h0-",
        "/data/data/dev.pathguard.hideprobe/no_backup/pathguard-hide-h0-",
    };
    std::string_view matched_prefix;
    for (const std::string_view prefix : kPrefixes) {
        if (path.starts_with(prefix) && path.size() > prefix.size()) {
            matched_prefix = prefix;
            break;
        }
    }
    if (matched_prefix.empty()) {
        return false;
    }
    if (path.find("..") != std::string_view::npos) {
        return false;
    }
    if (path.find('/', matched_prefix.size()) != std::string_view::npos) {
        return false;
    }
    for (const unsigned char ch : path) {
        if (ch < 0x20 || ch == 0x7f) {
            return false;
        }
    }
    return true;
}

}  // namespace pathguard::hide_probe
