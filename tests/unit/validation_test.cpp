#include <string>

#include "pathguard/policy.h"
#include "pathguard/validation.h"
#include "test_assert.h"

int main() {
    pathguard::ParseError error;

    pathguard::AppPolicy chained;
    chained.package = "com.example.app";
    chained.rules = {
        {pathguard::RuleAction::kRedirect, "/storage/emulated/0/Download",
         "/storage/emulated/0/Sandbox", 10},
        {pathguard::RuleAction::kRedirect, "/storage/emulated/0/Sandbox",
         "/storage/emulated/0/Other", 11},
    };
    assert(!pathguard::ValidatePolicy(&chained, &error));
    assert(error.line == 11);
    assert(error.message == "redirect cycle");

    pathguard::AppPolicy parent_deny;
    parent_deny.package = "com.example.app";
    parent_deny.rules = {
        {pathguard::RuleAction::kDeny, "/storage/emulated/0/Secret", "", 20},
        {pathguard::RuleAction::kRedirect, "/storage/emulated/0/Secret/Child",
         "/storage/emulated/0/Sandbox", 21},
    };
    assert(!pathguard::ValidatePolicy(&parent_deny, &error));
    assert(error.line == 21);
    assert(error.message == "parent rule conflicts with child rule");

    pathguard::AppPolicy sibling;
    sibling.package = "com.example.app";
    sibling.rules = {
        {pathguard::RuleAction::kDeny, "/storage/emulated/0/Secret/Child", "", 30},
        {pathguard::RuleAction::kRedirect, "/storage/emulated/0/Secret",
         "/storage/emulated/0/Sandbox", 31},
    };
    assert(pathguard::ValidatePolicy(&sibling, &error));
    return 0;
}
