#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include "pathguard/policy.h"
#include "test_assert.h"

struct PolicyV5GoldenFixture {
    pathguard::PolicyDocument document;
    std::size_t expected_file_size = 0;
    std::uint64_t expected_content_generation = 0;
    std::uint64_t expected_plan_generation = 0;
    std::uint32_t expected_payload_checksum = 0;
    std::string expected_hex;
};

inline PolicyV5GoldenFixture LoadPolicyV5GoldenFixture() {
    const std::filesystem::path path = std::filesystem::path(PATHGUARD_SOURCE_DIR)
        / "tests" / "golden" / "policy-v5" / "golden-vector-v5.txt";
    std::ifstream input(path);
    assert(input);
    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const std::size_t equals = line.find('=');
        assert(equals != std::string::npos);
        assert(values.emplace(line.substr(0, equals), line.substr(equals + 1)).second);
    }

    PolicyV5GoldenFixture fixture;
    fixture.document.schema = std::stoi(values.at("schema"));
    assert(values.at("failure") == "open");
    fixture.document.failure_mode = pathguard::FailureMode::kOpen;
    fixture.document.allow_legacy_string_bind =
        values.at("allow_legacy_string_bind") == "true";

    pathguard::AppPolicy app;
    app.package = values.at("package");
    if (values.at("provider") == "virtualize") {
        app.provider_compat = pathguard::ProviderCompat::kVirtualize;
    } else {
        assert(values.at("provider") == "off");
    }
    app.users = {values.at("users")};
    app.processes = {values.at("processes")};
    assert(values.at("mount_action") == "redirect");
    app.mounts.push_back({
        pathguard::MountAction::kRedirect,
        values.at("mount_visible"),
        values.at("mount_backing"),
        static_cast<std::uint16_t>(std::stoul(values.at("mount_depth"))),
        static_cast<std::uint16_t>(std::stoul(values.at("mount_flags"))),
        static_cast<std::size_t>(std::stoull(values.at("mount_line"))),
    });
    fixture.document.apps.push_back(std::move(app));

    fixture.expected_file_size =
        static_cast<std::size_t>(std::stoull(values.at("expected_file_size")));
    fixture.expected_content_generation =
        std::stoull(values.at("expected_content_generation"));
    fixture.expected_plan_generation =
        std::stoull(values.at("expected_plan_generation"));
    fixture.expected_payload_checksum = static_cast<std::uint32_t>(
        std::stoul(values.at("expected_payload_checksum")));
    fixture.expected_hex = values.at("expected_hex");
    return fixture;
}
