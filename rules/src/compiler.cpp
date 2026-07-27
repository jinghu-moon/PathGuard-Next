#include "pathguard/rules/compiler.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "toml.hpp"
#include "pathguard/rules/arrow_scanner.h"
#include "pathguard/rules/format_probe.h"

namespace pathguard::rules {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t ElapsedNs(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start).count());
}

struct InlineNode {
    const toml::table* table = nullptr;
    ByteSpan generated;
    std::vector<std::string> path;
    const GeneratedRedirect* origin = nullptr;
};

std::size_t Utf8Width(unsigned char value) {
    if ((value & 0x80U) == 0) return 1;
    if ((value & 0xe0U) == 0xc0U) return 2;
    if ((value & 0xf0U) == 0xe0U) return 3;
    if ((value & 0xf8U) == 0xf0U) return 4;
    return 1;
}

class GeneratedPositionIndex {
public:
    explicit GeneratedPositionIndex(std::string_view source)
        : source_(source), line_starts_{0} {
        for (std::uint32_t offset = 0; offset < source.size(); ++offset) {
            if (source[offset] == '\n') line_starts_.push_back(offset + 1);
        }
    }

    std::uint32_t ToByte(toml::source_position position) const {
        if (position.line == 0 || position.line > line_starts_.size()) {
            return static_cast<std::uint32_t>(source_.size());
        }
        std::size_t offset = line_starts_[position.line - 1];
        std::size_t column = 1;
        while (offset < source_.size() && column < position.column
               && source_[offset] != '\n') {
            if (source_[offset] == '\r') {
                ++offset;
            } else {
                offset += std::min(Utf8Width(
                    static_cast<unsigned char>(source_[offset])),
                    source_.size() - offset);
                ++column;
            }
        }
        return static_cast<std::uint32_t>(offset);
    }

    ByteSpan Region(const toml::source_region& region) const {
        return {ToByte(region.begin), ToByte(region.end)};
    }

private:
    std::string_view source_;
    std::vector<std::uint32_t> line_starts_;
};

bool IsPackageName(std::string_view value) {
    std::size_t components = 0;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const std::size_t end = value.find('.', begin);
        const std::size_t limit = end == std::string_view::npos ? value.size() : end;
        if (begin == limit
            || !std::isalpha(static_cast<unsigned char>(value[begin]))) {
            return false;
        }
        for (std::size_t index = begin + 1; index < limit; ++index) {
            const unsigned char byte = static_cast<unsigned char>(value[index]);
            if (!std::isalnum(byte) && byte != '_') return false;
        }
        ++components;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return components >= 2;
}

bool IsProcessName(std::string_view package, std::string_view process) {
    if (process == package) return true;
    if (!process.starts_with(package) || process.size() <= package.size() + 1
        || process[package.size()] != ':') {
        return false;
    }
    for (std::size_t index = package.size() + 1; index < process.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(process[index]);
        if (!std::isalnum(byte) && byte != '_' && byte != '.') return false;
    }
    return true;
}

bool IsRedirectScope(const std::vector<std::string>& path) {
    return path.size() == 4 && path[0] == "apps" && !path[1].empty()
        && path[2] == "redirect";
}

void CollectInlineNodes(const toml::node& node,
                        std::vector<std::string>* path,
                        const GeneratedPositionIndex& positions,
                        std::vector<InlineNode>* output) {
    if (const toml::table* table = node.as_table()) {
        if (table->is_inline()) {
            output->push_back({table, positions.Region(table->source()),
                               *path, nullptr});
        }
        table->for_each([&](const toml::key& key, const toml::node& child) {
            path->push_back(std::string(key.str()));
            CollectInlineNodes(child, path, positions, output);
            path->pop_back();
        });
        return;
    }
    if (const toml::array* array = node.as_array()) {
        for (std::size_t index = 0; index < array->size(); ++index) {
            path->push_back(std::to_string(index));
            CollectInlineNodes(*array->get(index), path, positions, output);
            path->pop_back();
        }
    }
}

class Decoder {
public:
    Decoder(const SourceBuffer& source, DesugarResult desugared,
            const RulesLimits& limits)
        : source_(source), desugared_(std::move(desugared)), limits_(limits) {}

    RulesCompileResult Run() {
        if (!desugared_.ok()) {
            result_.diagnostics = std::move(desugared_.diagnostics);
            return std::move(result_);
        }
        generated_source_ = desugared_.parser_input(source_);
        GeneratedPositionIndex positions(generated_source_);
        positions_ = &positions;
        const auto parse_started = Clock::now();
        toml::parse_result parsed = toml::parse(generated_source_, source_.file_name());
        result_.statistics.parse_ns = ElapsedNs(parse_started);
        if (!parsed) {
            ByteSpan generated = positions_->Region(parsed.error().source());
            const auto original = desugared_.rewrite_map.MapGeneratedSpan(generated);
            Add(kTomlParse, "rules.toml_parse",
                original.value_or(ByteSpan{0, 0}), DiagnosticPhase::kParse, "");
            return std::move(result_);
        }
        toml::table& root = parsed.table();
        const auto scope_started = Clock::now();
        const bool bound = BindGenerated(root);
        result_.statistics.scope_ns = ElapsedNs(scope_started);
        if (!bound) return std::move(result_);
        for (const GeneratedRedirect& redirect : desugared_.redirects) {
            next_rule_id_ = std::max(next_rule_id_, redirect.id + 1);
        }
        RulesDocument document;
        const auto decode_started = Clock::now();
        const bool decoded = DecodeRoot(root, &document);
        result_.statistics.decode_ns = ElapsedNs(decode_started);
        if (!decoded || !result_.diagnostics.empty()) {
            result_.origins = OriginMap{};
            return std::move(result_);
        }
        result_.document = std::move(document);
        return std::move(result_);
    }

private:
    void Add(std::string_view code, std::string_view message_key, ByteSpan span,
             DiagnosticPhase phase, std::string field_path) {
        const std::size_t limit = std::max<std::size_t>(limits_.max_diagnostics, 1);
        if (result_.diagnostics.size() < limit) {
            Diagnostic diagnostic{code, message_key, span, false};
            diagnostic.phase = phase;
            diagnostic.field_path = std::move(field_path);
            result_.diagnostics.push_back(std::move(diagnostic));
        } else if (!result_.diagnostics.back().omitted) {
            Diagnostic omitted{kDiagnosticsOmitted, "rules.diagnostics_omitted",
                               span, true};
            omitted.phase = phase;
            result_.diagnostics.back() = std::move(omitted);
        }
    }

    ByteSpan Original(const toml::source_region& region) {
        const ByteSpan generated = positions_->Region(region);
        return desugared_.rewrite_map.MapGeneratedSpan(generated)
            .value_or(ByteSpan{0, 0});
    }

    bool BindGenerated(const toml::table& root) {
        std::vector<std::string> path;
        CollectInlineNodes(root, &path, *positions_, &inline_nodes_);
        std::unordered_map<std::uint64_t, InlineNode*> nodes_by_span;
        nodes_by_span.reserve(inline_nodes_.size());
        for (InlineNode& node : inline_nodes_) {
            const std::uint64_t identity =
                (static_cast<std::uint64_t>(node.generated.begin) << 32U)
                | node.generated.end;
            if (!nodes_by_span.emplace(identity, &node).second) {
                Add(kDesugarInternal, "rules.generated_duplicate", {},
                    DiagnosticPhase::kInternal, "");
                return false;
            }
        }
        std::unordered_set<std::uint64_t> generated_spans;
        generated_spans.reserve(desugared_.redirects.size());
        for (const GeneratedRedirect& redirect : desugared_.redirects) {
            const std::uint64_t identity =
                (static_cast<std::uint64_t>(redirect.generated_table.begin) << 32U)
                | redirect.generated_table.end;
            if (!generated_spans.insert(identity).second) {
                Add(kDesugarInternal, "rules.generated_duplicate",
                    redirect.original_rule, DiagnosticPhase::kInternal, "");
                return false;
            }
            const auto found = nodes_by_span.find(identity);
            InlineNode* match = found == nodes_by_span.end()
                ? nullptr : found->second;
            if (match == nullptr || match->origin != nullptr) {
                Add(kDesugarInternal, "rules.generated_unmatched",
                    redirect.original_rule, DiagnosticPhase::kInternal, "");
                return false;
            }
            match->origin = &redirect;
            bound_[match->table] = &redirect;
        }
        bool scope_error = false;
        for (const InlineNode& node : inline_nodes_) {
            if (node.origin != nullptr && !IsRedirectScope(node.path)) {
                Add(kRuleArrowScope, "rules.arrow_scope",
                    node.origin->original_rule, DiagnosticPhase::kDecode,
                    "/apps/<package>/redirect");
                scope_error = true;
            }
        }
        return !scope_error;
    }

    bool DecodeRoot(const toml::table& root, RulesDocument* document) {
        static constexpr std::string_view allowed[]{"format", "compatibility",
                                                     "apps"};
        bool valid = CheckFields(root, allowed, "");
        const toml::node* format = root.get("format");
        const auto version = format ? format->value<std::int64_t>() : std::nullopt;
        if (!version || *version != 1) {
            Add(kTypeMismatch, "rules.format_type",
                format ? Original(format->source()) : ByteSpan{0, 0},
                DiagnosticPhase::kDecode, "/format");
            valid = false;
        }
        if (const toml::node* compatibility = root.get("compatibility")) {
            const toml::table* table = compatibility->as_table();
            if (table == nullptr) {
                Add(kTypeMismatch, "rules.compatibility_type",
                    Original(compatibility->source()), DiagnosticPhase::kDecode,
                    "/compatibility");
                valid = false;
            } else {
                valid = DecodeCompatibility(*table, &document->compatibility)
                    && valid;
            }
        }
        const toml::node* apps_node = root.get("apps");
        const toml::table* apps = apps_node ? apps_node->as_table() : nullptr;
        if (apps == nullptr || apps->empty()) {
            Add(kTypeMismatch, "rules.apps_required",
                apps_node ? Original(apps_node->source()) : ByteSpan{0, 0},
                DiagnosticPhase::kDecode, "/apps");
            return false;
        }
        if (apps->size() > limits_.max_apps) {
            Add(kResourceLimit, "rules.apps_limit", Original(apps->source()),
                DiagnosticPhase::kDecode, "/apps");
            return false;
        }
        apps->for_each([&](const toml::key& package_key, const toml::node& node) {
            const std::string package(package_key.str());
            if (!IsPackageName(package)) {
                Add(kInvalidValue, "rules.package_invalid",
                    Original(package_key.source()), DiagnosticPhase::kDecode,
                    "/apps/" + package);
                valid = false;
                return;
            }
            const toml::table* table = node.as_table();
            if (table == nullptr) {
                Add(kTypeMismatch, "rules.app_type", Original(node.source()),
                    DiagnosticPhase::kDecode, "/apps/" + package);
                valid = false;
                return;
            }
            AppRules app;
            app.package = package;
            if (!DecodeApp(*table, &app)) valid = false;
            document->apps.push_back(std::move(app));
        });
        return valid;
    }

    bool DecodeCompatibility(const toml::table& table,
                             CompatibilityRules* compatibility) {
        static constexpr std::string_view allowed[]{"allow_legacy_mount"};
        bool valid = CheckFields(table, allowed, "/compatibility");
        if (const toml::node* value = table.get("allow_legacy_mount")) {
            const auto decoded = value->value<bool>();
            if (!decoded) {
                Add(kTypeMismatch, "rules.boolean_required",
                    Original(value->source()), DiagnosticPhase::kDecode,
                    "/compatibility/allow_legacy_mount");
                valid = false;
            } else {
                compatibility->allow_legacy_mount = *decoded;
            }
        }
        return valid;
    }

    bool DecodeApp(const toml::table& table, AppRules* app) {
        static constexpr std::string_view allowed[]{
            "enabled", "users", "processes", "file_picker", "deny", "redirect"};
        const std::string base = "/apps/" + app->package;
        bool valid = CheckFields(table, allowed, base);
        valid = DecodeBool(table, "enabled", base + "/enabled", &app->enabled)
            && valid;
        valid = DecodeBool(table, "file_picker", base + "/file_picker",
                           &app->file_picker) && valid;
        if (const toml::node* users = table.get("users")) {
            valid = DecodeUsers(*users, base + "/users", &app->users) && valid;
        }
        if (const toml::node* processes = table.get("processes")) {
            valid = DecodeProcesses(*processes, app, base + "/processes") && valid;
        }
        if (const toml::node* deny = table.get("deny")) {
            valid = DecodeDeny(*deny, base + "/deny", app) && valid;
        }
        if (const toml::node* redirect = table.get("redirect")) {
            valid = DecodeRedirect(*redirect, base + "/redirect", app) && valid;
        }
        if (app->deny.size() + app->redirects.size()
            > limits_.max_rules_per_app) {
            Add(kResourceLimit, "rules.app_rule_limit", Original(table.source()),
                DiagnosticPhase::kDecode, base);
            valid = false;
        }
        return valid;
    }

    bool DecodeBool(const toml::table& table, std::string_view key,
                    const std::string& path, bool* output) {
        const toml::node* node = table.get(key);
        if (node == nullptr) return true;
        const auto value = node->value<bool>();
        if (!value) {
            Add(kTypeMismatch, "rules.boolean_required", Original(node->source()),
                DiagnosticPhase::kDecode, path);
            return false;
        }
        *output = *value;
        return true;
    }

    bool DecodeUsers(const toml::node& node, const std::string& path,
                     std::vector<std::int32_t>* output) {
        const toml::array* array = node.as_array();
        if (array == nullptr) {
            Add(kTypeMismatch, "rules.integer_array_required",
                Original(node.source()), DiagnosticPhase::kDecode, path);
            return false;
        }
        output->clear();
        std::unordered_set<std::int64_t> unique;
        bool valid = true;
        for (const toml::node& value_node : *array) {
            const auto value = value_node.value<std::int64_t>();
            if (!value) {
                Add(kTypeMismatch, "rules.integer_required",
                    Original(value_node.source()), DiagnosticPhase::kDecode, path);
                valid = false;
            } else if (*value < 0 || *value > 99'999
                       || !unique.insert(*value).second) {
                Add(kInvalidValue, "rules.user_invalid",
                    Original(value_node.source()), DiagnosticPhase::kDecode, path);
                valid = false;
            } else {
                output->push_back(static_cast<std::int32_t>(*value));
            }
        }
        if (output->empty()) {
            Add(kInvalidValue, "rules.users_empty", Original(node.source()),
                DiagnosticPhase::kDecode, path);
            valid = false;
        }
        return valid;
    }

    bool DecodeProcesses(const toml::node& node, AppRules* app,
                         const std::string& path) {
        const toml::array* array = node.as_array();
        if (array == nullptr) {
            Add(kTypeMismatch, "rules.string_array_required",
                Original(node.source()), DiagnosticPhase::kDecode, path);
            return false;
        }
        std::unordered_set<std::string> unique;
        bool valid = true;
        for (const toml::node& value_node : *array) {
            const auto value = value_node.value<std::string>();
            if (!value) {
                Add(kTypeMismatch, "rules.string_required",
                    Original(value_node.source()), DiagnosticPhase::kDecode, path);
                valid = false;
            } else if (!IsProcessName(app->package, *value)
                       || !unique.insert(*value).second) {
                Add(kInvalidValue, "rules.process_invalid",
                    Original(value_node.source()), DiagnosticPhase::kDecode, path);
                valid = false;
            } else {
                app->processes.push_back(*value);
            }
        }
        return valid;
    }

    bool DecodeDeny(const toml::node& node, const std::string& path,
                    AppRules* app) {
        const toml::array* array = node.as_array();
        if (array == nullptr) {
            Add(kTypeMismatch, "rules.string_array_required",
                Original(node.source()), DiagnosticPhase::kDecode, path);
            return false;
        }
        bool valid = true;
        for (const toml::node& value_node : *array) {
            const auto value = value_node.value<std::string>();
            if (!value) {
                Add(kTypeMismatch, "rules.string_required",
                    Original(value_node.source()), DiagnosticPhase::kDecode, path);
                valid = false;
                continue;
            }
            const RuleId id = next_rule_id_++;
            const ByteSpan span = Original(value_node.source());
            app->deny.push_back({id, *value});
            if (!result_.origins.Add(id, {span, {}, {}, {}, false})) {
                Add(kDesugarInternal, "rules.origin_duplicate", span,
                    DiagnosticPhase::kInternal, path);
                valid = false;
            }
        }
        return valid;
    }

    bool DecodeRedirect(const toml::node& node, const std::string& path,
                        AppRules* app) {
        const toml::array* array = node.as_array();
        if (array == nullptr) {
            Add(kRedirectSyntax, "rules.redirect_array_required",
                Original(node.source()), DiagnosticPhase::kDecode, path);
            return false;
        }
        bool valid = true;
        for (const toml::node& value_node : *array) {
            const toml::table* table = value_node.as_table();
            const auto bound = table ? bound_.find(table) : bound_.end();
            if (table == nullptr || bound == bound_.end()) {
                Add(kRedirectSyntax, "rules.redirect_arrow_required",
                    Original(value_node.source()), DiagnosticPhase::kDecode, path);
                valid = false;
                continue;
            }
            const GeneratedRedirect& generated = *bound->second;
            const toml::node* from = table->get("from");
            const toml::node* to = table->get("to");
            const auto source_value = from ? from->value<std::string>() : std::nullopt;
            const auto target_value = to ? to->value<std::string>() : std::nullopt;
            if (table->size() != 2 || !source_value || !target_value) {
                Add(kDesugarInternal, "rules.generated_shape_invalid",
                    generated.original_rule, DiagnosticPhase::kInternal, path);
                valid = false;
                continue;
            }
            app->redirects.push_back({generated.id, *source_value, *target_value});
            RuleOrigin origin{generated.original_rule, generated.original_source,
                              generated.original_arrow, generated.original_target,
                              true};
            if (!result_.origins.Add(generated.id, origin)) {
                Add(kDesugarInternal, "rules.origin_duplicate",
                    generated.original_rule, DiagnosticPhase::kInternal, path);
                valid = false;
            }
        }
        return valid;
    }

    template <std::size_t Size>
    bool CheckFields(const toml::table& table,
                     const std::string_view (&allowed)[Size],
                     const std::string& path) {
        bool valid = true;
        table.for_each([&](const toml::key& key, const toml::node&) {
            if (std::find(std::begin(allowed), std::end(allowed), key.str())
                == std::end(allowed)) {
                Add(kUnknownField, "rules.unknown_field", Original(key.source()),
                    DiagnosticPhase::kDecode, path + "/" + std::string(key.str()));
                valid = false;
            }
        });
        return valid;
    }

    const SourceBuffer& source_;
    DesugarResult desugared_;
    const RulesLimits& limits_;
    RulesCompileResult result_;
    std::string_view generated_source_;
    const GeneratedPositionIndex* positions_ = nullptr;
    std::vector<InlineNode> inline_nodes_;
    std::unordered_map<const toml::table*, const GeneratedRedirect*> bound_;
    RuleId next_rule_id_ = 1;
};

}  // namespace

bool OriginMap::Add(RuleId id, RuleOrigin origin) {
    return values_.emplace(id, std::move(origin)).second;
}

const RuleOrigin* OriginMap::Find(RuleId id) const {
    const auto found = values_.find(id);
    return found == values_.end() ? nullptr : &found->second;
}

RulesCompileResult DecodeDesugaredRules(const SourceBuffer& source,
                                        DesugarResult desugared,
                                        const RulesLimits& limits) {
    return Decoder(source, std::move(desugared), limits).Run();
}

RulesCompileResult ParseRulesDocument(const SourceBuffer& source,
                                      const RulesLimits& limits) {
    RulesCompileResult failed;
    failed.statistics.source_bytes = source.size();
    const auto format_started = Clock::now();
    const FormatProbeResult format = ProbeRulesFormat(source);
    failed.statistics.format_probe_ns = ElapsedNs(format_started);
    if (!format.ok()) {
        failed.diagnostics.push_back(format.diagnostic);
        return failed;
    }

    const auto lex_started = Clock::now();
    ArrowScanResult lexical = ScanArrowCandidates(source, limits);
    failed.statistics.lex_ns = ElapsedNs(lex_started);
    failed.statistics.arrow_count = lexical.candidates.size();
    if (!lexical.parser_allowed()) {
        failed.diagnostics = std::move(lexical.diagnostics);
        return failed;
    }

    std::vector<ArrowRewrite> rewrites;
    rewrites.reserve(lexical.candidates.size());
    for (std::size_t index = 0; index < lexical.candidates.size(); ++index) {
        const ArrowCandidate& candidate = lexical.candidates[index];
        rewrites.push_back({static_cast<RuleId>(index + 1), candidate.rule,
                            candidate.source, candidate.arrow, candidate.target});
    }
    const auto rewrite_started = Clock::now();
    DesugarResult desugared = EmitDesugaredSource(source, rewrites, limits);
    failed.statistics.rewrite_ns = ElapsedNs(rewrite_started);
    failed.statistics.rewrite_count = rewrites.size();
    failed.statistics.generated_bytes = desugared.parser_input(source).size();
    if (!desugared.ok()) {
        failed.diagnostics = std::move(desugared.diagnostics);
        return failed;
    }
    RulesCompileResult result = DecodeDesugaredRules(
        source, std::move(desugared), limits);
    result.statistics.source_bytes = failed.statistics.source_bytes;
    result.statistics.generated_bytes = failed.statistics.generated_bytes;
    result.statistics.arrow_count = failed.statistics.arrow_count;
    result.statistics.rewrite_count = failed.statistics.rewrite_count;
    result.statistics.format_probe_ns = failed.statistics.format_probe_ns;
    result.statistics.lex_ns = failed.statistics.lex_ns;
    result.statistics.rewrite_ns = failed.statistics.rewrite_ns;
    return result;
}

}  // namespace pathguard::rules
