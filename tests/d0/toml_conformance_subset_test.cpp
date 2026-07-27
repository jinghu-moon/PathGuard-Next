#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#define TOML_ENABLE_UNRELEASED_FEATURES 0
#include "toml.hpp"

#include "test_assert.h"

namespace fs = std::filesystem;

namespace {

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

}  // namespace

int main() {
    const fs::path root = fs::path(PATHGUARD_SOURCE_DIR) / "tests" / "fixtures"
        / "toml-test-v2.2.0";
    std::ifstream manifest(root / "manifest.txt");
    assert(manifest);
    std::size_t valid = 0;
    std::size_t invalid = 0;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string> fields = Split(line);
        assert(fields.size() == 3);
        const bool expected_valid = fields[1].rfind("valid/", 0) == 0;
        const std::string source = ReadAll(root / fields[1]);
        const toml::parse_result parsed = toml::parse(
            std::string_view(source), std::string_view(fields[1]));
        assert(static_cast<bool>(parsed) == expected_valid);
        if (expected_valid) ++valid;
        else ++invalid;
    }
    assert(valid == 39);
    assert(invalid == 98);

    const toml::parse_result toml_1_1 = toml::parse(
        std::string_view("value = {\n  nested = true,\n}\n"),
        std::string_view("TOML-1.1-only"));
    assert(!toml_1_1);
    return 0;
}
