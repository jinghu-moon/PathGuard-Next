#include "pathguard/policy.h"
#include "pathguard/validation.h"
#include "test_assert.h"

int main() {
    pathguard::ParseError error;

    pathguard::AppPolicy nested;
    nested.package = "com.example.app";
    nested.mounts = {
        {pathguard::MountAction::kDeny, "Pictures/Secret", "", 0, 0, 10},
        {pathguard::MountAction::kRedirect, "Pictures", "Sandbox/Pictures", 0, 0, 11},
    };
    assert(pathguard::ValidatePolicy(&nested, &error));
    assert(nested.mounts[0].action == pathguard::MountAction::kRedirect);
    assert(nested.mounts[1].action == pathguard::MountAction::kDeny);

    pathguard::AppPolicy conflict;
    conflict.package = "com.example.app";
    conflict.mounts = {
        {pathguard::MountAction::kDeny, "Pictures", "", 0, 0, 20},
        {pathguard::MountAction::kRedirect, "Pictures", "Sandbox", 0, 0, 21},
    };
    assert(!pathguard::ValidatePolicy(&conflict, &error));
    assert(error.line == 21);

    pathguard::AppPolicy allow_without_isolate;
    allow_without_isolate.package = "com.example.app";
    allow_without_isolate.mounts = {
        {pathguard::MountAction::kRestore, "Download", "Download", 0, 0, 30},
    };
    assert(!pathguard::ValidatePolicy(&allow_without_isolate, &error));
    assert(error.line == 30);

    pathguard::AppPolicy recursive_isolate;
    recursive_isolate.package = "com.example.app";
    recursive_isolate.mounts = {
        {pathguard::MountAction::kIsolateRoot, "", "Private/Root", 0, 0, 40},
        {pathguard::MountAction::kDeny, "Private", "", 0, 0, 41},
    };
    assert(!pathguard::ValidatePolicy(&recursive_isolate, &error));
    assert(error.line == 41);

    pathguard::AppPolicy invalid_placeholder;
    invalid_placeholder.package = "com.example.app";
    invalid_placeholder.mounts = {
        {pathguard::MountAction::kRedirect, "Download", "Path/{unknown}", 0, 0, 50},
    };
    assert(!pathguard::ValidatePolicy(&invalid_placeholder, &error));
    assert(error.line == 50);
    return 0;
}
