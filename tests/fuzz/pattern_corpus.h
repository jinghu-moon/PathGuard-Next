#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pathguard::pattern::test {

inline constexpr std::string_view kPatternCorpusSchema =
    "pathguard.pattern-corpus.v1";

struct PatternCorpusEntry {
    std::string target;
    std::filesystem::path path;
    std::string sha256;
    std::string purpose;
    std::vector<std::uint8_t> bytes;
};

struct PatternCorpus {
    std::uint64_t random_seed = 0;
    std::vector<PatternCorpusEntry> entries;
};

inline std::vector<std::string> SplitTabs(std::string_view line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const std::size_t end = line.find('\t', begin);
        fields.emplace_back(line.substr(begin, end - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return fields;
}

inline PatternCorpus LoadPatternCorpus(const std::filesystem::path& source_dir) {
    const auto corpus_dir = source_dir / "tests" / "fuzz" / "seeds"
        / "pattern-v1";
    const auto manifest_path = corpus_dir / "manifest.txt";
    std::ifstream manifest(manifest_path, std::ios::binary);
    if (!manifest) throw std::runtime_error("cannot open Pattern corpus manifest");

    PatternCorpus corpus;
    std::string line;
    bool header_seen = false;
    while (std::getline(manifest, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.starts_with("# schema=")) {
            const std::size_t schema_begin = std::string("# schema=").size();
            const std::size_t seed_marker = line.find(" random_seed=");
            if (seed_marker == std::string::npos
                || line.substr(schema_begin, seed_marker - schema_begin)
                    != kPatternCorpusSchema) {
                throw std::runtime_error("unsupported Pattern corpus schema");
            }
            const std::string seed_text = line.substr(seed_marker + 13);
            std::string_view digits(seed_text);
            if (digits.starts_with("0x")) digits.remove_prefix(2);
            const auto parsed = std::from_chars(
                digits.data(), digits.data() + digits.size(),
                corpus.random_seed, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
                throw std::runtime_error("invalid Pattern corpus random seed");
            }
            header_seen = true;
            continue;
        }
        if (line.starts_with('#')) continue;

        const auto fields = SplitTabs(line);
        if (fields.size() != 4 || fields[2].size() != 64) {
            throw std::runtime_error("invalid Pattern corpus record");
        }
        PatternCorpusEntry entry;
        entry.target = fields[0];
        entry.path = corpus_dir / fields[1];
        entry.sha256 = fields[2];
        entry.purpose = fields[3];
        std::ifstream input(entry.path, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open Pattern corpus seed");
        entry.bytes.assign(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
        corpus.entries.push_back(std::move(entry));
    }
    if (!header_seen || corpus.entries.empty()) {
        throw std::runtime_error("empty Pattern corpus manifest");
    }
    return corpus;
}

}  // namespace pathguard::pattern::test
