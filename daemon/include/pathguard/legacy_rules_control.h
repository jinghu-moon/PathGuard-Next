#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pathguard::legacy_rules {

inline constexpr char kRulesFileName[] = "rules.ini";
inline constexpr std::chrono::milliseconds kReloadDebounce{150};

struct CompilePerf {
    std::uint64_t parse_ns = 0;
    std::uint64_t validate_ns = 0;
    std::uint64_t encode_ns = 0;
    std::uint64_t compare_ns = 0;
    std::uint64_t publish_ns = 0;
    bool unchanged = false;
    bool published = false;
};

struct CompileOptions {
    void (*before_verify)(std::vector<std::uint8_t>* bytes) = nullptr;
};

std::filesystem::path TemporaryPolicyPath(const std::filesystem::path& output);

bool IsCandidateNew(std::string_view candidate, std::string_view active,
                    std::string_view rejected);

bool CompileText(std::string_view text, const std::filesystem::path& output,
                 CompilePerf* perf, std::string* error,
                 CompileOptions options = {});

bool CompileFile(const std::filesystem::path& config,
                 const std::filesystem::path& output, CompilePerf* perf,
                 std::uint64_t* read_ns, std::string* error,
                 CompileOptions options = {});

}  // namespace pathguard::legacy_rules
