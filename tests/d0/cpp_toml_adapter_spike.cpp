#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#define TOML_ENABLE_UNRELEASED_FEATURES 0
#include "toml.hpp"

#include "pathguard/binary.h"
#include "pathguard/policy.h"
#include "policy_v5_golden_fixture.h"
#include "test_assert.h"

namespace {

struct Span {
    std::size_t begin;
    std::size_t end;
};

struct GoldenRow {
    std::string generated;
    Span table;
    Span source;
    Span target;
};

std::size_t Utf8Width(unsigned char value) {
    if ((value & 0x80U) == 0) return 1;
    if ((value & 0xe0U) == 0xc0U) return 2;
    if ((value & 0xf0U) == 0xe0U) return 3;
    if ((value & 0xf8U) == 0xf0U) return 4;
    return 1;
}

std::size_t PositionToByte(std::string_view source,
                           toml::source_position position) {
    std::size_t offset = 0;
    std::size_t line = 1;
    while (offset < source.size() && line < position.line) {
        if (source[offset++] == '\n') ++line;
    }
    std::size_t column = 1;
    while (offset < source.size() && column < position.column
           && source[offset] != '\n') {
        if (source[offset] == '\r') {
            ++offset;
        } else {
            offset += Utf8Width(static_cast<unsigned char>(source[offset]));
            ++column;
        }
    }
    return offset;
}

Span RegionSpan(std::string_view source, const toml::source_region& region) {
    return {PositionToByte(source, region.begin),
            PositionToByte(source, region.end)};
}

Span ParseSpan(std::string_view value) {
    const std::size_t colon = value.find(':');
    assert(colon != std::string_view::npos);
    return {static_cast<std::size_t>(std::stoull(std::string(value.substr(0, colon)))),
            static_cast<std::size_t>(std::stoull(std::string(value.substr(colon + 1))))};
}

std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '|')) fields.push_back(field);
    return fields;
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void VerifyCase(const std::string& name, const std::filesystem::path& root,
                const std::vector<GoldenRow>& rows) {
    const std::string source = ReadAll(root / rows.front().generated);
    toml::parse_result parsed = toml::parse(
        std::string_view(source), std::string_view(name));
    assert(parsed);
    toml::table& document_root = parsed.table();
    toml::array* redirect =
        document_root["apps"]["com.example"]["redirect"].as_array();
    assert(redirect != nullptr && redirect->size() == rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        toml::table* table = redirect->get(index)->as_table();
        assert(table != nullptr);
        const Span table_span = RegionSpan(source, table->source());
        const Span source_span = RegionSpan(source, table->get("from")->source());
        const Span target_span = RegionSpan(source, table->get("to")->source());
        assert(table_span.begin == rows[index].table.begin);
        assert(table_span.end == rows[index].table.end);
        assert(source_span.begin == rows[index].source.begin);
        assert(source_span.end == rows[index].source.end);
        assert(target_span.begin == rows[index].target.begin);
        assert(target_span.end == rows[index].target.end);
    }
    std::cout << name << " nodes=" << rows.size() << " spans=ok\n";
}

pathguard::PolicyDocument DecodeGoldenDocument(const toml::table& root) {
    assert(root["format"].value<std::int64_t>() == 1);
    pathguard::PolicyDocument document;
    document.schema = 2;
    document.failure_mode = pathguard::FailureMode::kOpen;
    document.allow_legacy_string_bind = root["compatibility"]
        ["allow_legacy_mount"].value_or(false);

    pathguard::AppPolicy app;
    app.package = "org.localsend.localsend_app";
    const toml::table* app_table = root["apps"][app.package].as_table();
    assert(app_table != nullptr);
    app.provider_compat = (*app_table)["file_picker"].value_or(false)
        ? pathguard::ProviderCompat::kVirtualize
        : pathguard::ProviderCompat::kOff;
    app.users.clear();
    const toml::array* users = (*app_table)["users"].as_array();
    assert(users != nullptr);
    for (const toml::node& user : *users) {
        app.users.push_back(std::to_string(user.value<std::int64_t>().value()));
    }
    app.processes.clear();
    const toml::array* processes = (*app_table)["processes"].as_array();
    assert(processes != nullptr);
    for (const toml::node& process : *processes) {
        app.processes.push_back(process.value<std::string>().value());
    }
    const toml::node* redirect = app_table->get("redirect");
    assert(redirect != nullptr);
    if (const toml::array* values = redirect->as_array()) {
        for (const toml::node& value : *values) {
            const toml::table* mapping = value.as_table();
            assert(mapping != nullptr);
            app.mounts.push_back({
                pathguard::MountAction::kRedirect,
                mapping->get("from")->value<std::string>().value(),
                mapping->get("to")->value<std::string>().value(),
                0,
                0,
                0,
            });
        }
    } else {
        const toml::table* mapping = redirect->as_table();
        assert(mapping != nullptr);
        mapping->for_each([&](const toml::key& visible, const toml::node& target) {
            app.mounts.push_back({
                pathguard::MountAction::kRedirect,
                std::string(visible.str()),
                target.value<std::string>().value(),
                0,
                0,
                0,
            });
        });
    }
    document.apps.push_back(std::move(app));
    return document;
}

void VerifyPolicyGolden(const std::filesystem::path& repository_root,
                        const char* name) {
    const std::string source = ReadAll(
        repository_root / "tests" / "golden" / "rules" / name);
    toml::parse_result parsed = toml::parse(
        std::string_view(source), std::string_view(name));
    assert(parsed);
    const pathguard::PolicyDocument document = DecodeGoldenDocument(parsed.table());
    pathguard::ParseError error;
    std::vector<std::uint8_t> bytes;
    assert(pathguard::EncodePolicy(document, &bytes, &error));
    const PolicyV5GoldenFixture fixture = LoadPolicyV5GoldenFixture();
    assert(bytes.size() * 2 == fixture.expected_hex.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const std::string pair = fixture.expected_hex.substr(index * 2, 2);
        assert(bytes[index] == static_cast<std::uint8_t>(
            std::stoul(pair, nullptr, 16)));
    }
    pathguard::PolicyDocument verified;
    std::uint64_t generation = 0;
    assert(pathguard::DecodePolicy(bytes, &verified, &generation, &error));
    assert(generation == fixture.expected_content_generation);
}

void VerifyParseErrorGolden(const std::filesystem::path& golden_root) {
    std::ifstream manifest(golden_root / "parse-error-map.tsv");
    assert(manifest);
    std::size_t count = 0;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string> fields = Split(line);
        assert(fields.size() == 6);
        const std::string source = ReadAll(golden_root / fields[2]);
        toml::parse_result parsed = toml::parse(
            std::string_view(source), std::string_view(fields[0]));
        assert(!parsed);
        const Span error_span = RegionSpan(source, parsed.error().source());
        const Span segment = ParseSpan(fields[4]);
        assert(error_span.begin < segment.end && error_span.end > segment.begin);
        const Span original = ParseSpan(fields[5]);
        assert(original.begin < original.end);
        ++count;
    }
    assert(count == 3);
}

std::string VerifyBindingCase(const std::filesystem::path& golden_root,
                              const std::vector<std::string>& fields) {
    const std::string source = ReadAll(golden_root / fields[1]);
    toml::parse_result parsed = toml::parse(
        std::string_view(source), std::string_view(fields[0]));
    assert(parsed);
    toml::array* array = parsed["apps"]["com.example"][fields[2]].as_array();
    assert(array != nullptr);
    std::vector<Span> nodes;
    for (toml::node& node : *array) {
        toml::table* table = node.as_table();
        if (table != nullptr) nodes.push_back(RegionSpan(source, table->source()));
    }

    std::vector<Span> generated;
    if (fields[3] != "-") {
        std::stringstream spans(fields[3]);
        std::string value;
        while (std::getline(spans, value, ',')) generated.push_back(ParseSpan(value));
    }
    for (std::size_t index = 0; index < generated.size(); ++index) {
        for (std::size_t other = index + 1; other < generated.size(); ++other) {
            if (generated[index].begin == generated[other].begin
                && generated[index].end == generated[other].end) {
                return "PG-DESUGAR-INTERNAL";
            }
        }
        const std::size_t matches = static_cast<std::size_t>(std::count_if(
            nodes.begin(), nodes.end(), [&](const Span& node) {
                return node.begin == generated[index].begin
                    && node.end == generated[index].end;
            }));
        if (matches != 1) return "PG-DESUGAR-INTERNAL";
    }
    if (fields[2] != "redirect" && !generated.empty()) {
        return "PG-RULE-ARROW-SCOPE";
    }
    if (fields[2] == "redirect" && nodes.size() != generated.size()) {
        return "PG-REDIRECT-SYNTAX";
    }
    return "OK";
}

void VerifyBindingGolden(const std::filesystem::path& golden_root) {
    std::ifstream manifest(golden_root / "binding-cases.tsv");
    assert(manifest);
    std::size_t count = 0;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string> fields = Split(line);
        assert(fields.size() == 5);
        assert(VerifyBindingCase(golden_root, fields) == fields[4]);
        ++count;
    }
    assert(count == 5);
}

}  // namespace

int main() {
    const std::filesystem::path repository_root(PATHGUARD_SOURCE_DIR);
    const std::filesystem::path golden_root =
        repository_root / "tests" / "golden"
        / "rules" / "d0";
    std::ifstream manifest(golden_root / "binder-neutral.tsv");
    assert(manifest);
    std::map<std::string, std::vector<GoldenRow>> cases;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string> fields = Split(line);
        assert(fields.size() == 12 && fields[4] == "redirect");
        cases[fields[0]].push_back({fields[3], ParseSpan(fields[9]),
                                    ParseSpan(fields[10]), ParseSpan(fields[11])});
    }
    assert(cases.size() == 6);
    for (const auto& item : cases) {
        VerifyCase(item.first, golden_root, item.second);
    }

    toml::parse_result invalid = toml::parse(
        std::string_view("value = {\n  nested = true,\n}\n"),
        std::string_view("toml-1.1-only"));
    assert(!invalid);
    assert(invalid.error().source().begin.line > 0);
    VerifyPolicyGolden(repository_root, "policy-v5-strict-inline.toml");
    VerifyPolicyGolden(repository_root, "policy-v5-strict-mapping.toml");
    VerifyParseErrorGolden(golden_root);
    VerifyBindingGolden(golden_root);
    return 0;
}
