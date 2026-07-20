#include <string>

#include "pathguard/policy.h"
#include "test_assert.h"

int main() {
    const std::string input =
        "schema = 2\n"
        "failure = open\n"
        "\n"
        "[com.example.app]\n"
        "media = hide_denied\n"
        "users = 0, 10\n"
        "processes = *\n"
        "redirect DCIM/Example -> PathGuard/{package}/DCIM\n"
        "deny Pictures/Private\n"
        "observe DCIM/Camera\n"
        "export DCIM/Camera -> Pictures/Backup @mode=move @media_scan=true\n";

    pathguard::PolicyDocument document;
    pathguard::ParseError error;
    assert(pathguard::ParseRulesIni(input, &document, &error));
    assert(document.schema == 2);
    assert(document.failure_mode == pathguard::FailureMode::kOpen);
    assert(document.apps.size() == 1);
    assert(document.apps[0].media_compat == pathguard::MediaCompat::kHideDenied);
    assert(document.apps[0].mounts.size() == 2);
    assert(document.apps[0].mounts[0].action == pathguard::MountAction::kRedirect);
    assert(document.apps[0].mounts[1].action == pathguard::MountAction::kDeny);
    assert(document.apps[0].events.size() == 2);
    assert(document.apps[0].events[1].options
        == (pathguard::kEventModeMove | pathguard::kEventMediaScan));

    const std::string isolate =
        "schema = 2\nfailure = open\n[com.example.isolated]\n"
        "isolate -> Android/data/{package}/sdcard\n"
        "allow Download/Public\n";
    assert(pathguard::ParseRulesIni(isolate, &document, &error));
    assert(document.apps[0].mounts.size() == 2);
    assert(document.apps[0].mounts[0].action == pathguard::MountAction::kIsolateRoot);
    assert(document.apps[0].mounts[1].action == pathguard::MountAction::kRestore);

    const std::string old_schema =
        "schema = 1\nfailure = open\n[com.example.app]\ndeny Secret\n";
    assert(!pathguard::ParseRulesIni(old_schema, &document, &error));
    assert(error.line == 1);
    const std::string absolute_path =
        "schema = 2\nfailure = open\n[com.example.app]\n"
        "deny /storage/emulated/0/Secret\n";
    assert(!pathguard::ParseRulesIni(absolute_path, &document, &error));
    assert(error.line == 4);
    const std::string closed =
        "schema = 2\nfailure = closed\n[com.example.app]\ndeny Secret\n";
    assert(!pathguard::ParseRulesIni(closed, &document, &error));
    assert(error.line == 2);
    return 0;
}
