#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "pathguard/rules/desugarer.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

namespace fs = std::filesystem;
using pathguard::rules::ByteSpan;
using pathguard::rules::DesugarResult;
using pathguard::rules::Diagnostic;
using pathguard::rules::GeneratedRedirect;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

std::string ReadAll(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::vector<std::string> Split(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, delimiter)) fields.push_back(field);
    return fields;
}

ByteSpan ParseSpan(const std::string& value) {
    const std::size_t colon = value.find(':');
    assert(colon != std::string::npos);
    return {static_cast<std::uint32_t>(std::stoul(value.substr(0, colon))),
            static_cast<std::uint32_t>(std::stoul(value.substr(colon + 1)))};
}

SourceBuffer MakeSource(const fs::path& path) {
    Diagnostic error;
    auto source = SourceBuffer::Create(path.string(), ReadAll(path), RulesLimits{},
                                       &error);
    assert(source.has_value());
    return std::move(*source);
}

}  // namespace

int main() {
    const fs::path root = fs::path(PATHGUARD_SOURCE_DIR) / "tests" / "golden"
        / "rules" / "d0";
    std::ifstream manifest(root / "binder-neutral.tsv");
    assert(manifest);
    std::string active_case;
    SourceBuffer* unused = nullptr;
    (void)unused;
    std::string line;
    std::size_t rows = 0;
    std::string cached_name;
    std::vector<std::vector<std::string>> case_rows;
    auto verify_case = [&](const std::vector<std::vector<std::string>>& values) {
        if (values.empty()) return;
        SourceBuffer source = MakeSource(root / values.front()[2]);
        const DesugarResult result =
            pathguard::rules::DesugarRulesSource(source, RulesLimits{});
        assert(result.ok() && result.rewritten());
        assert(result.parser_input(source) == ReadAll(root / values.front()[3]));
        assert(result.redirects.size() == values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto& fields = values[index];
            const GeneratedRedirect& redirect = result.redirects[index];
            assert(redirect.original_rule == ParseSpan(fields[5]));
            assert(redirect.original_source == ParseSpan(fields[6]));
            assert(redirect.original_arrow == ParseSpan(fields[7]));
            assert(redirect.original_target == ParseSpan(fields[8]));
            assert(redirect.generated_table == ParseSpan(fields[9]));
            assert(redirect.generated_source == ParseSpan(fields[10]));
            assert(redirect.generated_target == ParseSpan(fields[11]));
        }
        assert(result.rewrite_map.query_count() == 0);
    };
    while (std::getline(manifest, line)) {
        if (line.empty() || line.front() == '#') continue;
        auto fields = Split(line, '|');
        assert(fields.size() == 12);
        if (!cached_name.empty() && fields[0] != cached_name) {
            verify_case(case_rows);
            case_rows.clear();
        }
        cached_name = fields[0];
        case_rows.push_back(std::move(fields));
        ++rows;
    }
    verify_case(case_rows);
    assert(rows == 7);

    std::ifstream error_manifest(root / "parse-error-map.tsv");
    assert(error_manifest);
    std::size_t error_rows = 0;
    while (std::getline(error_manifest, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto fields = Split(line, '|');
        assert(fields.size() == 6);
        SourceBuffer source = MakeSource(root / fields[1]);
        const DesugarResult result =
            pathguard::rules::DesugarRulesSource(source, RulesLimits{});
        assert(result.ok());
        assert(result.parser_input(source).size() == ReadAll(root / fields[2]).size());
        const auto mapped = result.rewrite_map.MapGeneratedSpan(
            ParseSpan(fields[4]));
        assert(mapped.has_value());
        assert(*mapped == ParseSpan(fields[5]));
        const auto begin = source.line_index().PositionAt(mapped->begin);
        const auto end = source.line_index().PositionAt(mapped->end);
        assert(begin.has_value() && end.has_value());
        ++error_rows;
    }
    assert(error_rows == 3);
    return 0;
}
