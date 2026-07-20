#include <string>

#include "pathguard/policy.h"
#include "test_assert.h"

int main() {
    const std::string input =
        "schema = 1\n"
        "failure_mode = fail_open_with_alert\n"
        "\n"
        "[com.example.app]\n"
        "enabled = true\n"
        "media_compat = query_filter\n"
        "users = 0, 10\n"
        "processes = *\n"
        "- Secret\n"
        "Download/AppCache -> PathGuard/{package}/AppCache\n";

    pathguard::PolicyDocument document;
    pathguard::ParseError error;
    assert(pathguard::ParseRulesIni(input, &document, &error));
    assert(document.schema == 1);
    assert(document.apps.size() == 1);
    assert(document.apps[0].media_compat == pathguard::MediaCompat::kQueryFilter);
    assert(document.apps[0].rules.size() == 2);
    assert(document.apps[0].rules[0].action == pathguard::RuleAction::kDeny);
    assert(document.apps[0].rules[1].action == pathguard::RuleAction::kRedirect);

    const std::string invalid = "schema = 1\nfailure_mode = fail_open_with_alert\n[com.example.app]\n- ../Secret\n";
    assert(!pathguard::ParseRulesIni(invalid, &document, &error));
    assert(error.line == 4);
    const std::string invalid_media =
        "schema = 1\nfailure_mode = fail_open_with_alert\n"
        "[com.example.app]\nmedia_compat = unknown\n";
    assert(!pathguard::ParseRulesIni(invalid_media, &document, &error));
    assert(error.line == 4);
    return 0;
}
