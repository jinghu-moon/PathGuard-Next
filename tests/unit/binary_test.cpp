#include <cstdint>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy_format.h"
#include "pathguard/policy_index.h"
#include "test_assert.h"

int main() {
    pathguard::PolicyDocument source;
    source.schema = 1;
    source.failure_mode = "fail_open_with_alert";
    pathguard::AppPolicy app;
    app.package = "com.example.app";
    app.media_compat = pathguard::MediaCompat::kQueryFilter;
    app.users = {"0", "10"};
    app.processes = {"*"};
    app.rules.push_back({pathguard::RuleAction::kDeny, "/storage/emulated/0/Secret", "", 3});
    app.rules.push_back({pathguard::RuleAction::kRedirect, "/storage/emulated/0/Download", "/storage/emulated/0/Cache", 4});
    source.apps.push_back(app);
    pathguard::AppPolicy other;
    other.package = "com.example.other";
    other.users = {"0"};
    other.processes = {"*"};
    other.rules.push_back({pathguard::RuleAction::kDeny, "/storage/emulated/0/Other", "", 8});
    source.apps.push_back(other);
    std::vector<std::uint8_t> bytes;
    pathguard::ParseError error;
    const bool encoded = pathguard::EncodePolicy(source, 7, &bytes, &error);
    assert(encoded);
    pathguard::PolicyDocument decoded;
    std::uint64_t generation = 0;
    const bool decoded_ok = pathguard::DecodePolicy(bytes, &decoded, &generation, &error);
    assert(decoded_ok);
    assert(generation == 7 && decoded.apps.size() == 2);
    for (std::size_t index = 1; index < decoded.apps.size(); ++index) {
        const auto previous_hash = pathguard::binary_format::PackageNameHash(
            decoded.apps[index - 1].package.data(), decoded.apps[index - 1].package.size());
        const auto current_hash = pathguard::binary_format::PackageNameHash(
            decoded.apps[index].package.data(), decoded.apps[index].package.size());
        assert(previous_hash < current_hash
            || (previous_hash == current_hash
                && decoded.apps[index - 1].package < decoded.apps[index].package));
    }
    const auto find_app = [&](const char* package) -> const pathguard::AppPolicy& {
        for (const auto& candidate : decoded.apps) {
            if (candidate.package == package) return candidate;
        }
        assert(false);
        return decoded.apps[0];
    };
    const auto& decoded_app = find_app("com.example.app");
    assert(decoded_app.rules.size() == 2);
    assert(decoded_app.media_compat == pathguard::MediaCompat::kQueryFilter);
    assert(decoded_app.users.size() == 2 && decoded_app.processes.size() == 1);
    assert(decoded_app.rules[0].action == pathguard::RuleAction::kDeny);
    assert(decoded_app.rules[0].source == "/storage/emulated/0/Secret");
    assert(decoded_app.rules[0].target.empty());
    assert(decoded_app.rules[1].action == pathguard::RuleAction::kRedirect);
    assert(decoded_app.rules[1].source == "/storage/emulated/0/Download");
    assert(decoded_app.rules[1].target == "/storage/emulated/0/Cache");
    const auto& decoded_other = find_app("com.example.other");
    assert(decoded_other.rules.size() == 1);

    pathguard::PolicyDocument collision_source = source;
    collision_source.apps.clear();
    for (const char* package : {"pkg.000418cc", "pkg.000bec18"}) {
        pathguard::AppPolicy collision_app;
        collision_app.package = package;
        collision_app.users = {"0"};
        collision_app.processes = {"*"};
        collision_app.rules.push_back({pathguard::RuleAction::kDeny,
                                       std::string("/storage/emulated/0/") + package,
                                       "", 1});
        collision_source.apps.push_back(std::move(collision_app));
    }
    assert(pathguard::binary_format::PackageNameHash(
               collision_source.apps[0].package.data(), collision_source.apps[0].package.size())
        == pathguard::binary_format::PackageNameHash(
               collision_source.apps[1].package.data(), collision_source.apps[1].package.size()));
    std::vector<std::uint8_t> collision_bytes;
    assert(pathguard::EncodePolicy(collision_source, 1, &collision_bytes, &error));
    pathguard::PolicyDocument collision_decoded;
    assert(pathguard::DecodePolicy(collision_bytes, &collision_decoded, &generation, &error));
    assert(collision_decoded.apps.size() == 2);
    assert(collision_decoded.apps[0].package == "pkg.000418cc");
    assert(collision_decoded.apps[1].package == "pkg.000bec18");
    const pathguard::binary_format::PolicyIndexView collision_index{
        collision_bytes.data(),
        collision_bytes.size(),
        pathguard::binary_format::ReadLe32(collision_bytes.data() + 24),
        pathguard::binary_format::ReadLe32(collision_bytes.data() + 28),
        pathguard::binary_format::ReadLe32(collision_bytes.data() + 32),
        pathguard::binary_format::ReadLe32(collision_bytes.data() + 36),
    };
    const auto* first_collision = pathguard::binary_format::FindPackageEntry(
        collision_index, "pkg.000418cc", std::string("pkg.000418cc").size());
    const auto* second_collision = pathguard::binary_format::FindPackageEntry(
        collision_index, "pkg.000bec18", std::string("pkg.000bec18").size());
    assert(first_collision != nullptr && second_collision != nullptr);
    assert(first_collision != second_collision);
    assert(pathguard::binary_format::FindPackageEntry(
        collision_index, "pkg.not-present", std::string("pkg.not-present").size()) == nullptr);

    std::vector<std::uint8_t> bad_package_hash = bytes;
    bad_package_hash[pathguard::binary_format::kHeaderSize] ^= 1;
    assert(!pathguard::DecodePolicy(bad_package_hash, &decoded, &generation, &error));
    if (!bytes.empty()) bytes[bytes.size() - 1] ^= 1;
    const bool corrupted_ok = pathguard::DecodePolicy(bytes, &decoded, &generation, &error);
    assert(!corrupted_ok);
    return 0;
}
