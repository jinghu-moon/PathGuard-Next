#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "pathguard/pattern.h"
#include "pathguard/policy_format.h"
#include "pathguard/policy_v6.h"
#include "pathguard/policy_v6_view.h"
#include "test_assert.h"

namespace {

using namespace pathguard;

void Store16(std::vector<std::uint8_t>* bytes, std::size_t offset,
             std::uint16_t value) {
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void Store32(std::vector<std::uint8_t>* bytes, std::size_t offset,
             std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        (*bytes)[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
    }
}

PolicyV6 LiteralPolicy() {
    PolicyV6 policy;
    policy.allow_legacy_mount = true;
    PolicyPackageV6 package;
    package.package = "org.localsend.localsend_app";
    package.users = {0};
    package.all_processes = true;
    package.selectors.push_back({
        PolicyMatchKind::kLiteralPrefix,
        PolicyObjectType::kAny,
        "Download/localsend-source",
    });
    PolicyActionV6 action;
    action.selector_index = 0;
    action.kind = PolicyActionKind::kRedirect;
    action.domain = PolicyExecutionDomain::kMount;
    action.target = "Download/localsend-redirect";
    action.preserve = PolicyPreserveMode::kRelative;
    action.collision = PolicyCollisionMode::kReject;
    action.reverse = PolicyReverseMode::kStaticUnique;
    package.actions.push_back(action);
    policy.packages.push_back(std::move(package));
    return policy;
}

PolicyV6 GlobPolicy() {
    const auto base = pattern::CompilePattern("**/IMG_[0-9]?.jpg");
    const auto except_a = pattern::CompilePattern("private/**");
    const auto except_b = pattern::CompilePattern("**/thumbnail-*/**");
    assert(base.ok() && except_a.ok() && except_b.ok());
    PolicyV6 policy;
    PolicyPackageV6 package;
    package.package = "org.pathguard.glob_golden";
    package.users = {0};
    package.provider_enabled = true;
    PolicySelectorV6 selector;
    selector.match_kind = PolicyMatchKind::kGlob;
    selector.object_type = PolicyObjectType::kFile;
    selector.root = "Pictures";
    selector.base_pattern = *base.program;
    selector.except_patterns = {*except_a.program, *except_b.program};
    package.selectors.push_back(std::move(selector));
    PolicyActionV6 action;
    action.kind = PolicyActionKind::kRedirect;
    action.domain = PolicyExecutionDomain::kProvider;
    action.target = "Download/images";
    action.preserve = PolicyPreserveMode::kRelative;
    action.collision = PolicyCollisionMode::kReject;
    action.reverse = PolicyReverseMode::kProvenance;
    action.required_capabilities = UINT64_C(0x00030000);
    action.required_operations = UINT64_C(0x000ffeff);
    package.actions.push_back(action);
    policy.packages.push_back(std::move(package));
    return policy;
}

std::vector<std::uint8_t> Encode(const PolicyV6& policy) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    assert(EncodePolicyV6(policy, &bytes, &error));
    assert(error.empty());
    return bytes;
}

std::vector<std::uint8_t> LoadGolden(std::string_view name) {
    const std::filesystem::path path = std::filesystem::path(PATHGUARD_SOURCE_DIR)
        / "tests" / "golden" / "policy-v6" / (std::string(name) + ".txt");
    std::ifstream input(path);
    assert(input);
    std::size_t size = 0;
    std::string hex;
    std::string line;
    while (std::getline(input, line)) {
        if (line.starts_with("size=")) size = std::stoull(line.substr(5));
        if (line.starts_with("hex=")) hex = line.substr(4);
    }
    assert(hex.size() == size * 2);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(size);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(
            std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

void RefreshChecksum(std::vector<std::uint8_t>* bytes) {
    Store32(bytes, binary_format::kPayloadChecksumOffset,
        binary_format::Crc32(bytes->data() + binary_format::kHeaderSize,
                            bytes->size() - binary_format::kHeaderSize));
}

void Rejects(std::vector<std::uint8_t> bytes) {
    PolicyV6 decoded;
    assert(!DecodePolicyV6(bytes, &decoded).ok);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace pathguard;

    const auto literal = Encode(LiteralPolicy());
    assert(literal == LoadGolden("g6-literal"));
    const auto literal_again = Encode(LiteralPolicy());
    assert(literal == literal_again);
    assert(literal.size() <= binary_format::kMaxPolicyFileSize);
    PolicyV6 decoded_literal;
    const auto literal_result = DecodePolicyV6(literal, &decoded_literal);
    assert(literal_result.ok);
    assert(literal_result.content_generation != 0);
    assert(decoded_literal.packages.size() == 1);
    assert(decoded_literal.packages.front().actions.front().rule_id != 0);
    policy_v6_view::PolicyV6View literal_view;
    policy_v6_view::Error view_error{};
    assert(literal_view.Initialize(literal.data(), literal.size(), &view_error));
    assert(view_error == policy_v6_view::Error::kNone);
    policy_v6_view::PackageRef package_view;
    assert(literal_view.FindPackage("org.localsend.localsend_app",
                                    sizeof("org.localsend.localsend_app") - 1,
                                    &package_view));
    assert(literal_view.PackageMatchesScope(
        package_view, "org.localsend.localsend_app",
        sizeof("org.localsend.localsend_app") - 1, 0));
    policy_v6_view::ActionRef action_view;
    policy_v6_view::SelectorRef selector_view;
    policy_v6_view::StringRef root_view;
    assert(literal_view.ActionAt(package_view.first_action, &action_view));
    assert(literal_view.SelectorAt(action_view.selector_id, &selector_view));
    assert(literal_view.StringAt(selector_view.root_id, &root_view));
    assert(root_view.Equals("Download/localsend-source", 25));

    const auto glob = Encode(GlobPolicy());
    assert(glob == LoadGolden("g6-glob-except"));
    assert(glob != literal);
    PolicyV6 decoded_glob;
    const auto glob_result = DecodePolicyV6(glob, &decoded_glob);
    assert(glob_result.ok);
    assert(decoded_glob.packages.front().selectors.front().except_patterns.size() == 2);
    assert(decoded_glob.packages.front().actions.front().domain
           == PolicyExecutionDomain::kProvider);

    auto corrupt = literal;
    Store16(&corrupt, 4, 5);
    Rejects(corrupt);
    corrupt = literal;
    corrupt[binary_format::kPayloadChecksumOffset] ^= 1;
    Rejects(corrupt);
    assert(!literal_view.Initialize(corrupt.data(), corrupt.size(), &view_error));
    assert(view_error == policy_v6_view::Error::kChecksum);
    corrupt = literal;
    corrupt.push_back(0);
    Store32(&corrupt, binary_format::kFileSizeOffset,
            static_cast<std::uint32_t>(corrupt.size()));
    RefreshChecksum(&corrupt);
    Rejects(corrupt);
    corrupt = literal;
    corrupt[114] = 1;
    Rejects(corrupt);
    corrupt = literal;
    Store32(&corrupt, binary_format::kSelectorCountOffset,
            binary_format::kMaxSelectorCount + 1);
    Rejects(corrupt);

    corrupt = glob;
    const std::uint32_t action_offset =
        corrupt[binary_format::kActionTableOffset]
        | static_cast<std::uint32_t>(corrupt[binary_format::kActionTableOffset + 1]) << 8
        | static_cast<std::uint32_t>(corrupt[binary_format::kActionTableOffset + 2]) << 16
        | static_cast<std::uint32_t>(corrupt[binary_format::kActionTableOffset + 3]) << 24;
    corrupt[action_offset + 41] = 0xff;
    RefreshChecksum(&corrupt);
    Rejects(corrupt);
    if (argc == 2 && std::string_view(argv[1]) == "--dump-golden") {
        auto dump = [](std::string_view name,
                       const std::vector<std::uint8_t>& bytes) {
            std::cout << name << "_size=" << bytes.size() << '\n';
            std::cout << name << "_hex=";
            for (std::uint8_t byte : bytes) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(byte);
            }
            std::cout << std::dec << '\n';
        };
        dump("literal", literal);
        dump("glob_except", glob);
    }
    return 0;
}
