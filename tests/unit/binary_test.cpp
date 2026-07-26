#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy_format.h"
#include "pathguard/policy_index.h"
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
    assert(!pathguard::EncodePolicy(unsupported, &bytes, &error));

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

    pathguard::PolicyDocument ambiguous_provider;
    ambiguous_provider.schema = 2;
    ambiguous_provider.failure_mode = pathguard::FailureMode::kOpen;
    pathguard::AppPolicy provider_a = MakeRedirectApp(
        "com.example.provider.a", "SharedSource");
    provider_a.users = {"0"};
    provider_a.provider_compat = pathguard::ProviderCompat::kVirtualize;
    provider_a.mounts[0].backing_path = "TargetA";
    pathguard::AppPolicy provider_b = provider_a;
    provider_b.package = "com.example.provider.b";
    provider_b.mounts[0].backing_path = "TargetB";
    ambiguous_provider.apps = {provider_a, provider_b};
    assert(!pathguard::EncodePolicy(ambiguous_provider, &bytes, &error));

    provider_b.mounts[0].visible_path = "OtherSource";
    provider_b.mounts[0].backing_path = "TargetA";
    ambiguous_provider.apps = {provider_a, provider_b};
    assert(!pathguard::EncodePolicy(ambiguous_provider, &bytes, &error));

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

    pathguard::PolicyDocument golden;
    golden.schema = 2;
    golden.failure_mode = pathguard::FailureMode::kOpen;
    pathguard::AppPolicy golden_app;
    golden_app.package = "org.localsend.localsend_app";
    golden_app.provider_compat = pathguard::ProviderCompat::kVirtualize;
    golden_app.users = {"0"};
    golden_app.processes = {"*"};
    golden_app.mounts = {
        {pathguard::MountAction::kRedirect, "Download/localsend-source",
         "Download/localsend-redirect", 0, 0, 9},
    };
    golden.apps.push_back(golden_app);
    assert(pathguard::EncodePolicy(golden, &bytes, &error));
    constexpr char kGoldenHex[] =
        "50474e4205000200cf00000088e9e01cf460ea685603bd990100000001000000"
        "00000000380000006800000078000000780000000000000015e1b3e301000000"
        "1d0000001f00000000000000010000000000000000000000809707b0f29f2252"
        "00000100000000000100020000000000210000003b000000006f72672e6c6f63"
        "616c73656e642e6c6f63616c73656e645f6170700030002a00446f776e6c6f61"
        "642f6c6f63616c73656e642d736f7572636500446f776e6c6f61642f6c6f6361"
        "6c73656e642d726564697265637400";
    assert(bytes.size() == 207);
    assert(pathguard::ComputeContentGeneration(golden)
        == UINT64_C(11078014328063549684));
    assert(pathguard::ComputePlanGeneration(
               golden.apps[0], pathguard::FailureMode::kOpen)
        == UINT64_C(5918468725002442624));
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kPayloadChecksumOffset)
        == UINT32_C(484501896));
    assert(std::strlen(kGoldenHex) == bytes.size() * 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const std::uint8_t expected = static_cast<std::uint8_t>(
            (HexNibble(kGoldenHex[index * 2]) << 4)
            | HexNibble(kGoldenHex[index * 2 + 1]));
        assert(bytes[index] == expected);
    }
    assert(pathguard::ComputeContentGeneration(golden) != 0);
    assert(pathguard::ComputePlanGeneration(
               golden.apps[0], pathguard::FailureMode::kOpen)
        != 0);
    assert(pathguard::DecodePolicy(bytes, &decoded, &generation, &error));
    assert(decoded.apps[0].provider_compat
        == pathguard::ProviderCompat::kVirtualize);
    return 0;
}
