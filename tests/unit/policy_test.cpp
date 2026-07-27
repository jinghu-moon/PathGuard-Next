#include "pathguard/policy.h"
#include "pathguard/validation.h"
#include "test_assert.h"

int main() {
    pathguard::AppPolicy app;
    app.package = "com.example.app";
    app.users = {"0"};
    app.processes = {"*"};
    app.mounts.push_back({pathguard::MountAction::kRedirect,
                          "Download/Source", "PathGuard/Target", 0, 0, 0});
    pathguard::ParseError error;
    assert(pathguard::ValidatePolicy(&app, &error));
    assert(app.mounts.front().depth == 2);

    pathguard::AppPolicy provider_without_redirect;
    provider_without_redirect.package = "com.example.provider";
    provider_without_redirect.users = {"0"};
    provider_without_redirect.provider_compat =
        pathguard::ProviderCompat::kVirtualize;
    assert(!pathguard::ValidatePolicy(&provider_without_redirect, &error));

    pathguard::AppPolicy provider_wildcard = app;
    provider_wildcard.users = {"*"};
    provider_wildcard.provider_compat = pathguard::ProviderCompat::kVirtualize;
    assert(!pathguard::ValidatePolicy(&provider_wildcard, &error));

    pathguard::AppPolicy duplicate = app;
    duplicate.mounts.push_back(duplicate.mounts.front());
    assert(!pathguard::ValidatePolicy(&duplicate, &error));
    return 0;
}
