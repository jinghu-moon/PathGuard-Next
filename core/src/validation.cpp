#include "pathguard/validation.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "pathguard/path.h"

namespace pathguard {
namespace {

bool SetError(ParseError* error, std::size_t line, std::string message) {
    if (error != nullptr) *error = {line, std::move(message)};
    return false;
}

bool ContainsPlaceholder(std::string_view path) {
    return path.find('{') != std::string_view::npos
        || path.find('}') != std::string_view::npos;
}

bool NormalizeBacking(std::string_view input, std::string_view package,
                      std::string* output) {
    std::string expanded;
    return ExpandPathPlaceholders(input, package, &expanded)
        && NormalizeLogicalPath(expanded, output);
}

bool NormalizeVisible(std::string_view input, std::string* output) {
    return !ContainsPlaceholder(input) && NormalizeLogicalPath(input, output);
}

bool NormalizeUsers(std::vector<std::string>* users) {
    if (users->size() == 1 && users->front() == "*") return true;
    for (const std::string& user : *users) {
        unsigned value = 0;
        const auto result = std::from_chars(user.data(), user.data() + user.size(), value);
        if (result.ec != std::errc() || result.ptr != user.data() + user.size()) return false;
    }
    std::sort(users->begin(), users->end(), [](const std::string& lhs,
                                                const std::string& rhs) {
        unsigned lhs_value = 0;
        unsigned rhs_value = 0;
        std::from_chars(lhs.data(), lhs.data() + lhs.size(), lhs_value);
        std::from_chars(rhs.data(), rhs.data() + rhs.size(), rhs_value);
        return lhs_value < rhs_value;
    });
    return true;
}

int MountStage(MountAction action) {
    switch (action) {
        case MountAction::kIsolateRoot: return 0;
        case MountAction::kRestore: return 1;
        case MountAction::kRedirect: return 2;
        case MountAction::kDeny: return 3;
    }
    return 4;
}

}  // namespace

bool ValidatePolicy(AppPolicy* policy, ParseError* error) {
    if (policy == nullptr || policy->package.empty()) return false;
    if (!NormalizeUsers(&policy->users)) {
        return SetError(error, 0, "invalid Android user list");
    }
    std::sort(policy->processes.begin(), policy->processes.end());

    LogicalMountRule* isolate = nullptr;
    std::unordered_map<std::string, std::size_t> visible_sources;
    visible_sources.reserve(policy->mounts.size());
    for (LogicalMountRule& rule : policy->mounts) {
        if (rule.action == MountAction::kIsolateRoot) {
            if (isolate != nullptr) {
                return SetError(error, rule.line, "multiple isolate rules");
            }
            std::string normalized_backing;
            if (!NormalizeBacking(rule.backing_path, policy->package,
                                  &normalized_backing)) {
                return SetError(error, rule.line, "invalid isolate backing path");
            }
            rule.visible_path.clear();
            rule.backing_path = std::move(normalized_backing);
            rule.depth = 0;
            isolate = &rule;
            continue;
        }

        std::string normalized_visible;
        if (!NormalizeVisible(rule.visible_path, &normalized_visible)) {
            return SetError(error, rule.line, "invalid visible path");
        }
        rule.visible_path = std::move(normalized_visible);
        rule.depth = 1;
        for (const char value : rule.visible_path) {
            if (value == '/') ++rule.depth;
        }
        if (!visible_sources.emplace(rule.visible_path, rule.line).second) {
            return SetError(error, rule.line, "conflicting synchronous action");
        }

        if (rule.action == MountAction::kRedirect) {
            std::string normalized_backing;
            if (!NormalizeBacking(rule.backing_path, policy->package,
                                  &normalized_backing)) {
                return SetError(error, rule.line, "invalid redirect backing path");
            }
            rule.backing_path = std::move(normalized_backing);
        } else if (rule.action == MountAction::kRestore) {
            rule.backing_path = rule.visible_path;
        } else if (rule.action == MountAction::kDeny) {
            rule.backing_path.clear();
        }
    }

    for (const LogicalMountRule& rule : policy->mounts) {
        if (rule.action == MountAction::kRestore && isolate == nullptr) {
            return SetError(error, rule.line, "allow requires isolate");
        }
        if (isolate != nullptr && rule.action != MountAction::kIsolateRoot
            && (IsPathOrDescendant(rule.visible_path, isolate->backing_path)
                || IsPathOrDescendant(isolate->backing_path, rule.visible_path))) {
            return SetError(error, rule.line,
                            "visible path conflicts with isolate backing path");
        }
    }

    if (policy->provider_compat == ProviderCompat::kVirtualize) {
        if (policy->users.size() == 1 && policy->users.front() == "*") {
            return SetError(error, 0,
                            "provider virtualization requires explicit users");
        }
        const bool has_redirect = std::any_of(
            policy->mounts.begin(), policy->mounts.end(),
            [](const LogicalMountRule& rule) {
                return rule.action == MountAction::kRedirect;
            });
        if (!has_redirect) {
            return SetError(error, 0,
                            "provider virtualization requires redirect rules");
        }
    }

    std::unordered_set<std::string> event_keys;
    event_keys.reserve(policy->events.size());
    for (EventRule& rule : policy->events) {
        std::string normalized_source;
        if (!NormalizeVisible(rule.source_path, &normalized_source)) {
            return SetError(error, rule.line, "invalid event source path");
        }
        rule.source_path = std::move(normalized_source);
        if (rule.action == EventAction::kExport) {
            std::string normalized_target;
            if (!NormalizeBacking(rule.target_path, policy->package,
                                  &normalized_target)
                || IsPathOrDescendant(normalized_target, rule.source_path)) {
                return SetError(error, rule.line, "invalid export target path");
            }
            rule.target_path = std::move(normalized_target);
            if ((rule.options & kEventModeMask) > kEventModeTrash
                || (rule.options & ~(kEventModeMask | kEventMediaScan)) != 0) {
                return SetError(error, rule.line, "invalid export options");
            }
        } else {
            rule.target_path.clear();
            rule.options = 0;
        }
        std::string key;
        key.reserve(rule.source_path.size() + rule.target_path.size() + 16);
        key.push_back(static_cast<char>(rule.action));
        key.append(rule.source_path);
        key.push_back('\0');
        key.append(rule.target_path);
        key.append(reinterpret_cast<const char*>(&rule.options), sizeof(rule.options));
        if (!event_keys.insert(std::move(key)).second) {
            return SetError(error, rule.line, "duplicate event rule");
        }
    }

    std::sort(policy->mounts.begin(), policy->mounts.end(),
              [](const LogicalMountRule& lhs, const LogicalMountRule& rhs) {
        return std::tuple(MountStage(lhs.action), lhs.depth, lhs.visible_path,
                          lhs.backing_path, lhs.flags)
            < std::tuple(MountStage(rhs.action), rhs.depth, rhs.visible_path,
                         rhs.backing_path, rhs.flags);
    });
    std::sort(policy->events.begin(), policy->events.end(),
              [](const EventRule& lhs, const EventRule& rhs) {
        return std::tuple(lhs.action, lhs.source_path, lhs.target_path, lhs.options)
            < std::tuple(rhs.action, rhs.source_path, rhs.target_path, rhs.options);
    });
    return true;
}

}  // namespace pathguard
