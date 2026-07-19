#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pathguard {

enum class RuleAction { kDeny, kRedirect };

struct Rule {
    RuleAction action;
    std::string source;
    std::string target;
    std::size_t line;
};

struct AppPolicy {
    std::string package;
    bool enabled = true;
    std::vector<std::string> users;
    std::vector<std::string> processes;
    std::vector<Rule> rules;
};

struct PolicyDocument {
    int schema = 0;
    std::string failure_mode;
    std::vector<AppPolicy> apps;
};

struct ParseError {
    std::size_t line = 0;
    std::string message;
};

bool ParseRulesIni(std::string_view text, PolicyDocument* document, ParseError* error);

}  // namespace pathguard
