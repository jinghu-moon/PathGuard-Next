#include "pathguard/binary.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <string_view>
#include <utility>

#include "pathguard/policy_v6.h"

namespace pathguard {
namespace {

bool Fail(ParseError* error, std::string message) {
    if (error != nullptr) *error = {0, std::move(message)};
    return false;
}

std::uint32_t UserId(std::string_view value, bool* ok) {
    std::uint32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    *ok = parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
    return result;
}

bool ToPolicyV6(const PolicyDocument& document, PolicyV6* output,
                ParseError* error) {
    if (document.failure_mode != FailureMode::kOpen || document.apps.empty()) {
        return Fail(error, "policy document is not executable");
    }
    output->allow_legacy_mount = document.allow_legacy_string_bind;
    for (const AppPolicy& app : document.apps) {
        PolicyPackageV6 package;
        package.package = app.package;
        package.provider_enabled = app.provider_compat == ProviderCompat::kVirtualize;
        package.all_processes = app.processes.empty()
            || std::find(app.processes.begin(), app.processes.end(), "*") != app.processes.end();
        if (!package.all_processes) package.processes = app.processes;
        package.all_users = std::find(app.users.begin(), app.users.end(), "*") != app.users.end();
        if (!package.all_users) {
            for (const std::string& user : app.users) {
                bool ok = false;
                const std::uint32_t id = UserId(user, &ok);
                if (!ok) return Fail(error, "invalid user id");
                package.users.push_back(id);
            }
        }
        std::map<std::string, std::uint32_t> selectors;
        auto selector_for = [&](const std::string& root) {
            const auto found = selectors.find(root);
            if (found != selectors.end()) return found->second;
            const std::uint32_t id = static_cast<std::uint32_t>(package.selectors.size());
            PolicySelectorV6 selector;
            selector.match_kind = PolicyMatchKind::kLiteralPrefix;
            selector.object_type = PolicyObjectType::kAny;
            selector.root = root;
            package.selectors.push_back(std::move(selector));
            selectors.emplace(root, id);
            return id;
        };
        for (const LogicalMountRule& rule : app.mounts) {
            PolicyActionV6 action;
            action.selector_index = selector_for(rule.visible_path);
            action.kind = rule.action == MountAction::kDeny
                ? PolicyActionKind::kDeny : PolicyActionKind::kRedirect;
            action.domain = PolicyExecutionDomain::kMount;
            if (action.kind == PolicyActionKind::kRedirect) {
                action.target = rule.backing_path;
                action.preserve = PolicyPreserveMode::kRelative;
                action.collision = PolicyCollisionMode::kReject;
                action.reverse = PolicyReverseMode::kStaticUnique;
            }
            package.actions.push_back(std::move(action));
        }
        for (const EventRule& rule : app.events) {
            PolicyActionV6 action;
            action.selector_index = selector_for(rule.source_path);
            action.kind = rule.action == EventAction::kObserve
                ? PolicyActionKind::kObserve : PolicyActionKind::kExport;
            action.domain = PolicyExecutionDomain::kEvent;
            action.options = rule.options;
            if (action.kind == PolicyActionKind::kExport) {
                action.target = rule.target_path;
                action.preserve = PolicyPreserveMode::kRelative;
                action.collision = PolicyCollisionMode::kReject;
            }
            package.actions.push_back(std::move(action));
        }
        output->packages.push_back(std::move(package));
    }
    return true;
}

bool FromPolicyV6(const PolicyV6& input, PolicyDocument* output,
                  ParseError* error) {
    PolicyDocument document;
    document.schema = 2;  // Compatibility DTO only; bytes are always schema 3.
    document.failure_mode = FailureMode::kOpen;
    document.allow_legacy_string_bind = input.allow_legacy_mount;
    for (const PolicyPackageV6& package : input.packages) {
        AppPolicy app;
        app.package = package.package;
        app.provider_compat = package.provider_enabled
            ? ProviderCompat::kVirtualize : ProviderCompat::kOff;
        if (package.all_users) app.users = {"*"};
        else for (std::uint32_t user : package.users) app.users.push_back(std::to_string(user));
        if (package.all_processes) app.processes = {"*"};
        else app.processes = package.processes;
        for (const PolicyActionV6& action : package.actions) {
            if (action.selector_index >= package.selectors.size()) {
                return Fail(error, "invalid selector reference");
            }
            const PolicySelectorV6& selector = package.selectors[action.selector_index];
            if (selector.match_kind != PolicyMatchKind::kLiteralPrefix) {
                return Fail(error, "glob policy cannot be represented by legacy DTO");
            }
            if (action.domain == PolicyExecutionDomain::kMount) {
                LogicalMountRule rule;
                rule.action = action.kind == PolicyActionKind::kDeny
                    ? MountAction::kDeny : MountAction::kRedirect;
                rule.visible_path = selector.root;
                rule.backing_path = action.target;
                rule.depth = static_cast<std::uint16_t>(
                    1 + std::count(rule.visible_path.begin(), rule.visible_path.end(), '/'));
                app.mounts.push_back(std::move(rule));
            } else if (action.domain == PolicyExecutionDomain::kEvent) {
                EventRule rule;
                rule.action = action.kind == PolicyActionKind::kObserve
                    ? EventAction::kObserve : EventAction::kExport;
                rule.source_path = selector.root;
                rule.target_path = action.target;
                rule.options = action.options;
                app.events.push_back(std::move(rule));
            } else {
                return Fail(error, "runtime action cannot be represented by legacy DTO");
            }
        }
        document.apps.push_back(std::move(app));
    }
    *output = std::move(document);
    return true;
}

}  // namespace

bool EncodePolicy(const PolicyDocument& document, std::vector<std::uint8_t>* output,
                  ParseError* error) {
    PolicyV6 policy;
    if (!ToPolicyV6(document, &policy, error)) return false;
    std::string message;
    if (!EncodePolicyV6(policy, output, &message)) return Fail(error, std::move(message));
    return true;
}

bool DecodePolicy(const std::vector<std::uint8_t>& input, PolicyDocument* document,
                  std::uint64_t* content_generation, ParseError* error) {
    if (document == nullptr || content_generation == nullptr) return false;
    PolicyV6 policy;
    const PolicyV6DecodeResult decoded = DecodePolicyV6(input, &policy);
    if (!decoded.ok) return Fail(error, decoded.error);
    if (!FromPolicyV6(policy, document, error)) return false;
    *content_generation = decoded.content_generation;
    return true;
}

std::uint64_t ComputeContentGeneration(const PolicyDocument& document) {
    PolicyV6 policy;
    if (!ToPolicyV6(document, &policy, nullptr)) return 0;
    return ComputePolicyV6ContentGeneration(policy);
}

std::uint64_t ComputePlanGeneration(const AppPolicy& policy,
                                    FailureMode failure_mode) {
    return ComputePlanGeneration(policy, failure_mode, false);
}

std::uint64_t ComputePlanGeneration(const AppPolicy& policy,
                                    FailureMode failure_mode,
                                    bool allow_legacy_string_bind) {
    PolicyDocument document;
    document.schema = 2;
    document.failure_mode = failure_mode;
    document.allow_legacy_string_bind = allow_legacy_string_bind;
    document.apps = {policy};
    PolicyV6 converted;
    if (!ToPolicyV6(document, &converted, nullptr) || converted.packages.size() != 1) return 0;
    return ComputePolicyV6PlanGeneration(converted.packages.front(), allow_legacy_string_bind);
}

}  // namespace pathguard
