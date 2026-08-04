#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pathguard/rules/diagnostic.h"
#include "pathguard/pattern.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules_contract.h"

namespace pathguard::rules {

enum class SelectorObjectType : std::uint8_t {
    kFile,
    kDirectory,
    kAny,
};

enum class RuleActionKind : std::uint8_t {
    kDeny,
    kRedirect,
    kObserve,
    kExport,
};

enum class ExportMode : std::uint8_t { kCopy, kMove, kTrash };

enum class RuleEnforcement : std::uint8_t {
    kNone,
    kProvider,
    kComplete,
};

enum class PreserveMode : std::uint8_t {
    kRelative,
};

enum class CollisionPolicy : std::uint8_t {
    kReject,
};

enum class SelectorSourceKind : std::uint8_t {
    kLiteral,
    kGlob,
};

struct SelectorInputV2 {
    std::string root;
    std::string glob;
    std::vector<std::string> except;
    SelectorObjectType object_type = SelectorObjectType::kFile;
};

struct ActionRuleInputV2 {
    RuleId id = 0;
    RuleActionKind action = RuleActionKind::kDeny;
    SelectorInputV2 select;
    std::string target;
    std::int32_t priority = 0;
    PreserveMode preserve = PreserveMode::kRelative;
    CollisionPolicy collision = CollisionPolicy::kReject;
    RuleEnforcement enforcement = RuleEnforcement::kNone;
    ExportMode export_mode = ExportMode::kCopy;
    bool media_scan = false;
    bool audit = false;
};

struct ProviderIntentV2 {
    bool enabled = false;
};

struct AppRulesV2 {
    std::string package;
    bool enabled = true;
    std::vector<std::int32_t> users{0};
    std::vector<std::string> processes;
    ProviderIntentV2 provider;
    std::vector<ActionRuleInputV2> actions;
};

struct RulesDocumentV2 {
    std::uint32_t format = 2;
    bool allow_legacy_mount = false;
    std::vector<AppRulesV2> apps;
};

struct CanonicalSelectorV2 {
    SelectorSourceKind source_kind = SelectorSourceKind::kLiteral;
    std::string root;
    std::string glob;
    SelectorObjectType object_type = SelectorObjectType::kFile;
    pathguard::pattern::PatternProgram base_pattern;
    std::vector<pathguard::pattern::PatternProgram> except_patterns;
    std::uint16_t specificity = 0;
};

struct CanonicalActionV2 {
    RuleId id = 0;
    RuleActionKind action = RuleActionKind::kDeny;
    CanonicalSelectorV2 selector;
    std::string target;
    std::int32_t priority = 0;
    PreserveMode preserve = PreserveMode::kRelative;
    CollisionPolicy collision = CollisionPolicy::kReject;
    RuleEnforcement enforcement = RuleEnforcement::kNone;
    ExportMode export_mode = ExportMode::kCopy;
    bool media_scan = false;
    bool audit = false;
};

struct CanonicalAppPolicyV2 {
    std::string package;
    std::vector<std::int32_t> users;
    std::vector<std::string> processes;
    ProviderIntentV2 provider;
    std::vector<CanonicalActionV2> actions;
};

struct CanonicalPolicyV2 {
    bool allow_legacy_mount = false;
    std::vector<CanonicalAppPolicyV2> apps;
};

struct RulesV2ParseResult {
    std::optional<RulesDocumentV2> document;
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return document.has_value() && diagnostics.empty(); }
};

struct RulesV2BuildResult {
    std::optional<CanonicalPolicyV2> canonical;
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return canonical.has_value() && diagnostics.empty(); }
};

RulesV2ParseResult ParseRulesDocumentV2(const SourceBuffer& source,
                                        const RulesLimits& limits);
RulesV2BuildResult BuildCanonicalPolicyV2(
    const RulesDocumentV2& document, const RulesLimits& limits,
    const pathguard::pattern::PatternLimitsProfile& pattern_limits =
        pathguard::pattern::kPatternLimitsProfileV1);

}  // namespace pathguard::rules
