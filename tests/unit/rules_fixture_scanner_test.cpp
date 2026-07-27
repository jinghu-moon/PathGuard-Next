#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "pathguard/rules/arrow_scanner.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

namespace fs = std::filesystem;
using pathguard::rules::ArrowScanResult;
using pathguard::rules::Diagnostic;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

std::string ReadAll(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) fields.push_back(field);
    return fields;
}

ArrowScanResult Scan(std::string bytes) {
    Diagnostic error;
    const RulesLimits limits;
    auto source = SourceBuffer::Create("fixture.toml", std::move(bytes),
                                       limits, &error);
    assert(source.has_value());
    ArrowScanResult result = pathguard::rules::ScanArrowCandidates(*source, limits);
    assert(result.max_frame_depth <= limits.max_container_depth);
    assert(result.significant_tokens <= limits.max_tokens_or_nodes);
    for (const Diagnostic& diagnostic : result.diagnostics) {
        assert(source->IsValidSpan(diagnostic.primary));
    }
    for (const auto& candidate : result.candidates) {
        assert(source->IsValidSpan(candidate.rule));
        assert(source->IsValidSpan(candidate.source));
        assert(source->IsValidSpan(candidate.arrow));
        assert(source->IsValidSpan(candidate.target));
    }
    return result;
}

}  // namespace

int main() {
    const fs::path root = fs::path(PATHGUARD_SOURCE_DIR) / "tests" / "fixtures"
        / "toml-test-v2.2.0";
    std::ifstream manifest(root / "manifest.txt");
    assert(manifest);
    std::size_t valid = 0;
    std::size_t invalid = 0;
    std::size_t invalid_encoding = 0;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto fields = Split(line);
        assert(fields.size() == 3);
        std::string bytes = ReadAll(root / fields[1]);
        if (fields[1].rfind("valid/", 0) == 0) {
            bytes += "\n__pg_sentinel = \"inside -> string # [ ]\"\n"
                     "# comment sentinel: \"A\" -> \"B\"\n";
            const ArrowScanResult result = Scan(std::move(bytes));
            assert(result.diagnostics.empty());
            assert(result.candidates.empty());
            assert(result.bytes_consumed != 0);
            ++valid;
        } else {
            const ArrowScanResult result = Scan(std::move(bytes));
            assert(result.candidates.empty());
            if (fields[1].rfind("invalid/encoding/", 0) == 0) {
                ++invalid_encoding;
            }
            ++invalid;
        }
    }
    assert(valid == 39);
    assert(invalid == 98);
    assert(invalid_encoding == 15);
    return 0;
}
