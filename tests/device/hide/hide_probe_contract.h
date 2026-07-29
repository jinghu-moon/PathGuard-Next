#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace pathguard::hide_probe {

inline constexpr std::uint32_t kProbeSchemaVersion = 1;

enum class ProbeStatus : std::uint8_t {
    kObserved = 0,
    kUnsupported = 1,
    kSetupError = 2,
};

struct Observation {
    std::string_view test;
    std::string_view surface;
    std::string_view path;
    std::int64_t return_value = 0;
    int error_number = 0;
    bool side_effect = false;
    ProbeStatus status = ProbeStatus::kObserved;
};

std::string EscapeJson(std::string_view value);
std::string RenderObservationJson(const Observation& observation);
bool IsAllowedSandboxPath(std::string_view path);

}  // namespace pathguard::hide_probe
