#include "pathguard/legacy_rules_control.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "pathguard/binary.h"
#include "pathguard/policy.h"
#include "pathguard/validation.h"

namespace pathguard::legacy_rules {
namespace {

using PerfClock = std::chrono::steady_clock;

std::uint64_t NowNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            PerfClock::now().time_since_epoch())
            .count());
}

std::uint64_t ElapsedNs(std::uint64_t start) {
    const std::uint64_t now = NowNs();
    return now >= start ? now - start : 0;
}

bool ReadAll(const std::filesystem::path& path, std::string* output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    *output = std::string(std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>());
    return true;
}

bool AtomicReplace(const std::filesystem::path& temporary,
                   const std::filesystem::path& output, std::string* error) {
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), output.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        != 0) {
        return true;
    }
    if (error != nullptr) {
        *error = "cannot atomically publish policy.bin: win32="
            + std::to_string(GetLastError());
    }
    return false;
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary, output, rename_error);
    if (!rename_error) return true;
    if (error != nullptr) {
        *error = "cannot atomically publish policy.bin: "
            + rename_error.message();
    }
    return false;
#endif
}

}  // namespace

std::filesystem::path TemporaryPolicyPath(
    const std::filesystem::path& output) {
    return std::filesystem::path(output.string() + ".tmp");
}

bool IsCandidateNew(std::string_view candidate, std::string_view active,
                    std::string_view rejected) {
    return candidate != active && candidate != rejected;
}

bool CompileText(std::string_view text, const std::filesystem::path& output,
                 CompilePerf* perf, std::string* error,
                 CompileOptions options) {
    if (perf == nullptr || error == nullptr) return false;
    *perf = {};
    PolicyDocument document;
    ParseError parse_error;
    const std::uint64_t parse_started = NowNs();
    if (!ParseRulesIni(text, &document, &parse_error)) {
        perf->parse_ns = ElapsedNs(parse_started);
        *error = "line " + std::to_string(parse_error.line) + ": "
            + parse_error.message;
        return false;
    }
    perf->parse_ns = ElapsedNs(parse_started);

    const std::uint64_t validate_started = NowNs();
    for (AppPolicy& app : document.apps) {
        if (!ValidatePolicy(&app, &parse_error)) {
            perf->validate_ns = ElapsedNs(validate_started);
            *error = "line " + std::to_string(parse_error.line) + ": "
                + parse_error.message;
            return false;
        }
    }
    perf->validate_ns = ElapsedNs(validate_started);

    std::vector<std::uint8_t> bytes;
    const std::uint64_t encode_started = NowNs();
    if (!EncodePolicy(document, &bytes, &parse_error)) {
        perf->encode_ns = ElapsedNs(encode_started);
        *error = parse_error.message;
        return false;
    }
    if (options.before_verify != nullptr) options.before_verify(&bytes);

    PolicyDocument verified;
    std::uint64_t verified_generation = 0;
    if (!DecodePolicy(bytes, &verified, &verified_generation, &parse_error)
        || verified_generation == 0) {
        perf->encode_ns = ElapsedNs(encode_started);
        *error = "encoder self-check failed: " + parse_error.message;
        return false;
    }
    perf->encode_ns = ElapsedNs(encode_started);

    const std::uint64_t compare_started = NowNs();
    std::string current;
    if (ReadAll(output, &current) && current.size() == bytes.size()
        && std::memcmp(current.data(), bytes.data(), bytes.size()) == 0) {
        perf->compare_ns = ElapsedNs(compare_started);
        perf->unchanged = true;
        return true;
    }
    perf->compare_ns = ElapsedNs(compare_started);

    const std::uint64_t publish_started = NowNs();
    std::filesystem::create_directories(output.parent_path());
    const std::filesystem::path temporary = TemporaryPolicyPath(output);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            perf->publish_ns = ElapsedNs(publish_started);
            *error = "cannot create policy.bin.tmp";
            return false;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    const bool replaced = AtomicReplace(temporary, output, error);
    perf->publish_ns = ElapsedNs(publish_started);
    if (!replaced) return false;
    perf->published = true;
    return true;
}

bool CompileFile(const std::filesystem::path& config,
                 const std::filesystem::path& output, CompilePerf* perf,
                 std::uint64_t* read_ns, std::string* error,
                 CompileOptions options) {
    const std::uint64_t read_started = NowNs();
    std::string text;
    if (!ReadAll(config, &text)) {
        if (read_ns != nullptr) *read_ns = ElapsedNs(read_started);
        if (error != nullptr) *error = "cannot read rules.ini";
        return false;
    }
    if (read_ns != nullptr) *read_ns = ElapsedNs(read_started);
    return CompileText(text, output, perf, error, options);
}

}  // namespace pathguard::legacy_rules
