#include "pathguard/rules/semantic.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy.h"
#include "pathguard/rules/compiler.h"

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

ByteSpan OriginSpan(const OriginMap& origins, RuleId id);
void AddDiagnostic(std::vector<Diagnostic>* diagnostics,
                   std::string_view code, std::string_view message,
                   ByteSpan primary, DiagnosticSeverity severity,
                   const RulesLimits& limits,
                   std::optional<ByteSpan> related = std::nullopt);

bool AddExpanded(std::size_t value, std::size_t limit, std::size_t* total) {
    if (value > limit || *total > limit - value) return false;
    *total += value;
    return true;
}

bool ValidateExpandedRuleLimit(const RulesDocument& document,
                               const OriginMap& origins,
                               const RulesLimits& limits,
                               std::vector<Diagnostic>* diagnostics) {
    std::size_t expanded = 0;
    for (const AppRules& app : document.apps) {
        if (!app.enabled) continue;
        const std::size_t users = std::max<std::size_t>(app.users.size(), 1);
        const std::size_t processes = std::max<std::size_t>(app.processes.size(), 1);
        const std::size_t rules = app.deny.size() + app.redirects.size();
        if (rules != 0
            && (users > limits.max_expanded_rules / rules
                || users * rules > limits.max_expanded_rules / processes
                || !AddExpanded(users * rules * processes,
                                limits.max_expanded_rules, &expanded))) {
            RuleId first = !app.redirects.empty() ? app.redirects.front().id
                                                  : app.deny.front().id;
            AddDiagnostic(diagnostics, kResourceLimit,
                          "expanded_rule_limit", OriginSpan(origins, first),
                          DiagnosticSeverity::kError, limits);
            return false;
        }
    }
    return true;
}

bool ExceedsPathLimits(std::string_view input, const RulesLimits& limits) {
    if (input.size() > limits.max_path_bytes) return true;
    std::size_t components = input.empty() ? 0 : 1;
    for (const char value : input) {
        if (value == '/' && ++components > limits.max_path_components) {
            return true;
        }
    }
    return components > limits.max_path_components;
}

ByteSpan OriginSpan(const OriginMap& origins, RuleId id) {
    const RuleOrigin* origin = origins.Find(id);
    return origin == nullptr ? ByteSpan{} : origin->primary;
}

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

std::uint16_t PathDepth(const NormalizedPath& path) {
    return static_cast<std::uint16_t>(path.component_offsets.size());
}

pathguard::PolicyDocument ToPolicyDocument(const CanonicalPolicy& canonical) {
    pathguard::PolicyDocument output;
    output.schema = 2;
    output.failure_mode = pathguard::FailureMode::kOpen;
    output.allow_legacy_string_bind = canonical.allow_legacy_mount;
    output.apps.reserve(canonical.apps.size());
    for (const CanonicalAppPolicy& source : canonical.apps) {
        pathguard::AppPolicy app;
        app.package = source.package;
        app.provider_compat = source.file_picker
            ? pathguard::ProviderCompat::kVirtualize
            : pathguard::ProviderCompat::kOff;
        app.users.clear();
        app.users.reserve(source.users.size());
        for (const std::int32_t user : source.users) {
            app.users.push_back(std::to_string(user));
        }
        if (app.users.empty()) app.users.push_back("*");
        app.processes = source.processes;
        if (app.processes.empty()) app.processes.push_back("*");
        app.mounts.reserve(source.deny.size() + source.redirects.size());
        for (const NormalizedPath& deny : source.deny) {
            app.mounts.push_back({
                pathguard::MountAction::kDeny, deny.bytes, {}, 0, 0, 0});
        }
        for (const CanonicalRedirectRule& redirect : source.redirects) {
            app.mounts.push_back({
                pathguard::MountAction::kRedirect,
                redirect.source.bytes,
                redirect.target.bytes,
                PathDepth(redirect.source),
                0,
                0,
            });
        }
        output.apps.push_back(std::move(app));
    }
    return output;
}

bool SamePath(const NormalizedPath& lhs, const NormalizedPath& rhs) {
    return lhs.bytes == rhs.bytes;
}

bool PathsOverlap(const NormalizedPath& lhs, const NormalizedPath& rhs) {
    return IsSameOrAncestor(lhs, rhs) || IsSameOrAncestor(rhs, lhs);
}

struct RedirectRef {
    const ResolvedRedirectRule* rule = nullptr;
};

void ValidateRedirects(ResolvedAppPolicy* app, const OriginMap& origins,
                       const RulesLimits& limits,
                       std::vector<Diagnostic>* diagnostics) {
    std::sort(app->redirects.begin(), app->redirects.end(),
              [](const ResolvedRedirectRule& lhs,
                 const ResolvedRedirectRule& rhs) {
                  return std::tie(lhs.source.bytes, lhs.target.bytes, lhs.id)
                      < std::tie(rhs.source.bytes, rhs.target.bytes, rhs.id);
              });

    std::vector<ResolvedRedirectRule> unique;
    unique.reserve(app->redirects.size());
    for (ResolvedRedirectRule& rule : app->redirects) {
        if (!unique.empty() && SamePath(unique.back().source, rule.source)
            && SamePath(unique.back().target, rule.target)) {
            AddDiagnostic(diagnostics, kRuleRedundant, "duplicate_redirect",
                          OriginSpan(origins, rule.id),
                          DiagnosticSeverity::kWarning, limits,
                          OriginSpan(origins, unique.back().id));
            continue;
        }
        unique.push_back(std::move(rule));
    }
    app->redirects = std::move(unique);

    for (std::size_t index = 0; index < app->redirects.size(); ++index) {
        const ResolvedRedirectRule& current = app->redirects[index];
        if (PathsOverlap(current.source, current.target)) {
            AddDiagnostic(diagnostics, kRuleConflict,
                          "redirect_source_target_overlap",
                          OriginSpan(origins, current.id),
                          DiagnosticSeverity::kError, limits);
        }
        if (index == 0) continue;
        const ResolvedRedirectRule& previous = app->redirects[index - 1];
        if (PathsOverlap(previous.source, current.source)) {
            AddDiagnostic(diagnostics, kRuleConflict,
                          "redirect_source_conflict",
                          OriginSpan(origins, current.id),
                          DiagnosticSeverity::kError, limits,
                          OriginSpan(origins, previous.id));
        }
    }

    std::unordered_map<std::string, std::size_t> source_index;
    source_index.reserve(app->redirects.size());
    for (std::size_t index = 0; index < app->redirects.size(); ++index) {
        source_index.emplace(app->redirects[index].source.bytes, index);
    }
    std::vector<std::uint8_t> colors(app->redirects.size());
    std::function<bool(std::size_t)> visit = [&](std::size_t index) {
        if (colors[index] == 1) return true;
        if (colors[index] == 2) return false;
        colors[index] = 1;
        const auto next = source_index.find(app->redirects[index].target.bytes);
        if (next != source_index.end() && visit(next->second)) return true;
        colors[index] = 2;
        return false;
    };
    for (std::size_t index = 0; index < app->redirects.size(); ++index) {
        if (visit(index)) {
            AddDiagnostic(diagnostics, kRedirectCycle, "redirect_cycle",
                          OriginSpan(origins, app->redirects[index].id),
                          DiagnosticSeverity::kError, limits);
            break;
        }
    }

    if (app->file_picker) {
        if (app->redirects.empty()) {
            AddDiagnostic(diagnostics, kInvalidValue,
                          "file_picker_requires_redirect", {},
                          DiagnosticSeverity::kError, limits);
        }
    }
}

void ValidateDeny(ResolvedAppPolicy* app, const OriginMap& origins,
                  const RulesLimits& limits,
                  std::vector<Diagnostic>* diagnostics) {
    std::sort(app->deny.begin(), app->deny.end(),
              [](const ResolvedDenyRule& lhs, const ResolvedDenyRule& rhs) {
                  return std::tie(lhs.path.bytes, lhs.id)
                      < std::tie(rhs.path.bytes, rhs.id);
              });
    std::vector<ResolvedDenyRule> unique;
    unique.reserve(app->deny.size());
    for (ResolvedDenyRule& current : app->deny) {
        if (!unique.empty()
            && IsSameOrAncestor(unique.back().path, current.path)) {
            AddDiagnostic(diagnostics, kRuleRedundant, "deny_redundant",
                          OriginSpan(origins, current.id),
                          DiagnosticSeverity::kWarning, limits,
                          OriginSpan(origins, unique.back().id));
            continue;
        }
        unique.push_back(std::move(current));
    }
    app->deny = std::move(unique);
    for (const ResolvedRedirectRule& redirect : app->redirects) {
        const auto next = std::lower_bound(
            app->deny.begin(), app->deny.end(), redirect.source.bytes,
            [](const ResolvedDenyRule& deny, std::string_view path) {
                return deny.path.bytes < path;
            });
        const ResolvedDenyRule* conflict = nullptr;
        if (next != app->deny.end()
            && SamePath(redirect.source, next->path)) {
            conflict = &*next;
        }
        if (next != app->deny.begin()) {
            const ResolvedDenyRule& previous = *std::prev(next);
            if (IsSameOrAncestor(previous.path, redirect.source)) {
                conflict = &previous;
            }
        }
        if (conflict != nullptr) {
            AddDiagnostic(diagnostics, kRuleConflict,
                          "deny_redirect_conflict",
                          OriginSpan(origins, redirect.id),
                          DiagnosticSeverity::kError, limits,
                          OriginSpan(origins, conflict->id));
        }
    }
}

std::optional<PolicyBlob> EncodeAndVerify(const CanonicalPolicy& canonical,
                                          CompileStatistics* statistics) {
    pathguard::PolicyDocument document = ToPolicyDocument(canonical);
    PolicyBlob blob;
    pathguard::ParseError error;
    const auto encode_started = Clock::now();
    if (!pathguard::EncodePolicy(document, &blob.bytes, &error)) {
        statistics->encode_ns += ElapsedNs(encode_started);
        return std::nullopt;
    }
    statistics->encode_ns += ElapsedNs(encode_started);
    blob.content_generation = pathguard::ComputeContentGeneration(document);
    const auto verify_started = Clock::now();
    const bool verified = blob.content_generation != 0
        && VerifyPolicyBlob(canonical, blob);
    statistics->verify_ns += ElapsedNs(verify_started);
    if (!verified) {
        return std::nullopt;
    }
    return blob;
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

static RulesBuildResult CompileRulesLegacy(const SourceBuffer& source,
                                           const RulesLimits& limits) {
    RulesBuildResult output;
    RulesCompileResult parsed = ParseRulesDocument(source, limits);
    output.statistics = parsed.statistics;
    output.origins = std::move(parsed.origins);
    output.diagnostics = std::move(parsed.diagnostics);
    if (!parsed.document.has_value() || HasErrors(output.diagnostics)) {
        return output;
    }
    if (!ValidateExpandedRuleLimit(*parsed.document, output.origins, limits,
                                   &output.diagnostics)) {
        return output;
    }

    ResolvedPolicy resolved;
    resolved.allow_legacy_mount = parsed.document->compatibility.allow_legacy_mount;
    resolved.apps.reserve(parsed.document->apps.size());
    for (const AppRules& source_app : parsed.document->apps) {
        ResolvedAppPolicy app;
        app.package = source_app.package;
        app.enabled = source_app.enabled;
        app.users = source_app.users;
        app.processes = source_app.processes;
        app.file_picker = source_app.file_picker;
        app.deny.reserve(source_app.deny.size());
        app.redirects.reserve(source_app.redirects.size());
        const auto normalize_started = Clock::now();
        for (const DenyRule& rule : source_app.deny) {
            if (ExceedsPathLimits(rule.path, limits)) {
                AddDiagnostic(&output.diagnostics, kResourceLimit,
                              "path_resource_limit",
                              OriginSpan(output.origins, rule.id),
                              DiagnosticSeverity::kError, limits);
                continue;
            }
            auto path = NormalizeRulePath(rule.path, limits, &output.statistics);
            if (!path.has_value()) {
                AddDiagnostic(&output.diagnostics, kPathInvalid, "invalid_path",
                              OriginSpan(output.origins, rule.id),
                              DiagnosticSeverity::kError, limits);
                continue;
            }
            app.deny.push_back({rule.id, std::move(*path)});
        }
        for (const RedirectRule& rule : source_app.redirects) {
            if (ExceedsPathLimits(rule.source, limits)
                || ExceedsPathLimits(rule.target, limits)) {
                AddDiagnostic(&output.diagnostics, kResourceLimit,
                              "path_resource_limit",
                              OriginSpan(output.origins, rule.id),
                              DiagnosticSeverity::kError, limits);
                continue;
            }
            auto from = NormalizeRulePath(rule.source, limits, &output.statistics);
            auto to = NormalizeRulePath(rule.target, limits, &output.statistics);
            if (!from.has_value() || !to.has_value()) {
                AddDiagnostic(&output.diagnostics, kPathInvalid, "invalid_path",
                              OriginSpan(output.origins, rule.id),
                              DiagnosticSeverity::kError, limits);
                continue;
            }
            app.redirects.push_back(
                {rule.id, std::move(*from), std::move(*to)});
        }
        output.statistics.normalize_ns += ElapsedNs(normalize_started);
        const auto conflict_started = Clock::now();
        ValidateRedirects(&app, output.origins, limits, &output.diagnostics);
        ValidateDeny(&app, output.origins, limits, &output.diagnostics);
        output.statistics.conflict_ns += ElapsedNs(conflict_started);
        resolved.apps.push_back(std::move(app));
    }
    if (HasErrors(output.diagnostics)) {
        output.resolved = std::move(resolved);
        return output;
    }

    const auto canonicalize_started = Clock::now();
    CanonicalPolicy canonical;
    canonical.allow_legacy_mount = resolved.allow_legacy_mount;
    for (const ResolvedAppPolicy& source_app : resolved.apps) {
        if (!source_app.enabled) continue;
        CanonicalAppPolicy app;
        app.package = source_app.package;
        app.users = source_app.users;
        app.processes = source_app.processes;
        app.file_picker = source_app.file_picker;
        app.deny.reserve(source_app.deny.size());
        for (const ResolvedDenyRule& deny : source_app.deny) {
            app.deny.push_back(deny.path);
        }
        std::sort(app.deny.begin(), app.deny.end(),
                  [](const NormalizedPath& lhs, const NormalizedPath& rhs) {
                      return lhs.bytes < rhs.bytes;
                  });
        std::sort(app.users.begin(), app.users.end());
        app.users.erase(std::unique(app.users.begin(), app.users.end()),
                        app.users.end());
        std::sort(app.processes.begin(), app.processes.end());
        app.processes.erase(
            std::unique(app.processes.begin(), app.processes.end()),
            app.processes.end());
        app.redirects.reserve(source_app.redirects.size());
        for (const ResolvedRedirectRule& redirect : source_app.redirects) {
            app.redirects.push_back({redirect.source, redirect.target});
        }
        std::sort(app.redirects.begin(), app.redirects.end(),
                  [](const CanonicalRedirectRule& lhs,
                     const CanonicalRedirectRule& rhs) {
                      return std::tie(lhs.source.bytes, lhs.target.bytes)
                          < std::tie(rhs.source.bytes, rhs.target.bytes);
                  });
        canonical.apps.push_back(std::move(app));
    }
    std::sort(canonical.apps.begin(), canonical.apps.end(),
              [](const CanonicalAppPolicy& lhs,
                 const CanonicalAppPolicy& rhs) {
                  return lhs.package < rhs.package;
              });
    output.statistics.canonicalize_ns = ElapsedNs(canonicalize_started);
    output.requirements.mount_actions = 0;
    for (const CanonicalAppPolicy& app : canonical.apps) {
        if (!app.redirects.empty()) {
            output.requirements.mount_actions |= kMountActionRedirect;
        }
        if (!app.deny.empty()) {
            output.requirements.mount_actions |= kMountActionDenyAnchor;
        }
    }
    output.requirements.provider = std::any_of(
        canonical.apps.begin(), canonical.apps.end(),
        [](const CanonicalAppPolicy& app) { return app.file_picker; });
    output.requirements.topology = !canonical.apps.empty();
    output.blob = EncodeAndVerify(canonical, &output.statistics);
    if (!output.blob.has_value()) {
        AddDiagnostic(&output.diagnostics, kPolicyEncode,
                      "policy_encode_failed", {},
                      DiagnosticSeverity::kError, limits);
        output.canonical.reset();
    } else {
        output.canonical = std::move(canonical);
    }
    output.resolved = std::move(resolved);
    return output;
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
                action.required_operations = kAppPathOperationsV1;
            } else {
                action.domain = pathguard::PolicyExecutionDomain::kMount;
            }
            action.priority = source_action.priority;
            if (action.kind == pathguard::PolicyActionKind::kRedirect) {
                action.target = source_action.target;
                action.preserve = pathguard::PolicyPreserveMode::kRelative;
                action.collision = pathguard::PolicyCollisionMode::kReject;
                action.reverse = pathguard::PolicyReverseMode::kStaticUnique;
            } else if (action.kind == pathguard::PolicyActionKind::kExport) {
                action.target = source_action.target;
                action.preserve = pathguard::PolicyPreserveMode::kRelative;
                action.collision = pathguard::PolicyCollisionMode::kReject;
                action.options = static_cast<std::uint32_t>(source_action.export_mode)
                    | (source_action.media_scan ? UINT32_C(1) << 2 : 0);
            }
            package.actions.push_back(std::move(action));
        }
        for (auto& action : package.actions) {
            if (action.kind != pathguard::PolicyActionKind::kRedirect
                || action.domain == pathguard::PolicyExecutionDomain::kMount) {
                continue;
            }
            std::vector<std::string_view> source_roots;
            for (const auto& candidate : package.actions) {
                if (candidate.kind == pathguard::PolicyActionKind::kRedirect
                    && candidate.domain == action.domain
                    && candidate.target == action.target
                    && candidate.selector_index < package.selectors.size()) {
                    const std::string& root =
                        package.selectors[candidate.selector_index].root;
                    if (std::find(source_roots.begin(), source_roots.end(), root)
                        == source_roots.end()) {
                        source_roots.emplace_back(root);
                    }
                }
            }
            action.reverse = source_roots.size() == 1
                ? pathguard::PolicyReverseMode::kStaticUnique
                : pathguard::PolicyReverseMode::kProvenance;
            // Reverse visibility is admitted at reverse/query operation time. It
            // must not make the forward create/write route unavailable: when the
            // provenance service is absent, forward routing remains deterministic
            // while reverse lookup reports ambiguous/unsupported instead of
            // inventing a canonical source.
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

AdmissionResult AdmitPolicy(const CanonicalPolicy& policy,
                            const PolicyRequirements& requirements,
                            const DeviceSnapshot& snapshot) {
    AdmissionResult output;
    const pathguard::PolicyDocument document = ToPolicyDocument(policy);
    output.content_generation = pathguard::ComputeContentGeneration(document);
    output.capability_generation = snapshot.capability_generation;
    output.topology_generation = snapshot.topology_generation;
    const MountBackendSelection selection = SelectMountBackend(
        requirements.mount_actions, snapshot.mount,
        policy.allow_legacy_mount);
    output.backend = selection.backend;
    output.reason = selection.reason;
    output.admitted = selection.backend != MountBackendKind::kUnsupported
        && (!requirements.provider || snapshot.provider_supported)
        && (!requirements.topology || snapshot.topology_supported);
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

bool VerifyPolicyBlob(const CanonicalPolicy& policy, const PolicyBlob& blob) {
    const pathguard::PolicyDocument expected = ToPolicyDocument(policy);
    return pathguard::ComputeContentGeneration(expected)
            == blob.content_generation
        && VerifyPolicyBytes(blob.bytes, blob.content_generation);
}

bool VerifyPolicyBlob(const pathguard::PolicyV6& policy,
                      const PolicyBlob& blob) {
    return pathguard::ComputePolicyV6ContentGeneration(policy)
            == blob.content_generation
        && VerifyPolicyBytes(blob.bytes, blob.content_generation);
}

}  // namespace pathguard::rules
