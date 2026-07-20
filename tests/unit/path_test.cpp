#include <string>

#include "pathguard/path.h"
#include "pathguard/policy.h"
#include "pathguard/validation.h"
#include "test_assert.h"

int main() {
    std::string output;
    assert(pathguard::NormalizeLogicalPath("DCIM/Camera", &output));
    assert(output == "DCIM/Camera");
    assert(!pathguard::NormalizeLogicalPath("/storage/emulated/0/DCIM", &output));
    assert(!pathguard::NormalizeLogicalPath("../Secret", &output));
    assert(!pathguard::NormalizeLogicalPath("DCIM//Camera", &output));

    assert(pathguard::ExpandPathPlaceholders(
        "PathGuard/{package}/{user}", "com.example.app", &output));
    assert(output == "PathGuard/com.example.app/{user}");
    assert(!pathguard::ExpandPathPlaceholders(
        "PathGuard/{unknown}", "com.example.app", &output));

    pathguard::AppPolicy policy;
    policy.package = "com.example.app";
    policy.mounts = {{pathguard::MountAction::kRedirect, "Download",
                      "PathGuard/{package}", 0, 0, 4}};
    pathguard::ParseError error;
    assert(pathguard::ValidatePolicy(&policy, &error));
    assert(policy.mounts[0].visible_path == "Download");
    assert(policy.mounts[0].backing_path == "PathGuard/com.example.app");
    assert(policy.mounts[0].depth == 1);
    return 0;
}
