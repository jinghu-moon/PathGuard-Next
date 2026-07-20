#include "pathguard/policy.h"

#include <charconv>
#include <cctype>
#include <string_view>
#include <unordered_set>

#include "pathguard/path.h"

namespace pathguard {
namespace {

std::string Trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size()
           && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin
           && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool SetError(ParseError* error, std::size_t line, std::string message) {
    if (error != nullptr) *error = {line, std::move(message)};
    return false;
}

bool ParseInteger(std::string_view value, int* output) {
    const std::string trimmed = Trim(value);
    const char* first = trimmed.data();
    const char* last = first + trimmed.size();
    const auto result = std::from_chars(first, last, *output);
    return result.ec == std::errc() && result.ptr == last;
}

bool ParseList(std::string_view value, std::vector<std::string>* output) {
    std::vector<std::string> parsed;
    std::unordered_set<std::string> unique;
    const std::string trimmed = Trim(value);
    if (trimmed == "*") {
        *output = {"*"};
        return true;
    }
    std::size_t start = 0;
    while (start <= trimmed.size()) {
        const std::size_t end = trimmed.find(',', start);
        std::string item = Trim(std::string_view(trimmed).substr(
            start, end == std::string::npos ? trimmed.size() - start : end - start));
        if (item.empty() || item == "*" || !unique.insert(item).second) return false;
        parsed.push_back(std::move(item));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    *output = std::move(parsed);
    return !output->empty();
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool SplitArrow(std::string_view value, std::string* left, std::string* right) {
    const std::size_t arrow = value.find("->");
    if (arrow == std::string_view::npos || value.find("->", arrow + 2) != std::string_view::npos) {
        return false;
    }
    *left = Trim(value.substr(0, arrow));
    *right = Trim(value.substr(arrow + 2));
    return !left->empty() && !right->empty();
}

bool ValidLogicalSyntax(std::string_view path) {
    std::string normalized;
    return NormalizeLogicalPath(path, &normalized);
}

std::uint16_t PathDepth(std::string_view path) {
    if (path.empty()) return 0;
    std::uint16_t depth = 1;
    for (const char value : path) {
        if (value == '/') ++depth;
    }
    return depth;
}

bool ParseExport(std::string_view body, std::size_t line, EventRule* rule,
                 ParseError* error) {
    std::string source;
    std::string target_and_options;
    if (!SplitArrow(body, &source, &target_and_options)) {
        return SetError(error, line, "invalid export rule");
    }
    const std::size_t options_begin = target_and_options.find(" @");
    const std::string target = Trim(std::string_view(target_and_options).substr(
        0, options_begin == std::string::npos ? target_and_options.size() : options_begin));
    if (!ValidLogicalSyntax(source) || !ValidLogicalSyntax(target)) {
        return SetError(error, line, "invalid export path");
    }

    std::uint32_t options = kEventModeCopy;
    bool saw_mode = false;
    bool saw_media_scan = false;
    if (options_begin != std::string::npos) {
        std::string_view remaining(target_and_options);
        std::size_t offset = options_begin + 1;
        while (offset < remaining.size()) {
            const std::size_t end = remaining.find(' ', offset);
            const std::string_view option = remaining.substr(
                offset, end == std::string_view::npos ? remaining.size() - offset : end - offset);
            if (StartsWith(option, "@mode=")) {
                if (saw_mode) return SetError(error, line, "duplicate export mode");
                saw_mode = true;
                const std::string_view mode = option.substr(6);
                if (mode == "copy") options = (options & ~kEventModeMask) | kEventModeCopy;
                else if (mode == "move") options = (options & ~kEventModeMask) | kEventModeMove;
                else if (mode == "trash") options = (options & ~kEventModeMask) | kEventModeTrash;
                else return SetError(error, line, "invalid export mode");
            } else if (StartsWith(option, "@media_scan=")) {
                if (saw_media_scan) {
                    return SetError(error, line, "duplicate media_scan option");
                }
                saw_media_scan = true;
                const std::string_view enabled = option.substr(12);
                if (enabled == "true") options |= kEventMediaScan;
                else if (enabled != "false") {
                    return SetError(error, line, "invalid media_scan option");
                }
            } else {
                return SetError(error, line, "unknown export option");
            }
            if (end == std::string_view::npos) break;
            offset = end + 1;
            while (offset < remaining.size() && remaining[offset] == ' ') ++offset;
        }
    }
    *rule = {EventAction::kExport, source, target, options, line};
    return true;
}

}  // namespace

bool ParseRulesIni(std::string_view text, PolicyDocument* document, ParseError* error) {
    if (document == nullptr) return false;
    *document = {};
    AppPolicy* current = nullptr;
    std::unordered_set<std::string> package_names;
    bool saw_schema = false;
    bool saw_failure = false;
    std::size_t line_number = 0;
    std::size_t offset = 0;

    while (offset <= text.size()) {
        ++line_number;
        const std::size_t newline = text.find('\n', offset);
        std::string line = Trim(text.substr(
            offset, newline == std::string_view::npos ? text.size() - offset : newline - offset));
        offset = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        if (line.empty() || line[0] == '#') continue;

        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']') {
                return SetError(error, line_number, "invalid section header");
            }
            const std::string package = Trim(
                std::string_view(line).substr(1, line.size() - 2));
            if (package.empty() || package.find_first_of("/{} =\t\r\n") != std::string::npos) {
                return SetError(error, line_number, "invalid package section");
            }
            if (!package_names.insert(package).second) {
                return SetError(error, line_number, "duplicate package section");
            }
            document->apps.push_back({});
            current = &document->apps.back();
            current->package = package;
            continue;
        }

        if (StartsWith(line, "deny ") || StartsWith(line, "redirect ")
            || StartsWith(line, "isolate ") || StartsWith(line, "allow ")
            || StartsWith(line, "observe ") || StartsWith(line, "export ")) {
            if (current == nullptr) {
                return SetError(error, line_number, "rule outside package section");
            }
            if (StartsWith(line, "deny ")) {
                const std::string path = Trim(std::string_view(line).substr(5));
                if (!ValidLogicalSyntax(path)) {
                    return SetError(error, line_number, "invalid deny path");
                }
                current->mounts.push_back({MountAction::kDeny, path, "",
                                           PathDepth(path), 0, line_number});
            } else if (StartsWith(line, "redirect ")) {
                std::string visible;
                std::string backing;
                if (!SplitArrow(std::string_view(line).substr(9), &visible, &backing)
                    || !ValidLogicalSyntax(visible) || !ValidLogicalSyntax(backing)) {
                    return SetError(error, line_number, "invalid redirect rule");
                }
                current->mounts.push_back({MountAction::kRedirect, visible, backing,
                                           PathDepth(visible), 0, line_number});
            } else if (StartsWith(line, "isolate ")) {
                std::string keyword;
                std::string backing;
                if (!SplitArrow(line, &keyword, &backing) || keyword != "isolate"
                    || !ValidLogicalSyntax(backing)) {
                    return SetError(error, line_number, "invalid isolate rule");
                }
                current->mounts.push_back({MountAction::kIsolateRoot, "", backing,
                                           0, 0, line_number});
            } else if (StartsWith(line, "allow ")) {
                const std::string path = Trim(std::string_view(line).substr(6));
                if (!ValidLogicalSyntax(path)) {
                    return SetError(error, line_number, "invalid allow path");
                }
                current->mounts.push_back({MountAction::kRestore, path, path,
                                           PathDepth(path), 0, line_number});
            } else if (StartsWith(line, "observe ")) {
                const std::string path = Trim(std::string_view(line).substr(8));
                if (!ValidLogicalSyntax(path)) {
                    return SetError(error, line_number, "invalid observe path");
                }
                current->events.push_back(
                    {EventAction::kObserve, path, "", 0, line_number});
            } else {
                EventRule rule;
                if (!ParseExport(std::string_view(line).substr(7), line_number,
                                 &rule, error)) {
                    return false;
                }
                current->events.push_back(std::move(rule));
            }
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            return SetError(error, line_number, "unknown syntax");
        }
        const std::string key = Trim(std::string_view(line).substr(0, equals));
        const std::string value = Trim(std::string_view(line).substr(equals + 1));
        if (key == "schema") {
            if (current != nullptr || saw_schema || !ParseInteger(value, &document->schema)
                || document->schema != 2) {
                return SetError(error, line_number, "unsupported schema");
            }
            saw_schema = true;
        } else if (key == "failure") {
            if (current != nullptr || saw_failure) {
                return SetError(error, line_number, "invalid failure option");
            }
            saw_failure = true;
            if (value == "open") document->failure_mode = FailureMode::kOpen;
            else if (value == "closed") {
                return SetError(error, line_number, "failure=closed is not executable");
            } else {
                return SetError(error, line_number, "invalid failure mode");
            }
        } else if (current == nullptr) {
            return SetError(error, line_number,
                            "global option required before package sections");
        } else if (key == "users") {
            if (!ParseList(value, &current->users)) {
                return SetError(error, line_number, "invalid users list");
            }
        } else if (key == "processes") {
            if (!ParseList(value, &current->processes)) {
                return SetError(error, line_number, "invalid processes list");
            }
        } else if (key == "media") {
            if (value == "off") current->media_compat = MediaCompat::kOff;
            else if (value == "hide_denied") {
                current->media_compat = MediaCompat::kHideDenied;
            } else {
                return SetError(error, line_number, "unsupported media mode");
            }
        } else {
            return SetError(error, line_number, "unknown option");
        }
    }

    if (!saw_schema || !saw_failure || document->apps.empty()) {
        return SetError(error, line_number,
                        "missing schema, failure, or package section");
    }
    return true;
}

}  // namespace pathguard
