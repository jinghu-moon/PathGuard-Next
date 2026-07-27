#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#define TOML_ENABLE_UNRELEASED_FEATURES 0
#include "toml.hpp"

#include "pathguard/binary.h"
#include "pathguard/policy.h"

extern "C" int pg_cpp_d0_compile(const char* source, std::size_t source_len,
                                 std::uint8_t* output,
                                 std::size_t* output_len) {
    if (source == nullptr || output_len == nullptr) return 1;
    toml::parse_result parsed = toml::parse(std::string_view(source, source_len));
    if (!parsed || parsed["format"].value<std::int64_t>() != 1) return 2;

    pathguard::PolicyDocument document;
    document.schema = 2;
    document.failure_mode = pathguard::FailureMode::kOpen;
    document.allow_legacy_string_bind = parsed["compatibility"]
        ["allow_legacy_mount"].value_or(false);
    pathguard::AppPolicy app;
    app.package = "org.localsend.localsend_app";
    toml::table* app_table = parsed["apps"][app.package].as_table();
    if (app_table == nullptr) return 3;
    app.provider_compat = (*app_table)["file_picker"].value_or(false)
        ? pathguard::ProviderCompat::kVirtualize
        : pathguard::ProviderCompat::kOff;
    app.users.clear();
    toml::array* users = (*app_table)["users"].as_array();
    toml::array* processes = (*app_table)["processes"].as_array();
    toml::array* redirects = (*app_table)["redirect"].as_array();
    if (users == nullptr || processes == nullptr || redirects == nullptr) return 4;
    for (toml::node& user : *users) {
        const auto value = user.value<std::int64_t>();
        if (!value) return 5;
        app.users.push_back(std::to_string(*value));
    }
    app.processes.clear();
    for (toml::node& process : *processes) {
        const auto value = process.value<std::string>();
        if (!value) return 6;
        app.processes.push_back(*value);
    }
    for (toml::node& redirect : *redirects) {
        toml::table* mapping = redirect.as_table();
        if (mapping == nullptr) return 7;
        const auto visible = (*mapping)["from"].value<std::string>();
        const auto backing = (*mapping)["to"].value<std::string>();
        if (!visible || !backing) return 8;
        app.mounts.push_back({pathguard::MountAction::kRedirect,
                              *visible, *backing, 0, 0, 0});
    }
    document.apps.push_back(std::move(app));
    pathguard::ParseError error;
    std::vector<std::uint8_t> bytes;
    if (!pathguard::EncodePolicy(document, &bytes, &error)) return 9;
    if (output == nullptr || *output_len < bytes.size()) {
        *output_len = bytes.size();
        return output == nullptr ? 0 : 10;
    }
    std::memcpy(output, bytes.data(), bytes.size());
    *output_len = bytes.size();
    return 0;
}
