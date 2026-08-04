#include "pathguard/rules/semantic.h"
#include "pathguard/policy_format.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace pathguard::rules {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t ElapsedNs(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start).count());
}

bool HasErrors(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& diagnostic) {
                           return diagnostic.severity == DiagnosticSeverity::kError;
                       });
}

using CanonicalActionKey = std::tuple<
    RuleActionKind, std::string, std::string, SelectorObjectType, std::string,
    std::int32_t, RuleEnforcement, ExportMode, bool, bool, std::string>;

CanonicalActionKey ActionKey(const CanonicalActionV2& action) {
    std::string except_key;
    for (const auto& except : action.selector.except_patterns) {
        except_key.append(except.canonical);
        except_key.push_back('\0');
    }
    return {action.action, action.selector.root, action.selector.glob,
            action.selector.object_type, action.target, action.priority,
            action.enforcement, action.export_mode, action.media_scan,
            action.audit,
            std::move(except_key)};
}

void AddDiagnostic(std::vector<Diagnostic>* diagnostics,
                   std::string_view code, std::string_view message,
                   ByteSpan primary, DiagnosticSeverity severity,
                   const RulesLimits& limits,
                   std::optional<ByteSpan> related = std::nullopt);

void AddDiagnostic(std::vector<Diagnostic>* diagnostics,
                   std::string_view code, std::string_view message,
                   ByteSpan primary, DiagnosticSeverity severity,
                   const RulesLimits& limits,
                   std::optional<ByteSpan> related) {
    if (diagnostics->size() >= limits.max_diagnostics) return;
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message_key = message;
    diagnostic.primary = primary;
    diagnostic.severity = severity;
    diagnostic.phase = DiagnosticPhase::kSemantic;
    if (related.has_value() && limits.max_related_spans != 0) {
        diagnostic.related.push_back({*related, "related_rule"});
    }
    diagnostics->push_back(std::move(diagnostic));
}

}  // namespace

bool RulesBuildResult::ok() const {
    return canonical_v2.has_value() && policy_v6.has_value() && blob.has_value()
        && !HasErrors(diagnostics);
}

std::optional<NormalizedPath> NormalizeRulePath(
        std::string_view input, const RulesLimits& limits,
        CompileStatistics* statistics) {
    if (statistics != nullptr) ++statistics->path_normalizations;
    if (input.empty() || input.size() > limits.max_path_bytes
        || input.front() == '/') {
        return std::nullopt;
    }
    NormalizedPath output;
    output.bytes.assign(input);
    std::size_t component_begin = 0;
    while (component_begin < input.size()) {
        const std::size_t separator = input.find('/', component_begin);
        const std::size_t component_end = separator == std::string_view::npos
            ? input.size() : separator;
        const std::string_view component = input.substr(
            component_begin, component_end - component_begin);
        if (component.empty() || component == "." || component == ".."
            || output.component_offsets.size() >= limits.max_path_components
            || component_begin > std::numeric_limits<std::uint16_t>::max()) {
            return std::nullopt;
        }
        for (const unsigned char byte : component) {
            if (byte == 0 || byte < 0x20 || byte == 0x7f) return std::nullopt;
        }
        output.component_offsets.push_back(
            static_cast<std::uint16_t>(component_begin));
        if (separator == std::string_view::npos) break;
        component_begin = separator + 1;
    }
    return output;
}

bool IsSameOrAncestor(const NormalizedPath& ancestor,
                      const NormalizedPath& path) {
    return path.bytes == ancestor.bytes
        || (path.bytes.size() > ancestor.bytes.size()
            && path.bytes.compare(0, ancestor.bytes.size(), ancestor.bytes) == 0
            && path.bytes[ancestor.bytes.size()] == '/');
}

RulesBuildResult CompileRules(const SourceBuffer& source,
                              const RulesLimits& limits) {
    RulesBuildResult output;
    RulesV2ParseResult parsed = ParseRulesDocumentV2(source, limits);
    output.diagnostics = std::move(parsed.diagnostics);
    if (!parsed.ok()) return output;
    RulesV2BuildResult built = BuildCanonicalPolicyV2(*parsed.document, limits);
    output.diagnostics.insert(output.diagnostics.end(),
                              built.diagnostics.begin(), built.diagnostics.end());
    if (!built.ok() || HasErrors(output.diagnostics)) return output;

    for (CanonicalAppPolicyV2& app : built.canonical->apps) {
        std::set<CanonicalActionKey> seen;
        std::vector<CanonicalActionV2> unique;
        unique.reserve(app.actions.size());
        for (CanonicalActionV2& action : app.actions) {
            if (!seen.insert(ActionKey(action)).second) {
                AddDiagnostic(&output.diagnostics, kRuleRedundant,
                              "rules.rule_redundant", {},
                              DiagnosticSeverity::kWarning, limits);
                continue;
            }
            unique.push_back(std::move(action));
        }
        app.actions = std::move(unique);
    }

    pathguard::PolicyV6 policy;
    policy.allow_legacy_mount = built.canonical->allow_legacy_mount;
    for (const CanonicalAppPolicyV2& source_app : built.canonical->apps) {
        pathguard::PolicyPackageV6 package;
        package.package = source_app.package;
        package.all_processes = source_app.processes.empty();
        package.processes = source_app.processes;
        package.provider_enabled = source_app.provider.enabled;
        for (std::int32_t user : source_app.users) {
            package.users.push_back(static_cast<std::uint32_t>(user));
        }
        std::map<std::string, std::uint32_t> selector_ids;
        for (const CanonicalActionV2& source_action : source_app.actions) {
            const std::string selector_root = source_action.selector.source_kind
                    == SelectorSourceKind::kLiteral
                ? source_action.selector.root + "/" + source_action.selector.glob
                : source_action.selector.root;
            std::string selector_key = selector_root;
            selector_key.push_back('\0');
            selector_key.push_back(static_cast<char>(source_action.selector.source_kind));
            selector_key.push_back(static_cast<char>(source_action.selector.object_type));
            selector_key.append(source_action.selector.base_pattern.canonical);
            for (const auto& except : source_action.selector.except_patterns) {
                selector_key.push_back('\0');
                selector_key.append(except.canonical);
            }
            auto found = selector_ids.find(selector_key);
            std::uint32_t selector_id = 0;
            if (found == selector_ids.end()) {
                selector_id = static_cast<std::uint32_t>(package.selectors.size());
                pathguard::PolicySelectorV6 selector;
                selector.match_kind = source_action.selector.source_kind
                        == SelectorSourceKind::kLiteral
                    ? pathguard::PolicyMatchKind::kLiteralPrefix
                    : pathguard::PolicyMatchKind::kGlob;
                selector.object_type = source_action.selector.object_type
                        == SelectorObjectType::kFile
                    ? pathguard::PolicyObjectType::kFile
                    : source_action.selector.object_type == SelectorObjectType::kDirectory
                        ? pathguard::PolicyObjectType::kDirectory
                        : pathguard::PolicyObjectType::kAny;
                selector.root = selector_root;
                selector.base_pattern = source_action.selector.base_pattern;
                selector.except_patterns = source_action.selector.except_patterns;
                package.selectors.push_back(std::move(selector));
                selector_ids.emplace(std::move(selector_key), selector_id);
            } else {
                selector_id = found->second;
            }
            pathguard::PolicyActionV6 action;
            action.selector_index = selector_id;
            action.rule_id = source_action.id;
            switch (source_action.action) {
                case RuleActionKind::kDeny:
                    action.kind = pathguard::PolicyActionKind::kDeny;
                    break;
                case RuleActionKind::kRedirect:
                    action.kind = pathguard::PolicyActionKind::kRedirect;
                    break;
                case RuleActionKind::kObserve:
                    action.kind = pathguard::PolicyActionKind::kObserve;
                    break;
                case RuleActionKind::kExport:
                    action.kind = pathguard::PolicyActionKind::kExport;
                    break;
            }
            if (action.kind == pathguard::PolicyActionKind::kObserve
                || action.kind == pathguard::PolicyActionKind::kExport) {
                action.domain = pathguard::PolicyExecutionDomain::kEvent;
                action.required_operations = kOperationCloseWriteEvent;
            } else if (source_action.enforcement == RuleEnforcement::kProvider) {
                action.domain = pathguard::PolicyExecutionDomain::kProvider;
                // Forward Provider routing only needs a trustworthy Binder caller
                // and the concrete path operations exercised by the adapter. Query/
                // insert/reverse visibility is admitted separately when requested.
                action.required_capabilities = kCapabilityProviderCallerUid;
                action.required_operations = action.kind
                        == pathguard::PolicyActionKind::kDeny
                    ? kProviderCompositeOperationsV1 & ~
                        (kOperationProviderQuery | kOperationProviderInsert
                         | kOperationMediaScan | kOperationReverseMapping)
                    : kOperationOpenRead | kOperationOpenWrite | kOperationCreate
                        | kOperationLookupStat | kOperationAccess
                        | kOperationRename | kOperationUnlink;
            } else if (source_action.enforcement == RuleEnforcement::kComplete) {
                action.domain = pathguard::PolicyExecutionDomain::kCompleteVfs;
                action.required_capabilities = kCapabilityFuseCompletePath;
                action.required_operations = kCompleteVfsOperationsV1;
            } else if (source_action.selector.source_kind == SelectorSourceKind::kGlob) {
                action.domain = pathguard::PolicyExecutionDomain::kAppPath;
                action.required_capabilities = kCapabilityAppPathAdapter;
                action.required_operations = action.kind
                            == pathguard::PolicyActionKind::kRedirect
                        && source_action.selector.object_type
                            == SelectorObjectType::kFile
                    ? kAppPathFileRedirectOperationsV1
                    : kAppPathOperationsV1;
            } else {
                action.domain = pathguard::PolicyExecutionDomain::kMount;
            }
            action.priority = source_action.priority;
            if (action.kind == pathguard::PolicyActionKind::kRedirect) {
                action.target = source_action.target;
                action.preserve = pathguard::PolicyPreserveMode::kRelative;
                action.collision = pathguard::PolicyCollisionMode::kReject;
                action.reverse = pathguard::PolicyReverseMode::kNone;
                if (source_action.audit) {
                    action.options |= pathguard::binary_format::kActionOptionPrivateAudit;
                }
            } else if (action.kind == pathguard::PolicyActionKind::kExport) {
                action.target = source_action.target;
                action.preserve = pathguard::PolicyPreserveMode::kRelative;
                action.collision = pathguard::PolicyCollisionMode::kReject;
                action.options = static_cast<std::uint32_t>(source_action.export_mode)
                    | (source_action.media_scan ? UINT32_C(1) << 2 : 0);
            }
            package.actions.push_back(std::move(action));
        }
        policy.packages.push_back(std::move(package));
    }
    PolicyBlob blob;
    std::string encode_error;
    const auto encode_started = Clock::now();
    if (!pathguard::EncodePolicyV6(policy, &blob.bytes, &encode_error)) {
        output.statistics.encode_ns = ElapsedNs(encode_started);
        AddDiagnostic(&output.diagnostics, kPolicyEncode, "policy_encode_failed",
                      {}, DiagnosticSeverity::kError, limits);
        return output;
    }
    output.statistics.encode_ns = ElapsedNs(encode_started);
    blob.content_generation = pathguard::ComputePolicyV6ContentGeneration(policy);
    pathguard::PolicyV6 decoded;
    const auto verify_started = Clock::now();
    const auto verified = pathguard::DecodePolicyV6(blob.bytes, &decoded);
    output.statistics.verify_ns = ElapsedNs(verify_started);
    if (!verified.ok || verified.content_generation != blob.content_generation) {
        AddDiagnostic(&output.diagnostics, kPolicyEncode, "policy_verify_failed",
                      {}, DiagnosticSeverity::kError, limits);
        return output;
    }
    output.requirements.mount_actions = 0;
    for (const auto& package : policy.packages) {
        for (const auto& action : package.actions) {
            if (action.domain == pathguard::PolicyExecutionDomain::kMount) {
                output.requirements.mount_actions |= action.kind
                        == pathguard::PolicyActionKind::kDeny
                    ? kMountActionDenyAnchor : kMountActionRedirect;
            }
            if (action.domain == pathguard::PolicyExecutionDomain::kProvider) {
                output.requirements.provider = true;
            }
        }
    }
    output.requirements.topology = !policy.packages.empty();
    output.blob = std::move(blob);
    output.policy_v6 = std::move(policy);
    output.canonical_v2 = std::move(*built.canonical);
    return output;
}

AdmissionResult AdmitPolicy(const pathguard::PolicyV6& policy,
                            const PolicyRequirements& requirements,
                            const DeviceSnapshot& snapshot) {
    AdmissionResult output;
    output.content_generation = pathguard::ComputePolicyV6ContentGeneration(policy);
    output.capability_generation = snapshot.capability_generation;
    output.topology_generation = snapshot.topology_generation;
    const MountBackendSelection selection = SelectMountBackend(
        requirements.mount_actions, snapshot.mount, policy.allow_legacy_mount);
    output.backend = requirements.mount_actions == 0
        ? MountBackendKind::kStrictOpenTree : selection.backend;
    output.reason = requirements.mount_actions == 0
        ? MountBackendReason::kNone : selection.reason;
    output.admitted = (requirements.mount_actions == 0
            || selection.backend != MountBackendKind::kUnsupported)
        && (!requirements.topology || snapshot.topology_supported);
    return output;
}

bool VerifyPolicyBytes(const std::vector<std::uint8_t>& bytes,
                       std::uint64_t expected_content_generation) {
    pathguard::PolicyV6 decoded;
    const pathguard::PolicyV6DecodeResult result =
        pathguard::DecodePolicyV6(bytes, &decoded);
    return result.ok && result.content_generation == expected_content_generation
        && pathguard::ComputePolicyV6ContentGeneration(decoded)
            == expected_content_generation;
}

bool VerifyPolicyBlob(const pathguard::PolicyV6& policy,
                      const PolicyBlob& blob) {
    return pathguard::ComputePolicyV6ContentGeneration(policy)
            == blob.content_generation
        && VerifyPolicyBytes(blob.bytes, blob.content_generation);
}

}  // namespace pathguard::rules
