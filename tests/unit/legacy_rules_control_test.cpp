#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/legacy_rules_control.h"
#include "test_assert.h"

namespace fs = std::filesystem;

namespace {

std::string ReadText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> ReadBytes(const fs::path& path) {
    const std::string text = ReadText(path);
    return {text.begin(), text.end()};
}

void CorruptPolicy(std::vector<std::uint8_t>* bytes) {
    assert(bytes != nullptr && !bytes->empty());
    (*bytes)[0] ^= 0xff;
}

}  // namespace

int main() {
    using pathguard::legacy_rules::CompileOptions;
    using pathguard::legacy_rules::CompilePerf;
    using pathguard::legacy_rules::CompileText;

    assert(std::string(pathguard::legacy_rules::kRulesFileName) == "rules.ini");
    assert(pathguard::legacy_rules::kReloadDebounce
           == std::chrono::milliseconds(150));
    assert(pathguard::legacy_rules::TemporaryPolicyPath("policy.bin")
           == fs::path("policy.bin.tmp"));
    assert(pathguard::legacy_rules::IsCandidateNew("new", "active", "rejected"));
    assert(!pathguard::legacy_rules::IsCandidateNew(
        "active", "active", "rejected"));
    assert(!pathguard::legacy_rules::IsCandidateNew(
        "rejected", "active", "rejected"));

    const fs::path fixture = fs::path(PATHGUARD_SOURCE_DIR) / "tests" / "fixtures"
        / "legacy-rules" / "valid-redirect.ini";
    const std::string valid = ReadText(fixture);
    const fs::path root = fs::temp_directory_path()
        / ("pathguard-rf0-control-"
           + std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch().count()));
    fs::create_directories(root);

    std::string error;
    CompilePerf perf;
    const fs::path policy = root / "policy.bin";
    assert(CompileText(valid, policy, &perf, &error));
    assert(perf.published);
    assert(!perf.unchanged);
    assert(fs::is_regular_file(policy));
    pathguard::PolicyDocument decoded;
    pathguard::ParseError parse_error;
    std::uint64_t generation = 0;
    assert(pathguard::DecodePolicy(
        ReadBytes(policy), &decoded, &generation, &parse_error));
    assert(generation != 0);

    assert(CompileText(valid, policy, &perf, &error));
    assert(perf.unchanged);
    assert(!perf.published);

    assert(!CompileText("schema = 1\n", policy, &perf, &error));
    assert(error.find("line 1:") == 0);

    const fs::path corrupt_policy = root / "corrupt.bin";
    CompileOptions corrupt;
    corrupt.before_verify = CorruptPolicy;
    assert(!CompileText(valid, corrupt_policy, &perf, &error, corrupt));
    assert(error.find("encoder self-check failed:") == 0);
    assert(!fs::exists(corrupt_policy));

    const fs::path blocked_temp_policy = root / "blocked-temp.bin";
    fs::create_directory(
        pathguard::legacy_rules::TemporaryPolicyPath(blocked_temp_policy));
    assert(!CompileText(valid, blocked_temp_policy, &perf, &error));
    assert(error == "cannot create policy.bin.tmp");

    const fs::path blocked_replace_policy = root / "blocked-replace.bin";
    fs::create_directory(blocked_replace_policy);
    assert(!CompileText(valid, blocked_replace_policy, &perf, &error));
    assert(error.find("cannot atomically publish policy.bin:") == 0);

    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    return 0;
}
