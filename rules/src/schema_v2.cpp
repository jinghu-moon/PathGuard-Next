#include "pathguard/rules/schema_v2.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <toml.hpp>

#include "pathguard/rules/semantic.h"

namespace pathguard::rules {
namespace {

using pathguard::pattern::BraceExpandError;
using pathguard::pattern::BraceExpansionResult;
using pathguard::pattern::CompilePattern;
using pathguard::pattern::ExpandPatternBraces;
using pathguard::pattern::PatternCompileError;
using pathguard::pattern::PatternCompileResult;
using pathguard::pattern::PatternProgram;

ByteSpan SourceSpan(const SourceBuffer& source,
                    const toml::source_region& region) {
    auto offset_at = [&](const toml::source_position& position) {
        std::size_t line = 1;
        std::size_t column = 1;
        std::size_t offset = 0;
        while (offset < source.bytes().size() && line < position.line) {
            if (source.bytes()[offset++] == '\n') {
                ++line;
                column = 1;
            }
        }
        while (offset < source.bytes().size() && column < position.column
               && source.bytes()[offset] != '\n') {
            ++offset;
            ++column;
        }
        return static_cast<std::uint32_t>(
            std::min<std::size_t>(offset, source.size()));
    };
    return {offset_at(region.begin), offset_at(region.end)};
}

bool IsPackageName(std::string_view value) {
    if (value.empty() || value.front() == '.' || value.back() == '.') return false;
    bool dot = false;
    bool component = false;
    for (const char character : value) {
        if (character == '.') {
            if (!component) return false;
            dot = true;
            component = false;
        } else if ((character >= 'a' && character <= 'z')
                   || (character >= 'A' && character <= 'Z')
                   || (character >= '0' && character <= '9')
                   || character == '_') {
            component = true;
        } else {
            return false;
        }
    }
    return dot && component;
}

bool HasUnescapedMeta(std::string_view pattern) {
    bool escaped = false;
    for (const char value : pattern) {
        if (escaped) {
            escaped = false;
        } else if (value == '\\') {
            escaped = true;
        } else if (value == '*' || value == '?' || value == '[') {
            return true;
        }
    }
    return false;
}

class Decoder {
public:
    Decoder(const SourceBuffer& source, const RulesLimits& limits)
        : source_(source), limits_(limits) {}

    RulesV2ParseResult Run() {
        const toml::parse_result parsed = toml::parse(
            source_.bytes(), source_.file_name());
        if (!parsed) {
            Add(kTomlParse, "rules.toml_parse", {}, "/");
            return std::move(result_);
        }
        const toml::table& root = parsed.table();
        RulesDocumentV2 document;
        bool valid = CheckFields(root, {"format", "compatibility", "apps"}, "");
        const toml::node* format = root.get("format");
        const auto version = format ? format->value<std::int64_t>() : std::nullopt;
        if (!version || *version != 2) {
            const bool legacy = version.has_value() && *version == 1;
            Add(legacy ? kFormatUnsupported : kFormatInvalid,
                legacy ? "rules.format_1_rejected" : "rules.format_2_required",
                format ? SourceSpan(source_, format->source()) : ByteSpan{},
                "/format");
            valid = false;
        }
        if (const toml::node* compatibility = root.get("compatibility")) {
            const toml::table* table = compatibility->as_table();
            if (table == nullptr) {
                Add(kTypeMismatch, "rules.compatibility_type",
                    SourceSpan(source_, compatibility->source()),
                    "/compatibility");
                valid = false;
            } else {
                valid = DecodeCompatibility(*table, &document) && valid;
            }
        }
        const toml::node* apps_node = root.get("apps");
        const toml::table* apps = apps_node ? apps_node->as_table() : nullptr;
        if (apps == nullptr || apps->empty()) {
            Add(kTypeMismatch, "rules.apps_required",
                apps_node ? SourceSpan(source_, apps_node->source()) : ByteSpan{},
                "/apps");
            return std::move(result_);
        }
        if (apps->size() > limits_.max_apps) {
            Add(kResourceLimit, "rules.apps_limit",
                SourceSpan(source_, apps->source()), "/apps");
            return std::move(result_);
        }
        apps->for_each([&](const toml::key& package_key,
                           const toml::node& node) {
            const std::string package(package_key.str());
            const std::string base = "/apps/" + package;
            if (!IsPackageName(package)) {
                Add(kInvalidValue, "rules.package_invalid",
                    SourceSpan(source_, package_key.source()), base);
                valid = false;
                return;
            }
            const toml::table* app_table = node.as_table();
            if (app_table == nullptr) {
                Add(kTypeMismatch, "rules.app_type",
                    SourceSpan(source_, node.source()), base);
                valid = false;
                return;
            }
            AppRulesV2 app;
            app.package = package;
            valid = DecodeApp(*app_table, base, &app) && valid;
            document.apps.push_back(std::move(app));
        });
        if (!valid || !result_.diagnostics.empty()) return std::move(result_);
        result_.document = std::move(document);
        return std::move(result_);
    }

private:
    void Add(std::string_view code, std::string_view message, ByteSpan span,
             std::string field_path) {
        if (result_.diagnostics.size() >= limits_.max_diagnostics) return;
        Diagnostic diagnostic{code, message, span, false};
        diagnostic.phase = DiagnosticPhase::kDecode;
        diagnostic.field_path = std::move(field_path);
        result_.diagnostics.push_back(std::move(diagnostic));
    }

    bool CheckFields(const toml::table& table,
                     std::initializer_list<std::string_view> allowed,
                     const std::string& path) {
        bool valid = true;
        table.for_each([&](const toml::key& key, const toml::node&) {
            if (std::find(allowed.begin(), allowed.end(), key.str())
                == allowed.end()) {
                Add(kUnknownField, "rules.unknown_field",
                    SourceSpan(source_, key.source()),
                    path + "/" + std::string(key.str()));
                valid = false;
            }
        });
        return valid;
    }

    bool DecodeCompatibility(const toml::table& table,
                             RulesDocumentV2* document) {
        bool valid = CheckFields(table, {"allow_legacy_mount"},
                                 "/compatibility");
        if (const toml::node* node = table.get("allow_legacy_mount")) {
            const auto value = node->value<bool>();
            if (!value) {
                Add(kTypeMismatch, "rules.boolean_required",
                    SourceSpan(source_, node->source()),
                    "/compatibility/allow_legacy_mount");
                valid = false;
            } else {
                document->allow_legacy_mount = *value;
            }
        }
        return valid;
    }

    bool DecodeBool(const toml::table& table, std::string_view key,
                    const std::string& path, bool* output) {
        const toml::node* node = table.get(key);
        if (node == nullptr) return true;
        const auto value = node->value<bool>();
        if (!value) {
            Add(kTypeMismatch, "rules.boolean_required",
                SourceSpan(source_, node->source()), path);
            return false;
        }
        *output = *value;
        return true;
    }

    bool DecodeUsers(const toml::node& node, const std::string& path,
                     std::vector<std::int32_t>* output) {
        const toml::array* values = node.as_array();
        if (values == nullptr || values->empty()) {
            Add(kTypeMismatch, "rules.integer_array_required",
                SourceSpan(source_, node.source()), path);
            return false;
        }
        output->clear();
        std::unordered_set<std::int64_t> unique;
        bool valid = true;
        for (const toml::node& value_node : *values) {
            const auto value = value_node.value<std::int64_t>();
            if (!value || *value < 0 || *value > 99'999
                || !unique.insert(*value).second) {
                Add(kInvalidValue, "rules.user_invalid",
                    SourceSpan(source_, value_node.source()), path);
                valid = false;
            } else {
                output->push_back(static_cast<std::int32_t>(*value));
            }
        }
        return valid;
    }

    bool DecodeProcesses(const toml::node& node, const std::string& package,
                         const std::string& path,
                         std::vector<std::string>* output) {
        const toml::array* values = node.as_array();
        if (values == nullptr) {
            Add(kTypeMismatch, "rules.string_array_required",
                SourceSpan(source_, node.source()), path);
            return false;
        }
        std::unordered_set<std::string> unique;
        bool valid = true;
        for (const toml::node& value_node : *values) {
            const auto value = value_node.value<std::string>();
            const bool allowed = value && (*value == "*" || *value == package
                || value->starts_with(package + ":"));
            if (!allowed || !unique.insert(*value).second) {
                Add(kInvalidValue, "rules.process_invalid",
                    SourceSpan(source_, value_node.source()), path);
                valid = false;
            } else {
                output->push_back(*value);
            }
        }
        return valid;
    }

    bool DecodeProvider(const toml::node& node, const std::string& path,
                        ProviderIntentV2* provider) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            Add(kTypeMismatch, "rules.provider_type",
                SourceSpan(source_, node.source()), path);
            return false;
        }
        bool valid = CheckFields(*table, {"enabled"}, path);
        valid = DecodeBool(*table, "enabled", path + "/enabled",
                           &provider->enabled) && valid;
        return valid;
    }

    bool DecodeSelector(const toml::node& node, const std::string& path,
                        SelectorInputV2* selector) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            Add(kTypeMismatch, "rules.selector_type",
                SourceSpan(source_, node.source()), path);
            return false;
        }
        bool valid = CheckFields(*table, {"root", "glob", "except", "type"},
                                 path);
        const toml::node* root = table->get("root");
        const toml::node* glob = table->get("glob");
        const auto root_value = root ? root->value<std::string>() : std::nullopt;
        const auto glob_value = glob ? glob->value<std::string>() : std::nullopt;
        if (!root_value) {
            Add(kTypeMismatch, "rules.selector_root_required",
                root ? SourceSpan(source_, root->source()) : SourceSpan(source_, table->source()),
                path + "/root");
            valid = false;
        } else {
            selector->root = *root_value;
        }
        if (!glob_value) {
            Add(kTypeMismatch, "rules.selector_glob_required",
                glob ? SourceSpan(source_, glob->source()) : SourceSpan(source_, table->source()),
                path + "/glob");
            valid = false;
        } else {
            selector->glob = *glob_value;
        }
        if (const toml::node* type = table->get("type")) {
            const auto value = type->value<std::string>();
            if (!value || (*value != "file" && *value != "directory"
                           && *value != "any")) {
                Add(kInvalidValue, "rules.selector_type_invalid",
                    SourceSpan(source_, type->source()), path + "/type");
                valid = false;
            } else if (*value == "directory") {
                selector->object_type = SelectorObjectType::kDirectory;
            } else if (*value == "any") {
                selector->object_type = SelectorObjectType::kAny;
            }
        }
        if (const toml::node* except = table->get("except")) {
            const toml::array* values = except->as_array();
            if (values == nullptr) {
                Add(kTypeMismatch, "rules.selector_except_array_required",
                    SourceSpan(source_, except->source()), path + "/except");
                valid = false;
            } else if (values->empty()) {
                Add(kInvalidValue, "rules.selector_except_empty",
                    SourceSpan(source_, except->source()), path + "/except");
                valid = false;
            } else {
                for (const toml::node& value_node : *values) {
                    const auto value = value_node.value<std::string>();
                    if (!value) {
                        Add(kTypeMismatch, "rules.string_required",
                            SourceSpan(source_, value_node.source()),
                            path + "/except");
                        valid = false;
                    } else {
                        selector->except.push_back(*value);
                    }
                }
            }
        }
        return valid;
    }

    bool DecodeAction(const toml::table& table, RuleActionKind kind,
                      const std::string& path, ActionRuleInputV2* action) {
        bool valid = CheckFields(table,
            {"select", "to", "priority", "preserve", "collision", "enforcement",
             "mode", "media_scan", "audit"},
            path);
        action->action = kind;
        const toml::node* select = table.get("select");
        if (select == nullptr) {
            Add(kTypeMismatch, "rules.selector_required",
                SourceSpan(source_, table.source()), path + "/select");
            valid = false;
        } else {
            valid = DecodeSelector(*select, path + "/select", &action->select)
                && valid;
        }
        if (const toml::node* priority = table.get("priority")) {
            const auto value = priority->value<std::int64_t>();
            if (!value || *value < std::numeric_limits<std::int32_t>::min()
                || *value > std::numeric_limits<std::int32_t>::max()) {
                Add(kInvalidValue, "rules.priority_invalid",
                    SourceSpan(source_, priority->source()), path + "/priority");
                valid = false;
            } else {
                action->priority = static_cast<std::int32_t>(*value);
            }
        }
        if (kind == RuleActionKind::kRedirect || kind == RuleActionKind::kExport) {
            const toml::node* to = table.get("to");
            const auto value = to ? to->value<std::string>() : std::nullopt;
            if (!value) {
                Add(kTypeMismatch, kind == RuleActionKind::kRedirect
                        ? "rules.redirect_target_required"
                        : "rules.export_target_required",
                    to ? SourceSpan(source_, to->source()) : SourceSpan(source_, table.source()),
                    path + "/to");
                valid = false;
            } else {
                action->target = *value;
            }
            if (const toml::node* preserve = table.get("preserve");
                kind == RuleActionKind::kRedirect && preserve != nullptr) {
                const auto mode = preserve->value<std::string>();
                if (!mode || *mode != "relative") {
                    Add(kInvalidValue, "rules.preserve_invalid",
                        SourceSpan(source_, preserve->source()), path + "/preserve");
                    valid = false;
                }
            }
            if (const toml::node* collision = table.get("collision");
                kind == RuleActionKind::kRedirect && collision != nullptr) {
                const auto policy = collision->value<std::string>();
                if (!policy || *policy != "reject") {
                    Add(kInvalidValue, "rules.collision_invalid",
                        SourceSpan(source_, collision->source()), path + "/collision");
                    valid = false;
                }
            }
            if (const toml::node* enforcement = table.get("enforcement");
                kind == RuleActionKind::kRedirect && enforcement != nullptr) {
                const auto value = enforcement->value<std::string>();
                if (!value || (*value != "provider" && *value != "complete")) {
                    Add(kInvalidValue, "rules.enforcement_invalid",
                        SourceSpan(source_, enforcement->source()),
                        path + "/enforcement");
                    valid = false;
                } else {
                    action->enforcement = *value == "provider"
                        ? RuleEnforcement::kProvider
                        : RuleEnforcement::kComplete;
                }
            }
            if (const toml::node* audit = table.get("audit");
                kind == RuleActionKind::kRedirect && audit != nullptr) {
                const auto value = audit->value<bool>();
                if (!value) {
                    Add(kTypeMismatch, "rules.audit_bool_required",
                        SourceSpan(source_, audit->source()), path + "/audit");
                    valid = false;
                } else {
                    action->audit = *value;
                }
            }
            if (kind == RuleActionKind::kExport) {
                if (table.contains("preserve") || table.contains("collision")
                    || table.contains("enforcement") || table.contains("audit")) {
                    Add(kUnknownField, "rules.export_path_fields_forbidden",
                        SourceSpan(source_, table.source()), path);
                    valid = false;
                }
                if (const toml::node* mode = table.get("mode")) {
                    const auto value = mode->value<std::string>();
                    if (!value || (*value != "copy" && *value != "move"
                                   && *value != "trash")) {
                        Add(kInvalidValue, "rules.export_mode_invalid",
                            SourceSpan(source_, mode->source()), path + "/mode");
                        valid = false;
                    } else {
                        action->export_mode = *value == "move" ? ExportMode::kMove
                            : *value == "trash" ? ExportMode::kTrash
                                                 : ExportMode::kCopy;
                    }
                }
                if (const toml::node* scan = table.get("media_scan")) {
                    const auto value = scan->value<bool>();
                    if (!value) {
                        Add(kTypeMismatch, "rules.media_scan_bool_required",
                            SourceSpan(source_, scan->source()), path + "/media_scan");
                        valid = false;
                    } else {
                        action->media_scan = *value;
                    }
                }
            }
        } else {
            if (table.contains("to") || table.contains("preserve")
                || table.contains("collision") || table.contains("mode")
                || table.contains("media_scan") || table.contains("audit")) {
                Add(kUnknownField, kind == RuleActionKind::kDeny
                        ? "rules.deny_target_fields_forbidden"
                        : "rules.observe_target_fields_forbidden",
                    SourceSpan(source_, table.source()), path);
                valid = false;
            }
            if (const toml::node* enforcement = table.get("enforcement");
                kind == RuleActionKind::kDeny && enforcement != nullptr) {
                const auto value = enforcement->value<std::string>();
                if (!value || (*value != "provider" && *value != "complete")) {
                    Add(kInvalidValue, "rules.enforcement_invalid",
                        SourceSpan(source_, enforcement->source()),
                        path + "/enforcement");
                    valid = false;
                } else {
                    action->enforcement = *value == "provider"
                        ? RuleEnforcement::kProvider
                        : RuleEnforcement::kComplete;
                }
            }
            if (kind == RuleActionKind::kObserve && table.contains("enforcement")) {
                Add(kUnknownField, "rules.observe_enforcement_forbidden",
                    SourceSpan(source_, table.source()), path + "/enforcement");
                valid = false;
            }
            if (kind == RuleActionKind::kDeny
                && HasUnescapedMeta(action->select.glob)
                && action->enforcement == RuleEnforcement::kNone) {
                Add(kInvalidValue, "rules.glob_deny_enforcement_required",
                    SourceSpan(source_, table.source()), path + "/enforcement");
                valid = false;
            }
        }
        return valid;
    }

    bool DecodeActions(const toml::node& node, RuleActionKind kind,
                       const std::string& path, AppRulesV2* app) {
        const toml::array* rules = node.as_array();
        if (rules == nullptr) {
            Add(kTypeMismatch, "rules.action_array_required",
                SourceSpan(source_, node.source()), path);
            return false;
        }
        bool valid = true;
        std::size_t index = 0;
        for (const toml::node& rule_node : *rules) {
            const toml::table* table = rule_node.as_table();
            if (table == nullptr) {
                Add(kTypeMismatch, "rules.action_table_required",
                    SourceSpan(source_, rule_node.source()), path);
                valid = false;
            } else {
                ActionRuleInputV2 action;
                action.id = next_rule_id_++;
                valid = DecodeAction(*table, kind,
                    path + "/" + std::to_string(index), &action) && valid;
                app->actions.push_back(std::move(action));
            }
            ++index;
        }
        return valid;
    }

    bool DecodeApp(const toml::table& table, const std::string& path,
                   AppRulesV2* app) {
        bool valid = CheckFields(table,
            {"enabled", "users", "processes", "provider", "deny_rules",
             "redirect_rules", "observe_rules", "export_rules"}, path);
        valid = DecodeBool(table, "enabled", path + "/enabled", &app->enabled)
            && valid;
        if (const toml::node* users = table.get("users")) {
            valid = DecodeUsers(*users, path + "/users", &app->users) && valid;
        }
        if (const toml::node* processes = table.get("processes")) {
            valid = DecodeProcesses(*processes, app->package, path + "/processes",
                                    &app->processes) && valid;
        }
        if (const toml::node* provider = table.get("provider")) {
            valid = DecodeProvider(*provider, path + "/provider", &app->provider)
                && valid;
        }
        if (const toml::node* deny = table.get("deny_rules")) {
            valid = DecodeActions(*deny, RuleActionKind::kDeny,
                                  path + "/deny_rules", app) && valid;
        }
        if (const toml::node* redirect = table.get("redirect_rules")) {
            valid = DecodeActions(*redirect, RuleActionKind::kRedirect,
                                  path + "/redirect_rules", app) && valid;
        }
        if (const toml::node* observe = table.get("observe_rules")) {
            valid = DecodeActions(*observe, RuleActionKind::kObserve,
                                  path + "/observe_rules", app) && valid;
        }
        if (const toml::node* export_rules = table.get("export_rules")) {
            valid = DecodeActions(*export_rules, RuleActionKind::kExport,
                                  path + "/export_rules", app) && valid;
        }
        if (app->actions.size() > limits_.max_rules_per_app) {
            Add(kResourceLimit, "rules.app_rule_limit",
                SourceSpan(source_, table.source()), path);
            valid = false;
        }
        return valid;
    }

    const SourceBuffer& source_;
    const RulesLimits& limits_;
    RulesV2ParseResult result_;
    RuleId next_rule_id_ = 1;
};

void AddBuildDiagnostic(RulesV2BuildResult* result, std::string_view code,
                        std::string_view message, RuleId rule_id,
                        std::size_t actual = 0, std::size_t limit = 0) {
    Diagnostic diagnostic{code, message, {}, false};
    diagnostic.phase = DiagnosticPhase::kSemantic;
    diagnostic.field_path = "/rules/" + std::to_string(rule_id);
    if (limit != 0) {
        diagnostic.field_path += "/limit=" + std::to_string(limit)
            + "/actual=" + std::to_string(actual);
    }
    result->diagnostics.push_back(std::move(diagnostic));
}

bool ValidateStoragePath(std::string_view path, const RulesLimits& limits) {
    return NormalizeRulePath(path, limits).has_value();
}

}  // namespace

RulesV2ParseResult ParseRulesDocumentV2(const SourceBuffer& source,
                                        const RulesLimits& limits) {
    return Decoder(source, limits).Run();
}

RulesV2BuildResult BuildCanonicalPolicyV2(
    const RulesDocumentV2& document, const RulesLimits& limits,
    const pathguard::pattern::PatternLimitsProfile& pattern_limits) {
    RulesV2BuildResult result;
    CanonicalPolicyV2 policy;
    policy.allow_legacy_mount = document.allow_legacy_mount;

    for (const AppRulesV2& app : document.apps) {
        if (!app.enabled) continue;
        CanonicalAppPolicyV2 canonical_app;
        canonical_app.package = app.package;
        canonical_app.users = app.users;
        canonical_app.processes = app.processes;
        canonical_app.provider = app.provider;
        std::size_t token_total = 0;
        std::size_t except_total = 0;
        for (const ActionRuleInputV2& action : app.actions) {
            if (!ValidateStoragePath(action.select.root, limits)
                || ((action.action == RuleActionKind::kRedirect
                     || action.action == RuleActionKind::kExport)
                    && !ValidateStoragePath(action.target, limits))) {
                AddBuildDiagnostic(&result, kPathInvalid,
                                   "rules.storage_path_invalid", action.id);
                continue;
            }
            const BraceExpansionResult bases = ExpandPatternBraces(
                action.select.glob, pattern_limits);
            if (!bases.ok()) {
                AddBuildDiagnostic(&result,
                    bases.error == BraceExpandError::kResultLimit
                        || bases.error == BraceExpandError::kByteLimit
                        ? kResourceLimit : kBraceSyntax,
                    "rules.brace_invalid", action.id, bases.actual, bases.limit);
                continue;
            }

            std::vector<PatternProgram> except_programs;
            bool except_ok = true;
            for (const std::string& except_source : action.select.except) {
                const BraceExpansionResult expanded = ExpandPatternBraces(
                    except_source, pattern_limits);
                if (!expanded.ok()) {
                    AddBuildDiagnostic(&result, kBraceSyntax,
                                       "rules.except_brace_invalid", action.id);
                    except_ok = false;
                    break;
                }
                for (const std::string& pattern : expanded.patterns) {
                    PatternCompileResult compiled = CompilePattern(
                        pattern, pattern_limits);
                    if (!compiled.ok()) {
                        AddBuildDiagnostic(&result, kPatternSyntax,
                                           "rules.except_pattern_invalid", action.id);
                        except_ok = false;
                        break;
                    }
                    except_programs.push_back(std::move(*compiled.program));
                }
                if (!except_ok) break;
            }
            if (!except_ok) continue;
            std::sort(except_programs.begin(), except_programs.end(),
                [](const PatternProgram& lhs, const PatternProgram& rhs) {
                    return lhs.canonical < rhs.canonical;
                });
            except_programs.erase(std::unique(except_programs.begin(),
                except_programs.end(), [](const PatternProgram& lhs,
                                          const PatternProgram& rhs) {
                    return lhs.canonical == rhs.canonical;
                }), except_programs.end());
            if (except_programs.size()
                > pattern_limits.max_except_patterns_per_selector
                || except_total + except_programs.size()
                    > pattern_limits.max_except_refs_per_app) {
                AddBuildDiagnostic(&result, kResourceLimit,
                    "rules.except_limit", action.id, except_programs.size(),
                    pattern_limits.max_except_patterns_per_selector);
                continue;
            }

            for (const std::string& base : bases.patterns) {
                PatternCompileResult compiled = CompilePattern(base, pattern_limits);
                if (!compiled.ok()) {
                    AddBuildDiagnostic(&result,
                        compiled.error == PatternCompileError::kResourceLimit
                            ? kResourceLimit : kPatternSyntax,
                        "rules.pattern_invalid", action.id);
                    break;
                }
                std::size_t action_tokens = compiled.program->token_count;
                for (const PatternProgram& except : except_programs) {
                    action_tokens += except.token_count;
                }
                if (token_total + action_tokens
                    > pattern_limits.max_pattern_tokens_per_app) {
                    AddBuildDiagnostic(&result, kResourceLimit,
                        "rules.pattern_token_limit", action.id,
                        token_total + action_tokens,
                        pattern_limits.max_pattern_tokens_per_app);
                    break;
                }
                CanonicalActionV2 canonical_action;
                canonical_action.id = action.id;
                canonical_action.action = action.action;
                canonical_action.target = action.target;
                canonical_action.priority = action.priority;
                canonical_action.preserve = action.preserve;
                canonical_action.collision = action.collision;
                canonical_action.enforcement = action.enforcement;
                canonical_action.export_mode = action.export_mode;
                canonical_action.media_scan = action.media_scan;
                canonical_action.audit = action.audit;
                canonical_action.selector.root = action.select.root;
                canonical_action.selector.glob = base;
                canonical_action.selector.object_type = action.select.object_type;
                canonical_action.selector.source_kind =
                    HasUnescapedMeta(base) || !except_programs.empty()
                    ? SelectorSourceKind::kGlob : SelectorSourceKind::kLiteral;
                canonical_action.selector.base_pattern =
                    std::move(*compiled.program);
                canonical_action.selector.except_patterns = except_programs;
                std::size_t depth = 1;
                for (const char value : action.select.root) {
                    if (value == '/') ++depth;
                }
                canonical_action.selector.specificity =
                    static_cast<std::uint16_t>(std::min<std::size_t>(65535,
                        canonical_action.selector.base_pattern.specificity
                            + depth * 32));
                canonical_app.actions.push_back(std::move(canonical_action));
                token_total += action_tokens;
                except_total += except_programs.size();
            }
        }
        if (canonical_app.actions.size() > pattern_limits.max_actions_per_app
            || result.diagnostics.size() > limits.max_diagnostics) {
            AddBuildDiagnostic(&result, kResourceLimit,
                               "rules.action_limit", 0,
                               canonical_app.actions.size(),
                               pattern_limits.max_actions_per_app);
        }
        std::sort(canonical_app.actions.begin(), canonical_app.actions.end(),
            [](const CanonicalActionV2& lhs, const CanonicalActionV2& rhs) {
                return std::tie(lhs.selector.root, lhs.selector.glob, lhs.action,
                                lhs.priority, lhs.target, lhs.id)
                    < std::tie(rhs.selector.root, rhs.selector.glob, rhs.action,
                               rhs.priority, rhs.target, rhs.id);
            });
        policy.apps.push_back(std::move(canonical_app));
    }
    std::sort(policy.apps.begin(), policy.apps.end(),
              [](const CanonicalAppPolicyV2& lhs,
                 const CanonicalAppPolicyV2& rhs) {
                  return lhs.package < rhs.package;
              });
    if (result.diagnostics.empty()) result.canonical = std::move(policy);
    return result;
}

}  // namespace pathguard::rules
