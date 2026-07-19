#include <cassert>
#include <cstdint>
#include <vector>

#include "pathguard/binary.h"

int main() {
    pathguard::PolicyDocument source;
    source.schema = 1;
    source.failure_mode = "fail_open_with_alert";
    pathguard::AppPolicy app;
    app.package = "com.example.app";
    app.users = {"0", "10"};
    app.processes = {"*"};
    app.rules.push_back({pathguard::RuleAction::kDeny, "/storage/emulated/0/Secret", "", 3});
    app.rules.push_back({pathguard::RuleAction::kRedirect, "/storage/emulated/0/Download", "/storage/emulated/0/Cache", 4});
    source.apps.push_back(app);
    std::vector<std::uint8_t> bytes;
    pathguard::ParseError error;
    const bool encoded = pathguard::EncodePolicy(source, 7, &bytes, &error);
    assert(encoded);
    pathguard::PolicyDocument decoded;
    std::uint64_t generation = 0;
    const bool decoded_ok = pathguard::DecodePolicy(bytes, &decoded, &generation, &error);
    assert(decoded_ok);
    assert(generation == 7 && decoded.apps.size() == 1 && decoded.apps[0].rules.size() == 2);
    assert(decoded.apps[0].users.size() == 2 && decoded.apps[0].processes.size() == 1);
    assert(decoded.apps[0].rules[0].action == pathguard::RuleAction::kDeny);
    assert(decoded.apps[0].rules[0].source == "/storage/emulated/0/Secret");
    assert(decoded.apps[0].rules[0].target.empty());
    assert(decoded.apps[0].rules[1].action == pathguard::RuleAction::kRedirect);
    assert(decoded.apps[0].rules[1].source == "/storage/emulated/0/Download");
    assert(decoded.apps[0].rules[1].target == "/storage/emulated/0/Cache");
    if (!bytes.empty()) bytes[bytes.size() - 1] ^= 1;
    const bool corrupted_ok = pathguard::DecodePolicy(bytes, &decoded, &generation, &error);
    assert(!corrupted_ok);
    return 0;
}
