#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#define TOML_ENABLE_UNRELEASED_FEATURES 0
#include "toml.hpp"

#include "pathguard/binary.h"
#include "pathguard/policy.h"
#include "test_assert.h"

namespace {

using Clock = std::chrono::steady_clock;

std::string BuildSource(std::size_t rules) {
    std::string output =
        "format = 1\n[compatibility]\nallow_legacy_mount = false\n"
        "[apps.\"org.example.benchmark\"]\nenabled = true\nusers = [0]\n"
        "processes = [\"*\"]\nfile_picker = false\nredirect = [\n";
    for (std::size_t index = 0; index < rules; ++index) {
        std::ostringstream item;
        item << "{ from = \"Visible/" << std::setw(4) << std::setfill('0') << index
             << "\", to = \"Backing/" << std::setw(4) << std::setfill('0') << index
             << "\" },\n";
        output += item.str();
    }
    output += "]\n";
    return output;
}

pathguard::PolicyDocument Decode(toml::table& root) {
    pathguard::PolicyDocument document;
    document.schema = 2;
    document.failure_mode = pathguard::FailureMode::kOpen;
    pathguard::AppPolicy app;
    app.package = "org.example.benchmark";
    app.users = {"0"};
    app.processes = {"*"};
    toml::array* redirects = root["apps"][app.package]["redirect"].as_array();
    assert(redirects != nullptr);
    app.mounts.reserve(redirects->size());
    for (toml::node& redirect : *redirects) {
        toml::table* mapping = redirect.as_table();
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
    document.apps.push_back(std::move(app));
    return document;
}

std::uint64_t Median(std::vector<std::uint64_t> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 3);
    const std::size_t rules = std::stoull(argv[1]);
    const std::size_t iterations = std::stoull(argv[2]);
    const std::string source = BuildSource(rules);
    pathguard::ParseError error;
    for (int index = 0; index < 5; ++index) {
        toml::parse_result parsed = toml::parse(std::string_view(source));
        assert(parsed);
        const pathguard::PolicyDocument document = Decode(parsed.table());
        std::vector<std::uint8_t> bytes;
        assert(pathguard::EncodePolicy(document, &bytes, &error));
    }
    std::vector<std::uint64_t> parse_times;
    std::vector<std::uint64_t> compile_times;
    parse_times.reserve(iterations);
    compile_times.reserve(iterations);
    for (std::size_t index = 0; index < iterations; ++index) {
        auto started = Clock::now();
        toml::parse_result parsed = toml::parse(std::string_view(source));
        assert(parsed);
        parse_times.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - started).count()));

        started = Clock::now();
        parsed = toml::parse(std::string_view(source));
        assert(parsed);
        const pathguard::PolicyDocument document = Decode(parsed.table());
        std::vector<std::uint8_t> bytes;
        assert(pathguard::EncodePolicy(document, &bytes, &error));
        compile_times.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - started).count()));
    }
    std::cout << "rules=" << rules << " bytes=" << source.size()
              << " iterations=" << iterations
              << " parser_median_us=" << Median(parse_times) / 1000
              << " full_median_us=" << Median(compile_times) / 1000 << '\n';
    return 0;
}
