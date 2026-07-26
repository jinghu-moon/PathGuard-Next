#include <filesystem>
#include <fstream>
#include <string>

#include "pathguard/policy.h"
#include "pathguard/validation.h"
#include "test_assert.h"

namespace fs = std::filesystem;

namespace {

std::string ReadFixture(const char* name) {
    const fs::path path = fs::path(PATHGUARD_SOURCE_DIR) / "tests" / "fixtures"
        / "legacy-rules" / name;
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    pathguard::PolicyDocument document;
    pathguard::ParseError error;
    assert(pathguard::ParseRulesIni(
        ReadFixture("valid-full-syntax.ini"), &document, &error));
    assert(document.schema == 2);
    assert(document.failure_mode == pathguard::FailureMode::kOpen);
    assert(document.allow_legacy_string_bind);
    assert(document.apps.size() == 1);
    assert(document.apps[0].media_compat == pathguard::MediaCompat::kHideDenied);
    assert(document.apps[0].provider_compat == pathguard::ProviderCompat::kVirtualize);
    assert(document.apps[0].mounts.size() == 2);
    assert(document.apps[0].mounts[0].action == pathguard::MountAction::kRedirect);
    assert(document.apps[0].mounts[1].action == pathguard::MountAction::kDeny);
    assert(document.apps[0].events.size() == 2);
    assert(document.apps[0].events[1].options
        == (pathguard::kEventModeMove | pathguard::kEventMediaScan));

    assert(pathguard::ParseRulesIni(
        ReadFixture("legacy-isolate.ini"), &document, &error));
    assert(document.apps[0].mounts.size() == 2);
    assert(document.apps[0].mounts[0].action == pathguard::MountAction::kIsolateRoot);
    assert(document.apps[0].mounts[1].action == pathguard::MountAction::kRestore);

    assert(!pathguard::ParseRulesIni(
        ReadFixture("invalid-schema.ini"), &document, &error));
    assert(error.line == 1);
    assert(!pathguard::ParseRulesIni(
        ReadFixture("absolute-path.ini"), &document, &error));
    assert(error.line == 4);
    assert(!pathguard::ParseRulesIni(
        ReadFixture("failure-closed.ini"), &document, &error));
    assert(error.line == 2);
    assert(!pathguard::ParseRulesIni(
        ReadFixture("invalid-legacy-bool.ini"), &document, &error));
    assert(error.line == 3);

    assert(pathguard::ParseRulesIni(
        ReadFixture("provider-without-redirect.ini"), &document, &error));
    assert(!pathguard::ValidatePolicy(&document.apps[0], &error));

    assert(pathguard::ParseRulesIni(
        ReadFixture("provider-wildcard-user.ini"), &document, &error));
    assert(!pathguard::ValidatePolicy(&document.apps[0], &error));
    return 0;
}
