#include "pathguard/policy.h"

#include <charconv>
#include <cctype>
#include <unordered_set>

namespace pathguard {
namespace {

std::string Trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool HasPathTraversal(std::string_view path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view component = path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
        if (component == "." || component == ".." || component.empty() && start != 0) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

bool ValidPath(std::string_view path) {
    return !path.empty() && path.find('\0') == std::string_view::npos
        && !HasPathTraversal(path);
}

bool ParseInteger(std::string_view value, int* output) {
    const std::string trimmed = Trim(value);
    const char* first = trimmed.data();
    const char* last = first + trimmed.size();
    auto result = std::from_chars(first, last, *output);
    return result.ec == std::errc() && result.ptr == last;
}

bool ParseList(std::string_view value, std::vector<std::string>* output) {
    output->clear();
    const std::string trimmed = Trim(value);
    if (trimmed == "*") {
        output->push_back("*");
        return true;
    }
    std::size_t start = 0;
    while (start <= trimmed.size()) {
        const std::size_t end = trimmed.find(',', start);
        const std::string item = Trim(std::string_view(trimmed).substr(start, end == std::string::npos ? trimmed.size() - start : end - start));
        if (item.empty()) {
            return false;
        }
        output->push_back(item);
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return !output->empty();
}

bool SetError(ParseError* error, std::size_t line, std::string message) {
    if (error != nullptr) {
        error->line = line;
        error->message = std::move(message);
    }
    return false;
}

}  // namespace

bool ParseRulesIni(std::string_view text, PolicyDocument* document, ParseError* error) {
    if (document == nullptr) {
        return false;
    }
    *document = {};
    AppPolicy* current = nullptr;
    std::unordered_set<std::string> package_names;
    std::unordered_set<std::string> current_rule_keys;
    bool saw_schema = false;
    std::size_t line_number = 0;
    std::size_t offset = 0;

    while (offset <= text.size()) {
        ++line_number;
        const std::size_t newline = text.find('\n', offset);
        std::string line = Trim(text.substr(offset, newline == std::string_view::npos ? text.size() - offset : newline - offset));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        offset = newline == std::string_view::npos ? text.size() + 1 : newline + 1;

        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']') {
                return SetError(error, line_number, "invalid section header");
            }
            const std::string package = Trim(std::string_view(line).substr(1, line.size() - 2));
            if (package.empty() || package.find_first_of(" \t=\r\n") != std::string::npos) {
                return SetError(error, line_number, "invalid package section");
            }
            if (!package_names.insert(package).second) {
                return SetError(error, line_number, "duplicate package section");
            }
            document->apps.push_back({});
            current = &document->apps.back();
            current->package = package;
            current_rule_keys.clear();
            continue;
        }
        if (line[0] == '-' || line.find("->") != std::string::npos) {
            if (current == nullptr) {
                return SetError(error, line_number, "rule outside package section");
            }
            Rule rule{};
            rule.line = line_number;
            if (line[0] == '-') {
                rule.action = RuleAction::kDeny;
                rule.source = Trim(std::string_view(line).substr(1));
                if (rule.source.empty() || !ValidPath(rule.source)) {
                    return SetError(error, line_number, "invalid deny path");
                }
            } else {
                const std::size_t arrow = line.find("->");
                rule.action = RuleAction::kRedirect;
                rule.source = Trim(std::string_view(line).substr(0, arrow));
                rule.target = Trim(std::string_view(line).substr(arrow + 2));
                if (!ValidPath(rule.source) || !ValidPath(rule.target) || rule.source == rule.target) {
                    return SetError(error, line_number, "invalid redirect paths");
                }
            }
            std::string rule_key;
            rule_key.reserve(rule.source.size() + rule.target.size() + 2);
            rule_key.push_back(rule.action == RuleAction::kDeny ? 'D' : 'R');
            rule_key.append(rule.source);
            rule_key.push_back('\0');
            rule_key.append(rule.target);
            if (!current_rule_keys.insert(std::move(rule_key)).second) {
                return SetError(error, line_number, "duplicate rule");
            }
            current->rules.push_back(std::move(rule));
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            return SetError(error, line_number, "unknown syntax");
        }
        const std::string key = Trim(std::string_view(line).substr(0, equals));
        const std::string value = Trim(std::string_view(line).substr(equals + 1));
        if (key == "schema") {
            if (saw_schema || !ParseInteger(value, &document->schema) || document->schema != 1) {
                return SetError(error, line_number, "unsupported schema");
            }
            saw_schema = true;
        } else if (key == "failure_mode") {
            if (current != nullptr || value != "fail_open_with_alert" && value != "fail_closed") {
                return SetError(error, line_number, "invalid failure_mode");
            }
            document->failure_mode = value;
        } else if (current == nullptr) {
            return SetError(error, line_number, "global option required before package sections");
        } else if (key == "enabled") {
            if (value == "true") current->enabled = true;
            else if (value == "false") current->enabled = false;
            else return SetError(error, line_number, "invalid enabled value");
        } else if (key == "media_compat") {
            if (value == "off") current->media_compat = MediaCompat::kOff;
            else if (value == "query_filter") current->media_compat = MediaCompat::kQueryFilter;
            else return SetError(error, line_number, "invalid media_compat value");
        } else if (key == "users") {
            if (!ParseList(value, &current->users)) return SetError(error, line_number, "invalid users list");
        } else if (key == "processes") {
            if (!ParseList(value, &current->processes)) return SetError(error, line_number, "invalid processes list");
        } else {
            return SetError(error, line_number, "unknown option");
        }
    }

    if (!saw_schema || document->failure_mode.empty() || document->apps.empty()) {
        return SetError(error, line_number, "missing schema, failure_mode, or package section");
    }
    return true;
}

}  // namespace pathguard
