#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pathguard {

enum class FailureMode : std::uint8_t { kOpen = 0, kClosed = 1 };
enum class MountAction : std::uint8_t {
    kDeny = 0,
    kRedirect = 1,
    kRestore = 2,
    kIsolateRoot = 3,
};
enum class EventAction : std::uint8_t { kObserve = 0, kExport = 1 };
enum class MediaCompat : std::uint8_t { kOff = 0, kHideDenied = 1 };

inline constexpr std::uint32_t kEventModeCopy = 0;
inline constexpr std::uint32_t kEventModeMove = 1;
inline constexpr std::uint32_t kEventModeTrash = 2;
inline constexpr std::uint32_t kEventModeMask = 0x3;
inline constexpr std::uint32_t kEventMediaScan = 1u << 2;

struct LogicalMountRule {
    MountAction action = MountAction::kDeny;
    std::string visible_path;
    std::string backing_path;
    std::uint16_t depth = 0;
    std::uint16_t flags = 0;
    std::size_t line = 0;
};

struct EventRule {
    EventAction action = EventAction::kObserve;
    std::string source_path;
    std::string target_path;
    std::uint32_t options = 0;
    std::size_t line = 0;
};

struct AppPolicy {
    std::string package;
    MediaCompat media_compat = MediaCompat::kOff;
    std::vector<std::string> users = {"*"};
    std::vector<std::string> processes = {"*"};
    std::vector<LogicalMountRule> mounts;
    std::vector<EventRule> events;
};

struct PolicyDocument {
    int schema = 0;
    FailureMode failure_mode = FailureMode::kOpen;
    std::vector<AppPolicy> apps;
};

struct ParseError {
    std::size_t line = 0;
    std::string message;
};

bool ParseRulesIni(std::string_view text, PolicyDocument* document, ParseError* error);

}  // namespace pathguard
