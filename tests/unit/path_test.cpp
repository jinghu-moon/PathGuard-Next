#include <cassert>
#include <string>

#include "pathguard/path.h"
#include "pathguard/policy.h"
#include "pathguard/validation.h"

int main() {
    std::string output;
    assert(pathguard::NormalizePath("DCIM/Camera/", &output));
    assert(output == "/storage/emulated/0/DCIM/Camera");
    assert(pathguard::NormalizePath("/sdcard/DCIM", &output));
    assert(output == "/storage/emulated/0/DCIM");
    assert(!pathguard::NormalizePath("../Secret", &output));

    assert(pathguard::ExpandPackagePlaceholder(
        "PathGuard/{package}/Cache", "com.example.app", &output));
    assert(output == "PathGuard/com.example.app/Cache");

    pathguard::AppPolicy policy;
    policy.package = "com.example.app";
    policy.rules = {{pathguard::RuleAction::kRedirect, "Download", "PathGuard/{package}", 4}};
    pathguard::ParseError error;
    assert(pathguard::ValidatePolicy(&policy, &error));
    assert(policy.rules[0].source == "/storage/emulated/0/Download");
    assert(policy.rules[0].target == "/storage/emulated/0/PathGuard/com.example.app");
    return 0;
}
