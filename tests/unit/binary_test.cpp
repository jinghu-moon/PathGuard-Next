#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy_format.h"
#include "pathguard/policy_index.h"
#include "policy_v5_golden_fixture.h"
#include "test_assert.h"

namespace {

void StoreLe32(std::vector<std::uint8_t>* bytes, std::size_t offset,
               std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        (*bytes)[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
}

void RefreshChecksum(std::vector<std::uint8_t>* bytes) {
    const std::uint32_t checksum = pathguard::binary_format::Crc32(
        bytes->data() + pathguard::binary_format::kHeaderSize,
        bytes->size() - pathguard::binary_format::kHeaderSize);
    StoreLe32(bytes, pathguard::binary_format::kPayloadChecksumOffset, checksum);
}

std::uint8_t HexNibble(char value) {
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    assert(false);
    return 0;
}

std::uint16_t ReadLe16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0])
        | static_cast<std::uint16_t>(value[1]) << 8;
}

std::string StringAt(const std::vector<std::uint8_t>& bytes,
                     std::uint32_t string_table,
                     std::uint32_t relative_offset) {
    const std::size_t begin = string_table + relative_offset;
    assert(begin < bytes.size());
    const auto end = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                               bytes.end(), 0);
    assert(end != bytes.end());
    return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(begin), end);
}

pathguard::AppPolicy MakeRedirectApp(const char* package, const char* path) {
    pathguard::AppPolicy app;
    app.package = package;
    app.users = {"0", "10"};
    app.processes = {"*"};
    app.mounts.push_back({pathguard::MountAction::kRedirect, path,
                          std::string("Redirected/") + path, 0, 0, 1});
    return app;
}

}  // namespace

int main() {
    pathguard::PolicyDocument source;
    source.schema = 2;
    source.failure_mode = pathguard::FailureMode::kOpen;
    pathguard::AppPolicy app = MakeRedirectApp("com.example.app", "Secret");
    source.apps.push_back(app);
    source.apps.push_back(MakeRedirectApp("com.example.other", "Other"));

    std::vector<std::uint8_t> bytes;
    pathguard::ParseError error;
    assert(pathguard::EncodePolicy(source, &bytes, &error));
    assert(bytes.size() >= pathguard::binary_format::kHeaderSize);
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kFileSizeOffset)
        == bytes.size());
    assert(pathguard::binary_format::ReadLe32(bytes.data())
        == pathguard::binary_format::kMagic);
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kPayloadChecksumOffset)
        == pathguard::binary_format::Crc32(
            bytes.data() + pathguard::binary_format::kHeaderSize,
            bytes.size() - pathguard::binary_format::kHeaderSize));

    pathguard::PolicyDocument decoded;
    std::uint64_t generation = 0;
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));
    assert(generation == pathguard::ComputeContentGeneration(source));
    assert(decoded.schema == 2 && decoded.failure_mode == pathguard::FailureMode::kOpen);
    assert(!decoded.allow_legacy_string_bind);
    assert(decoded.apps.size() == 2);
    const auto find_app = [&](const char* package) -> const pathguard::AppPolicy& {
        for (const pathguard::AppPolicy& candidate : decoded.apps) {
            if (candidate.package == package) return candidate;
        }
        assert(false);
        return decoded.apps.front();
    };
    const pathguard::AppPolicy& decoded_app = find_app("com.example.app");
    assert(decoded_app.media_compat == pathguard::MediaCompat::kOff);
    assert(decoded_app.mounts.size() == 1);
    assert(decoded_app.mounts[0].visible_path == "Secret");
    assert(decoded_app.mounts[0].backing_path == "Redirected/Secret");

    const pathguard::binary_format::PolicyIndexView index{
        bytes.data(),
        bytes.size(),
        pathguard::binary_format::ReadLe32(
            bytes.data() + pathguard::binary_format::kPackageCountOffset),
        pathguard::binary_format::ReadLe32(
            bytes.data() + pathguard::binary_format::kPackageTableOffset),
        pathguard::binary_format::ReadLe32(
            bytes.data() + pathguard::binary_format::kMountRuleTableOffset),
        pathguard::binary_format::ReadLe32(
            bytes.data() + pathguard::binary_format::kStringTableOffset),
    };
    assert(pathguard::binary_format::FindPackageEntry(
               index, "com.example.app", std::strlen("com.example.app")) != nullptr);
    assert(pathguard::binary_format::FindPackageEntry(
               index, "com.example.missing", std::strlen("com.example.missing")) == nullptr);

    std::vector<std::uint8_t> corrupted = bytes;
    corrupted.back() ^= 1;
    assert(!pathguard::DecodePolicy(corrupted, &decoded, &generation, &error));

    pathguard::PolicyDocument legacy_enabled = source;
    legacy_enabled.allow_legacy_string_bind = true;
    assert(pathguard::EncodePolicy(legacy_enabled, &bytes, &error));
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kHeaderFlagsOffset)
        == pathguard::binary_format::kPolicyFlagAllowLegacyStringBind);
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));
    assert(decoded.allow_legacy_string_bind);
    assert(generation == pathguard::ComputeContentGeneration(legacy_enabled));
    assert(pathguard::ComputePlanGeneration(
               legacy_enabled.apps[0], legacy_enabled.failure_mode, true)
        != pathguard::ComputePlanGeneration(
               legacy_enabled.apps[0], legacy_enabled.failure_mode, false));

    std::vector<std::uint8_t> unknown_flags = bytes;
    StoreLe32(&unknown_flags, pathguard::binary_format::kHeaderFlagsOffset,
              UINT32_C(0x80000000));
    assert(!pathguard::DecodePolicy(
        unknown_flags, &decoded, &generation, &error));

    assert(pathguard::EncodePolicy(source, &bytes, &error));

    std::vector<std::uint8_t> unknown_action = bytes;
    const std::uint32_t mount_offset = pathguard::binary_format::ReadLe32(
        unknown_action.data() + pathguard::binary_format::kMountRuleTableOffset);
    unknown_action[mount_offset + pathguard::binary_format::kMountActionOffset] = 0xff;
    RefreshChecksum(&unknown_action);
    assert(!pathguard::DecodePolicy(unknown_action, &decoded, &generation, &error));

    std::vector<std::uint8_t> unknown_provider = bytes;
    const std::uint32_t provider_package_offset = pathguard::binary_format::ReadLe32(
        unknown_provider.data() + pathguard::binary_format::kPackageTableOffset);
    unknown_provider[provider_package_offset
        + pathguard::binary_format::kPackageProviderCompatOffset] = 0xff;
    RefreshChecksum(&unknown_provider);
    assert(!pathguard::DecodePolicy(
        unknown_provider, &decoded, &generation, &error));

    std::vector<std::uint8_t> overlapping_ranges = bytes;
    const std::uint32_t package_offset = pathguard::binary_format::ReadLe32(
        overlapping_ranges.data() + pathguard::binary_format::kPackageTableOffset);
    StoreLe32(&overlapping_ranges,
              package_offset + pathguard::binary_format::kPackageSize
                  + pathguard::binary_format::kPackageFirstMountOffset,
              0);
    RefreshChecksum(&overlapping_ranges);
    assert(!pathguard::DecodePolicy(
        overlapping_ranges, &decoded, &generation, &error));

    pathguard::PolicyDocument unsupported = source;
    unsupported.apps[0].mounts[0].action = pathguard::MountAction::kDeny;
    unsupported.apps[0].mounts[0].backing_path.clear();
    assert(pathguard::EncodePolicy(unsupported, &bytes, &error));
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));
    const pathguard::AppPolicy& decoded_deny_app = find_app("com.example.app");
    const auto deny = std::find_if(
        decoded_deny_app.mounts.begin(), decoded_deny_app.mounts.end(),
        [](const auto& rule) {
            return rule.action == pathguard::MountAction::kDeny;
        });
    assert(deny != decoded_deny_app.mounts.end());
    assert(deny->backing_path.empty());

    pathguard::PolicyDocument media_gated = source;
    media_gated.apps[0].media_compat = pathguard::MediaCompat::kHideDenied;
    assert(!pathguard::EncodePolicy(media_gated, &bytes, &error));

    pathguard::PolicyDocument runtime_user = source;
    runtime_user.apps[0].mounts[0].backing_path = "Users/{user}/Target";
    assert(pathguard::EncodePolicy(runtime_user, &bytes, &error));
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));
    const auto runtime_app = std::find_if(
        decoded.apps.begin(), decoded.apps.end(), [](const pathguard::AppPolicy& item) {
            return item.package == "com.example.app";
        });
    assert(runtime_app != decoded.apps.end());
    assert(runtime_app->mounts[0].backing_path == "Users/{user}/Target");

    pathguard::PolicyDocument provider_scopes;
    provider_scopes.schema = 2;
    provider_scopes.failure_mode = pathguard::FailureMode::kOpen;
    pathguard::AppPolicy provider_a = MakeRedirectApp(
        "com.example.provider.a", "SharedSource");
    provider_a.users = {"0"};
    provider_a.provider_compat = pathguard::ProviderCompat::kVirtualize;
    provider_a.mounts[0].backing_path = "TargetA";
    pathguard::AppPolicy provider_b = provider_a;
    provider_b.package = "com.example.provider.b";
    provider_b.mounts[0].backing_path = "TargetB";
    provider_scopes.apps = {provider_a, provider_b};
    assert(pathguard::EncodePolicy(provider_scopes, &bytes, &error));
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));

    provider_b.mounts[0].visible_path = "OtherSource";
    provider_b.mounts[0].backing_path = "TargetA";
    provider_scopes.apps = {provider_a, provider_b};
    assert(pathguard::EncodePolicy(provider_scopes, &bytes, &error));
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));

    pathguard::PolicyDocument shared_target_provider;
    shared_target_provider.schema = 2;
    shared_target_provider.failure_mode = pathguard::FailureMode::kOpen;
    pathguard::AppPolicy provider_shared = MakeRedirectApp(
        "com.example.provider.shared", "Download/Source");
    provider_shared.users = {"0"};
    provider_shared.provider_compat = pathguard::ProviderCompat::kVirtualize;
    provider_shared.mounts[0].backing_path = "Download/Shared";
    pathguard::LogicalMountRule shared_alias = provider_shared.mounts[0];
    shared_alias.visible_path = "Pictures";
    provider_shared.mounts.push_back(shared_alias);
    shared_target_provider.apps = {provider_shared};
    assert(pathguard::EncodePolicy(shared_target_provider, &bytes, &error));
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));
    assert(find_app("com.example.provider.shared").mounts.size() == 2);

    pathguard::PolicyDocument collision_source;
    collision_source.schema = 2;
    collision_source.failure_mode = pathguard::FailureMode::kOpen;
    for (const char* package : {"pkg.000418cc", "pkg.000bec18"}) {
        collision_source.apps.push_back(MakeRedirectApp(package, "Collision"));
    }
    assert(pathguard::binary_format::PackageNameHash(
               collision_source.apps[0].package.data(),
               collision_source.apps[0].package.size())
        == pathguard::binary_format::PackageNameHash(
               collision_source.apps[1].package.data(),
               collision_source.apps[1].package.size()));
    assert(pathguard::EncodePolicy(collision_source, &bytes, &error));
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));
    assert(decoded.apps.size() == 2);
    assert(pathguard::binary_format::FindPackageEntry(
               {bytes.data(), bytes.size(),
                pathguard::binary_format::ReadLe32(
                    bytes.data() + pathguard::binary_format::kPackageCountOffset),
                pathguard::binary_format::ReadLe32(
                    bytes.data() + pathguard::binary_format::kPackageTableOffset),
                pathguard::binary_format::ReadLe32(
                    bytes.data() + pathguard::binary_format::kMountRuleTableOffset),
                pathguard::binary_format::ReadLe32(
                    bytes.data() + pathguard::binary_format::kStringTableOffset)},
               "pkg.000bec18", std::strlen("pkg.000bec18")) != nullptr);

    const PolicyV5GoldenFixture golden_fixture = LoadPolicyV5GoldenFixture();
    const pathguard::PolicyDocument& golden = golden_fixture.document;
    assert(pathguard::EncodePolicy(golden, &bytes, &error));
    assert(bytes.size() == golden_fixture.expected_file_size);
    assert(pathguard::ComputeContentGeneration(golden)
        == golden_fixture.expected_content_generation);
    assert(pathguard::ComputePlanGeneration(
               golden.apps[0], pathguard::FailureMode::kOpen)
        == golden_fixture.expected_plan_generation);
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kPayloadChecksumOffset)
        == golden_fixture.expected_payload_checksum);
    assert(golden_fixture.expected_hex.size() == bytes.size() * 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const std::uint8_t expected = static_cast<std::uint8_t>(
            (HexNibble(golden_fixture.expected_hex[index * 2]) << 4)
            | HexNibble(golden_fixture.expected_hex[index * 2 + 1]));
        assert(bytes[index] == expected);
    }

    const std::uint32_t golden_package_offset =
        pathguard::binary_format::ReadLe32(
            bytes.data() + pathguard::binary_format::kPackageTableOffset);
    const std::uint32_t golden_mount_offset =
        pathguard::binary_format::ReadLe32(
            bytes.data() + pathguard::binary_format::kMountRuleTableOffset);
    const std::uint32_t golden_event_offset =
        pathguard::binary_format::ReadLe32(
            bytes.data() + pathguard::binary_format::kEventRuleTableOffset);
    const std::uint32_t golden_string_offset =
        pathguard::binary_format::ReadLe32(
            bytes.data() + pathguard::binary_format::kStringTableOffset);
    assert(pathguard::binary_format::ReadLe32(bytes.data())
        == pathguard::binary_format::kMagic);
    assert(ReadLe16(bytes.data() + 4) == pathguard::binary_format::kFormatVersion);
    assert(ReadLe16(bytes.data() + 6) == pathguard::binary_format::kSchemaVersion);
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kFileSizeOffset)
        == golden_fixture.expected_file_size);
    assert(pathguard::binary_format::ReadLe64(
               bytes.data() + pathguard::binary_format::kContentGenerationOffset)
        == golden_fixture.expected_content_generation);
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kPackageCountOffset) == 1);
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kMountRuleCountOffset) == 1);
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kEventRuleCountOffset) == 0);
    assert(golden_package_offset == pathguard::binary_format::kHeaderSize);
    assert(golden_mount_offset == golden_package_offset
        + pathguard::binary_format::kPackageSize);
    assert(golden_event_offset == golden_mount_offset
        + pathguard::binary_format::kMountRuleSize);
    assert(golden_string_offset == golden_event_offset);
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kHeaderFlagsOffset) == 0);

    const std::uint8_t* package_entry = bytes.data() + golden_package_offset;
    assert(pathguard::binary_format::ReadLe32(
               package_entry + pathguard::binary_format::kPackageHashOffset)
        == pathguard::binary_format::PackageNameHash(
            golden.apps[0].package.data(), golden.apps[0].package.size()));
    assert(StringAt(bytes, golden_string_offset,
                    pathguard::binary_format::ReadLe32(
                        package_entry + pathguard::binary_format::kPackageNameOffset))
        == golden.apps[0].package);
    assert(StringAt(bytes, golden_string_offset,
                    pathguard::binary_format::ReadLe32(
                        package_entry + pathguard::binary_format::kPackageUsersOffset))
        == "0");
    assert(StringAt(bytes, golden_string_offset,
                    pathguard::binary_format::ReadLe32(
                        package_entry + pathguard::binary_format::kPackageProcessesOffset))
        == "*");
    assert(pathguard::binary_format::ReadLe32(
               package_entry + pathguard::binary_format::kPackageFirstMountOffset) == 0);
    assert(pathguard::binary_format::ReadLe32(
               package_entry + pathguard::binary_format::kPackageMountCountOffset) == 1);
    assert(pathguard::binary_format::ReadLe32(
               package_entry + pathguard::binary_format::kPackageFirstEventOffset) == 0);
    assert(pathguard::binary_format::ReadLe32(
               package_entry + pathguard::binary_format::kPackageEventCountOffset) == 0);
    assert(pathguard::binary_format::ReadLe64(
               package_entry + pathguard::binary_format::kPackagePlanGenerationOffset)
        == golden_fixture.expected_plan_generation);
    assert(package_entry[pathguard::binary_format::kPackageFailureModeOffset]
        == static_cast<std::uint8_t>(pathguard::FailureMode::kOpen));
    assert(package_entry[pathguard::binary_format::kPackageMediaCompatOffset]
        == static_cast<std::uint8_t>(pathguard::MediaCompat::kOff));
    assert(package_entry[pathguard::binary_format::kPackageProviderCompatOffset]
        == static_cast<std::uint8_t>(pathguard::ProviderCompat::kVirtualize));
    assert(package_entry[43] == 0);
    assert(pathguard::binary_format::ReadLe32(package_entry + 44) == 0);

    const std::uint8_t* mount_entry = bytes.data() + golden_mount_offset;
    assert(mount_entry[pathguard::binary_format::kMountActionOffset]
        == static_cast<std::uint8_t>(pathguard::MountAction::kRedirect));
    assert(mount_entry[1] == 0);
    assert(ReadLe16(mount_entry + pathguard::binary_format::kMountDepthOffset) == 2);
    assert(ReadLe16(mount_entry + pathguard::binary_format::kMountFlagsOffset) == 0);
    assert(ReadLe16(mount_entry + 6) == 0);
    assert(StringAt(bytes, golden_string_offset,
                    pathguard::binary_format::ReadLe32(
                        mount_entry + pathguard::binary_format::kMountVisiblePathOffset))
        == "Download/localsend-source");
    assert(StringAt(bytes, golden_string_offset,
                    pathguard::binary_format::ReadLe32(
                        mount_entry + pathguard::binary_format::kMountBackingPathOffset))
        == "Download/localsend-redirect");

    assert(pathguard::ComputeContentGeneration(golden) != 0);
    assert(pathguard::ComputePlanGeneration(
               golden.apps[0], pathguard::FailureMode::kOpen)
        != 0);
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));
    assert(decoded.apps[0].provider_compat
        == pathguard::ProviderCompat::kVirtualize);
    return 0;
}
