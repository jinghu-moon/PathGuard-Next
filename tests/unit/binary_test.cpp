#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy_format.h"
#include "pathguard/policy_index.h"
#include "test_assert.h"

namespace {

std::string ToHex(const std::vector<std::uint8_t>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        output.push_back(digits[byte >> 4]);
        output.push_back(digits[byte & 0x0f]);
    }
    return output;
}

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

pathguard::AppPolicy MakeDenyApp(const char* package, const char* path) {
    pathguard::AppPolicy app;
    app.package = package;
    app.users = {"0", "10"};
    app.processes = {"*"};
    app.mounts.push_back({pathguard::MountAction::kDeny, path, "", 0, 0, 1});
    return app;
}

}  // namespace

int main() {
    pathguard::PolicyDocument source;
    source.schema = 2;
    source.failure_mode = pathguard::FailureMode::kOpen;
    pathguard::AppPolicy app = MakeDenyApp("com.example.app", "Secret");
    app.media_compat = pathguard::MediaCompat::kHideDenied;
    source.apps.push_back(app);
    source.apps.push_back(MakeDenyApp("com.example.other", "Other"));

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
    assert(decoded.apps.size() == 2);
    const auto find_app = [&](const char* package) -> const pathguard::AppPolicy& {
        for (const pathguard::AppPolicy& candidate : decoded.apps) {
            if (candidate.package == package) return candidate;
        }
        assert(false);
        return decoded.apps.front();
    };
    const pathguard::AppPolicy& decoded_app = find_app("com.example.app");
    assert(decoded_app.media_compat == pathguard::MediaCompat::kHideDenied);
    assert(decoded_app.mounts.size() == 1);
    assert(decoded_app.mounts[0].visible_path == "Secret");

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

    std::vector<std::uint8_t> unknown_action = bytes;
    const std::uint32_t mount_offset = pathguard::binary_format::ReadLe32(
        unknown_action.data() + pathguard::binary_format::kMountRuleTableOffset);
    unknown_action[mount_offset + pathguard::binary_format::kMountActionOffset] = 0xff;
    RefreshChecksum(&unknown_action);
    assert(!pathguard::DecodePolicy(unknown_action, &decoded, &generation, &error));

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
    unsupported.apps[0].mounts[0].action = pathguard::MountAction::kRedirect;
    unsupported.apps[0].mounts[0].backing_path = "Sandbox";
    assert(!pathguard::EncodePolicy(unsupported, &bytes, &error));

    pathguard::PolicyDocument collision_source;
    collision_source.schema = 2;
    collision_source.failure_mode = pathguard::FailureMode::kOpen;
    for (const char* package : {"pkg.000418cc", "pkg.000bec18"}) {
        collision_source.apps.push_back(MakeDenyApp(package, "Collision"));
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
    golden_app.media_compat = pathguard::MediaCompat::kHideDenied;
    golden_app.users = {"0"};
    golden_app.processes = {"*"};
    golden_app.mounts = {
        {pathguard::MountAction::kDeny, "Pictures/Nagram", "", 0, 0, 9},
        {pathguard::MountAction::kDeny, "DCIM", "", 0, 0, 10},
    };
    golden.apps.push_back(golden_app);
    assert(pathguard::EncodePolicy(golden, &bytes, &error));
    assert(pathguard::ComputeContentGeneration(golden) == UINT64_C(0xef7c781910967e88));
    assert(pathguard::ComputePlanGeneration(
               golden.apps[0], pathguard::FailureMode::kOpen)
        == UINT64_C(0xdba633f57989fddd));
    assert(pathguard::binary_format::ReadLe32(
               bytes.data() + pathguard::binary_format::kPayloadChecksumOffset)
        == UINT32_C(0x0d28599f));
    assert(ToHex(bytes) ==
        "50474e4204000200be0000009f59280d887e961019787cef0100000002000000"
        "00000000380000006800000088000000880000000000000015e1b3e301000000"
        "1d0000001f00000000000000020000000000000000000000ddfd8979f533a6db"
        "0001000000000000000001000000000021000000000000000000020000000000"
        "2600000000000000006f72672e6c6f63616c73656e642e6c6f63616c73656e"
        "645f6170700030002a004443494d0050696374757265732f4e616772616d00");
    return 0;
}
