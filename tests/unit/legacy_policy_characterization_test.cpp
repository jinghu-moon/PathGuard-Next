#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy.h"
#include "pathguard/validation.h"
#include "test_assert.h"

namespace fs = std::filesystem;

namespace {

struct Case {
    std::string file;
    std::string classification;
    std::string parse;
    std::size_t parse_error_line = 0;
    std::string validate;
    std::string encode;
    int schema = 0;
    std::size_t apps = 0;
    std::size_t mounts = 0;
    std::size_t events = 0;
    std::size_t binary_bytes = 0;
    std::uint64_t content_generation = 0;
};

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::vector<std::string> Split(const std::string& line, char separator) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const std::size_t end = line.find(separator, begin);
        fields.push_back(line.substr(
            begin, end == std::string::npos ? line.size() - begin : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return fields;
}

std::vector<Case> LoadManifest(const fs::path& path) {
    std::ifstream input(path);
    assert(input);
    std::vector<Case> cases;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string> fields = Split(line, '|');
        assert(fields.size() == 12);
        assert(fields[10] != "PENDING");
        assert(fields[11] != "PENDING");
        Case item;
        item.file = fields[0];
        item.classification = fields[1];
        item.parse = fields[2];
        item.parse_error_line = static_cast<std::size_t>(std::stoull(fields[3]));
        item.validate = fields[4];
        item.encode = fields[5];
        item.schema = std::stoi(fields[6]);
        item.apps = static_cast<std::size_t>(std::stoull(fields[7]));
        item.mounts = static_cast<std::size_t>(std::stoull(fields[8]));
        item.events = static_cast<std::size_t>(std::stoull(fields[9]));
        item.binary_bytes = static_cast<std::size_t>(std::stoull(fields[10]));
        item.content_generation = std::stoull(fields[11]);
        cases.push_back(std::move(item));
    }
    return cases;
}

}  // namespace

int main() {
    const fs::path fixture_dir =
        fs::path(PATHGUARD_SOURCE_DIR) / "tests" / "fixtures" / "legacy-rules";
    const std::vector<Case> cases = LoadManifest(fixture_dir / "manifest.tsv");
    assert(!cases.empty());

    std::unordered_set<std::string> registered;
    std::size_t preserve_count = 0;
    std::size_t replace_count = 0;
    std::size_t compile_gated_count = 0;
    for (const Case& item : cases) {
        assert(registered.insert(item.file).second);
        if (item.classification == "preserve") ++preserve_count;
        else if (item.classification == "replace") ++replace_count;
        else if (item.classification == "compile-gated") ++compile_gated_count;
        else assert(false);

        pathguard::PolicyDocument document;
        pathguard::ParseError error;
        const bool parsed = pathguard::ParseRulesIni(
            Read(fixture_dir / item.file), &document, &error);
        assert(parsed == (item.parse == "ok"));
        if (!parsed) {
            assert(error.line == item.parse_error_line);
            continue;
        }

        assert(document.schema == item.schema);
        assert(document.apps.size() == item.apps);
        std::size_t mount_count = 0;
        std::size_t event_count = 0;
        bool valid = true;
        for (pathguard::AppPolicy& app : document.apps) {
            mount_count += app.mounts.size();
            event_count += app.events.size();
            if (!pathguard::ValidatePolicy(&app, &error)) valid = false;
        }
        assert(mount_count == item.mounts);
        assert(event_count == item.events);
        assert(item.validate != "skip");
        assert(valid == (item.validate == "ok"));

        std::vector<std::uint8_t> bytes;
        const bool encoded = pathguard::EncodePolicy(document, &bytes, &error);
        assert(item.encode != "skip");
        assert(encoded == (item.encode == "ok"));
        if (encoded) {
            assert(bytes.size() == item.binary_bytes);
            assert(pathguard::ComputeContentGeneration(document)
                   == item.content_generation);
        }
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(fixture_dir)) {
        if (entry.path().extension() == ".ini") {
            assert(registered.contains(entry.path().filename().string()));
        }
    }
    assert(preserve_count > 0);
    assert(replace_count > 0);
    assert(compile_gated_count > 0);
    return 0;
}
