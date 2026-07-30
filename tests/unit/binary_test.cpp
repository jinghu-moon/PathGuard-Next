#include <cstdint>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy.h"
#include "pathguard/policy_format.h"
#include "pathguard/policy_v6.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;

    PolicyDocument document;
    document.schema = 2;
    document.failure_mode = FailureMode::kOpen;
    document.allow_legacy_string_bind = true;
    AppPolicy app;
    app.package = "org.localsend.localsend_app";
    app.users = {"0"};
    app.processes = {"*"};
    app.provider_compat = ProviderCompat::kVirtualize;
    app.mounts.push_back({MountAction::kRedirect,
                          "Download/localsend-source",
                          "Download/localsend-redirect", 2, 0, 0});
    document.apps.push_back(app);

    std::vector<std::uint8_t> bytes;
    ParseError error;
    assert(EncodePolicy(document, &bytes, &error));
    assert(bytes[4] == binary_format::kFormatVersion);
    assert(bytes[6] == binary_format::kSchemaVersion);
    PolicyV6 semantic;
    const auto v6 = DecodePolicyV6(bytes, &semantic);
    assert(v6.ok);
    assert(semantic.packages.front().selectors.front().root
           == "Download/localsend-source");

    PolicyDocument decoded;
    std::uint64_t generation = 0;
    assert(DecodePolicy(bytes, &decoded, &generation, &error));
    assert(generation == ComputeContentGeneration(document));
    assert(decoded.apps.front().mounts.front().backing_path
           == "Download/localsend-redirect");

    std::vector<std::uint8_t> old = bytes;
    old[4] = 5;
    assert(!DecodePolicy(old, &decoded, &generation, &error));
    bytes[binary_format::kPayloadChecksumOffset] ^= 1;
    assert(!DecodePolicy(bytes, &decoded, &generation, &error));
    return 0;
}
