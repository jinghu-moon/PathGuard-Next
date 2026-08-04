#include "pathguard/rules/selector_builder.h"
#include "pathguard/policy_format.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace pathguard::rules {
namespace {

using namespace pathguard::pattern;

void AppendSized(std::string* output, std::string_view value) {
    output->append(std::to_string(value.size()));
    output->push_back(':');
    output->append(value);
}

std::string SelectorKey(const CanonicalSelectorV2& selector) {
    std::string key;
    AppendSized(&key, selector.root);
    AppendSized(&key, selector.base_pattern.canonical);
    key.push_back(static_cast<char>(selector.object_type));
    key.append(std::to_string(selector.except_patterns.size()));
    key.push_back(':');
    for (const PatternProgram& except : selector.except_patterns) {
        AppendSized(&key, except.canonical);
    }
    return key;
}

std::uint64_t HashBytes(std::uint64_t hash, std::string_view bytes) {
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

ObjectType MapObjectType(SelectorObjectType value) {
    switch (value) {
        case SelectorObjectType::kFile: return ObjectType::kFile;
        case SelectorObjectType::kDirectory: return ObjectType::kDirectory;
        case SelectorObjectType::kAny: return ObjectType::kAny;
    }
    return ObjectType::kAny;
}

std::string FirstLiteralComponent(const PatternProgram& program) {
    if (program.components.empty() || program.components.front().globstar) return {};
    std::string output;
    for (const PatternToken& token : program.components.front().tokens) {
        if (token.kind != PatternTokenKind::kLiteral) return {};
        output += token.literal;
    }
    return output;
}

std::string FixedExtension(const PatternProgram& program) {
    if (program.components.empty()) return {};
    const PatternComponent& component = program.components.back();
    if (component.globstar || component.tokens.empty()) return {};
    const PatternToken& last = component.tokens.back();
    if (last.kind != PatternTokenKind::kLiteral) return {};
    const std::size_t dot = last.literal.rfind('.');
    if (dot == std::string::npos || dot + 1 == last.literal.size()) return {};
    return last.literal.substr(dot + 1);
}

ExecutionDomain SelectDomain(const CanonicalActionV2& action) {
    if (action.action == RuleActionKind::kObserve
        || action.action == RuleActionKind::kExport) {
        return ExecutionDomain::kEvent;
    }
    if (action.action == RuleActionKind::kDeny) {
        return action.enforcement == RuleEnforcement::kComplete
            ? ExecutionDomain::kCompleteVfs : ExecutionDomain::kProvider;
    }
    if (action.enforcement == RuleEnforcement::kProvider) {
        return ExecutionDomain::kProvider;
    }
    if (action.enforcement == RuleEnforcement::kComplete) {
        return ExecutionDomain::kCompleteVfs;
    }
    if (action.selector.source_kind == SelectorSourceKind::kLiteral
        && action.selector.object_type == SelectorObjectType::kAny) {
        return ExecutionDomain::kMount;
    }
    return ExecutionDomain::kAppPath;
}

void SetRequirements(const CanonicalActionV2& source, PlanAction* action) {
    if (action == nullptr) return;
    switch (action->domain) {
        case ExecutionDomain::kMount:
            break;
        case ExecutionDomain::kAppPath:
            action->required_capabilities = kCapabilityAppPathAdapter;
            action->required_operations =
                    source.action == RuleActionKind::kRedirect
                    && source.selector.object_type == SelectorObjectType::kFile
                ? kAppPathFileRedirectOperationsV1
                : kAppPathOperationsV1;
            break;
        case ExecutionDomain::kProvider:
            action->required_capabilities = kCapabilityProviderCallerUid
                | kCapabilityProviderQueryInsertMapping;
            action->required_operations = kProviderCompositeOperationsV1;
            break;
        case ExecutionDomain::kCompleteVfs:
            action->required_capabilities = kCapabilityFuseCompletePath;
            action->required_operations = kCompleteVfsOperationsV1;
            break;
        case ExecutionDomain::kEvent:
            action->required_operations = kOperationCloseWriteEvent;
            break;
    }
}

}  // namespace

PatternPlanBuildResult BuildPatternPlan(
    const CanonicalPolicyV2& policy,
    const std::vector<PackageIdentityBinding>& bindings) {
    PatternPlanBuildResult result;
    PatternPlan plan;

    std::vector<std::string> package_names;
    for (const CanonicalAppPolicyV2& app : policy.apps) {
        package_names.push_back(app.package);
    }
    std::sort(package_names.begin(), package_names.end());
    package_names.erase(std::unique(package_names.begin(), package_names.end()),
                        package_names.end());
    std::unordered_map<std::string, PackageId> package_ids;
    for (const std::string& package : package_names) {
        const PackageId id = static_cast<PackageId>(plan.packages.size());
        plan.packages.push_back({id, package});
        package_ids.emplace(package, id);
    }

    std::map<std::string, const CanonicalSelectorV2*> selectors;
    for (const CanonicalAppPolicyV2& app : policy.apps) {
        for (const CanonicalActionV2& action : app.actions) {
            selectors.emplace(SelectorKey(action.selector), &action.selector);
        }
    }
    std::unordered_map<std::string, SelectorId> selector_ids;
    for (const auto& [key, source] : selectors) {
        const SelectorId id = static_cast<SelectorId>(plan.selectors.size());
        PlanSelector selector;
        selector.id = id;
        selector.root = source->root;
        selector.base = source->base_pattern;
        selector.except = source->except_patterns;
        selector.object_type = MapObjectType(source->object_type);
        selector.specificity = source->specificity;
        selector.first_literal_component = FirstLiteralComponent(selector.base);
        selector.fixed_extension = selector.first_literal_component.empty()
            ? FixedExtension(selector.base) : std::string{};
        plan.selectors.push_back(std::move(selector));
        selector_ids.emplace(key, id);
    }

    for (const CanonicalAppPolicyV2& app : policy.apps) {
        const auto package_found = package_ids.find(app.package);
        if (package_found == package_ids.end()) {
            result.errors.push_back("package id missing: " + app.package);
            continue;
        }
        for (const CanonicalActionV2& source : app.actions) {
            const std::string selector_key = SelectorKey(source.selector);
            const auto selector_found = selector_ids.find(selector_key);
            if (selector_found == selector_ids.end()) {
                result.errors.push_back("selector id missing");
                continue;
            }
            PlanAction action;
            action.package_id = package_found->second;
            action.selector_id = selector_found->second;
            switch (source.action) {
                case RuleActionKind::kDeny:
                    action.kind = RuntimeActionKind::kDeny;
                    break;
                case RuleActionKind::kRedirect:
                    action.kind = RuntimeActionKind::kRedirect;
                    break;
                case RuleActionKind::kObserve:
                    action.kind = RuntimeActionKind::kObserve;
                    break;
                case RuleActionKind::kExport:
                    action.kind = RuntimeActionKind::kExport;
                    break;
            }
            action.domain = SelectDomain(source);
            action.target = source.target;
            action.priority = source.priority;
            action.options = static_cast<std::uint32_t>(source.export_mode)
                | (source.media_scan ? UINT32_C(1) << 2 : 0)
                | (source.audit
                    ? pathguard::binary_format::kActionOptionPrivateAudit : 0);
            SetRequirements(source, &action);
            std::uint64_t rule_hash = UINT64_C(14695981039346656037);
            rule_hash = HashBytes(rule_hash, app.package);
            rule_hash = HashBytes(rule_hash, selector_key);
            rule_hash = HashBytes(
                rule_hash,
                std::to_string(static_cast<std::uint8_t>(source.action)));
            rule_hash = HashBytes(rule_hash, source.target);
            rule_hash = HashBytes(rule_hash, std::to_string(source.priority));
            action.rule_id = rule_hash;
            plan.actions.push_back(std::move(action));
        }
    }
    std::sort(plan.actions.begin(), plan.actions.end(),
        [](const PlanAction& lhs, const PlanAction& rhs) {
            return std::tie(lhs.package_id, lhs.selector_id, lhs.kind,
                            lhs.priority, lhs.rule_id)
                < std::tie(rhs.package_id, rhs.selector_id, rhs.kind,
                           rhs.priority, rhs.rule_id);
        });
    for (std::size_t index = 0; index < plan.actions.size(); ++index) {
        plan.actions[index].id = static_cast<ActionId>(index);
    }

    std::set<std::tuple<std::int32_t, std::uint32_t, PackageId, bool>> unique_scopes;
    for (const PackageIdentityBinding& binding : bindings) {
        const auto package_found = package_ids.find(binding.package);
        if (package_found == package_ids.end()) {
            result.errors.push_back("binding package missing: " + binding.package);
            continue;
        }
        const auto key = std::tuple(binding.caller_uid, binding.user_id,
                                    package_found->second,
                                    binding.requires_package_attribution);
        if (!unique_scopes.insert(key).second) continue;
        plan.scopes.push_back({{binding.caller_uid, binding.user_id},
                               package_found->second,
                               binding.requires_package_attribution});
    }
    std::sort(plan.scopes.begin(), plan.scopes.end(),
        [](const ScopeGrant& lhs, const ScopeGrant& rhs) {
            return std::tie(lhs.identity.caller_uid, lhs.identity.user_id,
                            lhs.package_id, lhs.requires_package_attribution)
                < std::tie(rhs.identity.caller_uid, rhs.identity.user_id,
                           rhs.package_id, rhs.requires_package_attribution);
        });

    std::uint64_t generation = UINT64_C(14695981039346656037);
    for (const PlanPackage& package : plan.packages) {
        generation = HashBytes(generation, package.name);
    }
    for (const PlanSelector& selector : plan.selectors) {
        generation = HashBytes(generation, selector.root);
        generation = HashBytes(generation, selector.base.canonical);
        for (const PatternProgram& except : selector.except) {
            generation = HashBytes(generation, except.canonical);
        }
    }
    for (const PlanAction& action : plan.actions) {
        generation = HashBytes(generation, std::to_string(action.rule_id));
        generation = HashBytes(generation, action.target);
    }
    plan.plan_generation = generation;
    if (result.errors.empty()) result.plan = std::move(plan);
    return result;
}

}  // namespace pathguard::rules
