#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy.h"
#include "pathguard/policy_v6.h"
#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules/tools.h"

namespace fs = std::filesystem;

static bool Read(const fs::path& path, std::string* out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    *out = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

static int PrintStatus(const fs::path& module_dir, const char* pid) {
    const fs::path directory = module_dir / "run" / "status";
    if (pid != nullptr) {
        std::string text;
        if (!Read(directory / (std::string(pid) + ".status"), &text)) {
            std::cerr << "status not found\n";
            return 1;
        }
        std::cout << text;
        return 0;
    }
    std::string rules_status;
    if (Read(module_dir / "run" / "rules-status.txt", &rules_status)) {
        std::cout << rules_status;
    }
    std::error_code error;
    if (!fs::is_directory(directory, error)) {
        std::cout << "no runtime status\n";
        return 0;
    }
    std::vector<fs::path> entries;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".status") {
            entries.push_back(entry.path());
        }
    }
    std::sort(entries.begin(), entries.end());
    for (const fs::path& entry : entries) {
        std::string text;
        if (Read(entry, &text)) std::cout << "[" << entry.stem().string() << "]\n" << text;
    }
    return error ? 1 : 0;
}

static pathguard::rules::RulesBuildResult CompileRulesFile(
        const fs::path& path, std::optional<pathguard::rules::SourceBuffer>* source,
        std::string* load_error) {
    using namespace pathguard::rules;
    std::string text;
    if (!Read(path, &text)) {
        *load_error = "cannot read rules.toml";
        return {};
    }
    Diagnostic diagnostic;
    *source = SourceBuffer::Create(
        path.filename().string(), std::move(text), RulesLimits{}, &diagnostic);
    if (!source->has_value()) {
        RulesBuildResult result;
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    return CompileRules(**source, RulesLimits{});
}

static int ValidateOrCompile(const std::string& command, int argc, char** argv) {
    using namespace pathguard::rules;
    if (argc < 3) {
        std::cerr << "missing rules.toml\n";
        return 2;
    }
    bool json = false;
    bool device = false;
    for (int index = 3; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--json") json = true;
        if (option == "--device") device = true;
    }
    std::optional<SourceBuffer> source;
    std::string load_error;
    RulesBuildResult result = CompileRulesFile(argv[2], &source, &load_error);
    if (!load_error.empty()) {
        std::cerr << load_error << '\n';
        return 1;
    }
    if (!result.ok()) {
        if (source.has_value()) {
            for (const Diagnostic& diagnostic : result.diagnostics) {
                std::cerr << (json ? RenderDiagnosticJson(diagnostic, *source)
                                  : RenderDiagnosticText(diagnostic, *source))
                          << '\n';
            }
        }
        return 1;
    }
    if (device) {
        std::cerr << "environment_unsupported: use reload so the daemon can "
                     "validate the current device snapshot\n";
        return 1;
    }
    if (command == "validate") {
        std::cout << "valid: " << result.canonical_v2->apps.size()
                  << " package(s), content_generation="
                  << result.blob->content_generation << '\n';
        return 0;
    }
    if (argc < 4 || std::string(argv[3]).starts_with("--")) {
        std::cerr << "missing policy.bin output\n";
        return 2;
    }
    const fs::path output_path = argv[3];
    if (output_path.filename() == "policy.bin"
        && output_path.parent_path().filename() == "run") {
        std::cerr << "refusing to write an active policy path; use reload\n";
        return 1;
    }
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(result.blob->bytes.data()),
                 static_cast<std::streamsize>(result.blob->bytes.size()));
    if (!output) {
        std::cerr << "cannot write policy.bin\n";
        return 1;
    }
    std::cout << "compiled: " << result.blob->bytes.size()
              << " bytes, content_generation="
              << result.blob->content_generation << '\n';
    return 0;
}

static void PrintDiagnostics(
        const pathguard::rules::RulesBuildResult& result,
        const pathguard::rules::SourceBuffer& source,
        bool errors_only = false) {
    using namespace pathguard::rules;
    for (const Diagnostic& diagnostic : result.diagnostics) {
        if (errors_only
            && diagnostic.severity != DiagnosticSeverity::kError) continue;
        std::cerr << RenderDiagnosticText(diagnostic, source) << '\n';
    }
}

static int LintRulesFile(const fs::path& path) {
    using namespace pathguard::rules;
    std::optional<SourceBuffer> source;
    std::string load_error;
    const RulesBuildResult result = CompileRulesFile(path, &source, &load_error);
    if (!load_error.empty()) {
        std::cerr << load_error << '\n';
        return 1;
    }
    if (!source.has_value()) return 1;
    PrintDiagnostics(result, *source, true);
    for (const Diagnostic& diagnostic : LintRules(result, RulesLimits{})) {
        std::cout << RenderDiagnosticText(diagnostic, *source) << '\n';
    }
    return result.ok() ? 0 : 1;
}

static const char* ChangeName(pathguard::rules::PolicyChangeKind kind) {
    using pathguard::rules::PolicyChangeKind;
    switch (kind) {
        case PolicyChangeKind::kAdd: return "add";
        case PolicyChangeKind::kRemove: return "remove";
        case PolicyChangeKind::kModify: return "modify";
    }
    return "unknown";
}

static const char* RuleName(pathguard::rules::PolicyRuleKind kind) {
    return kind == pathguard::rules::PolicyRuleKind::kDeny
        ? "deny" : "redirect";
}

static int PlanRulesFiles(const fs::path& before_path,
                          const fs::path& after_path) {
    using namespace pathguard::rules;
    std::optional<SourceBuffer> before_source;
    std::optional<SourceBuffer> after_source;
    std::string error;
    const RulesBuildResult before = CompileRulesFile(
        before_path, &before_source, &error);
    if (!error.empty() || !before_source.has_value()) {
        std::cerr << (error.empty() ? "invalid old rules.toml" : error) << '\n';
        return 1;
    }
    if (!before.ok()) {
        PrintDiagnostics(before, *before_source);
        return 1;
    }
    error.clear();
    const RulesBuildResult after = CompileRulesFile(
        after_path, &after_source, &error);
    if (!error.empty() || !after_source.has_value()) {
        std::cerr << (error.empty() ? "invalid new rules.toml" : error) << '\n';
        return 1;
    }
    if (!after.ok()) {
        PrintDiagnostics(after, *after_source);
        return 1;
    }
    for (const PolicyChange& change :
         BuildPolicyPlan(*before.canonical, *after.canonical)) {
        std::cout << ChangeName(change.kind) << ' ' << change.package << ' '
                  << RuleName(change.rule_kind) << ' ' << change.source;
        if (!change.before_target.empty()) {
            std::cout << " from=" << change.before_target;
        }
        if (!change.after_target.empty()) {
            std::cout << " to=" << change.after_target;
        }
        std::cout << '\n';
    }
    return 0;
}

static int ExplainRulesPath(const fs::path& rules_path,
                            std::string_view package,
                            std::string_view path) {
    using namespace pathguard::rules;
    std::optional<SourceBuffer> source;
    std::string error;
    const RulesBuildResult result = CompileRulesFile(
        rules_path, &source, &error);
    if (!error.empty() || !source.has_value()) {
        std::cerr << (error.empty() ? "invalid rules.toml" : error) << '\n';
        return 1;
    }
    if (!result.resolved.has_value()) {
        PrintDiagnostics(result, *source);
        return 1;
    }
    const PathExplanation explanation = ExplainPath(
        *result.resolved, package, path, RulesLimits{});
    std::cout << "package=" << explanation.package
              << "\npath=" << explanation.query << '\n';
    if (!explanation.source.has_value()) {
        std::cout << "match=none\nshadowed_parent=none\n";
        return 0;
    }
    std::cout << "match=" << RuleName(*explanation.action) << ' '
              << *explanation.source;
    if (explanation.target.has_value()) {
        std::cout << " -> " << *explanation.target;
    }
    std::cout << '\n';
    if (explanation.shadowed_parents.empty()) {
        std::cout << "shadowed_parent=none\n";
    } else {
        for (const std::string& parent : explanation.shadowed_parents) {
            std::cout << "shadowed_parent=" << parent << '\n';
        }
    }
    return 0;
}

static const char* ActionName(pathguard::PolicyActionKind action) {
    switch (action) {
        case pathguard::PolicyActionKind::kDeny: return "deny";
        case pathguard::PolicyActionKind::kRedirect: return "redirect";
        case pathguard::PolicyActionKind::kObserve: return "observe";
        case pathguard::PolicyActionKind::kExport: return "export";
    }
    return "unknown";
}

static const char* DomainName(pathguard::PolicyExecutionDomain domain) {
    switch (domain) {
        case pathguard::PolicyExecutionDomain::kMount: return "mount";
        case pathguard::PolicyExecutionDomain::kAppPath: return "app_path";
        case pathguard::PolicyExecutionDomain::kProvider: return "provider";
        case pathguard::PolicyExecutionDomain::kCompleteVfs: return "complete_vfs";
        case pathguard::PolicyExecutionDomain::kEvent: return "event";
    }
    return "unknown";
}

static void AppendEscapedPatternLiteral(std::string_view value,
                                        std::string* output) {
    static constexpr char kHex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
        if (byte < 0x20 || byte == 0x7f) {
            output->append("\\x");
            output->push_back(kHex[byte >> 4U]);
            output->push_back(kHex[byte & 0x0fU]);
        } else {
            if (byte == '\\' || byte == '*' || byte == '?' || byte == '['
                || byte == ']' || byte == '!') {
                output->push_back('\\');
            }
            output->push_back(static_cast<char>(byte));
        }
    }
}

static std::string RenderPattern(
        const pathguard::pattern::PatternProgram& program) {
    std::string output;
    for (std::size_t component_index = 0;
         component_index < program.components.size(); ++component_index) {
        if (component_index != 0) output.push_back('/');
        const auto& component = program.components[component_index];
        if (component.globstar) {
            output.append("**");
            continue;
        }
        for (const auto& token : component.tokens) {
            switch (token.kind) {
                case pathguard::pattern::PatternTokenKind::kLiteral:
                    AppendEscapedPatternLiteral(token.literal, &output);
                    break;
                case pathguard::pattern::PatternTokenKind::kStarComponent:
                    output.push_back('*');
                    break;
                case pathguard::pattern::PatternTokenKind::kOneComponentChar:
                    output.push_back('?');
                    break;
                case pathguard::pattern::PatternTokenKind::kCharacterClass: {
                    if (token.character_class >= program.character_classes.size()) {
                        return "<invalid-character-class>";
                    }
                    const auto& character_class =
                        program.character_classes[token.character_class];
                    output.push_back('[');
                    if (character_class.negated) output.push_back('!');
                    for (unsigned value = 0; value < 128; ++value) {
                        if ((character_class.bitmap[value / 64U]
                             & (UINT64_C(1) << (value % 64U))) == 0) {
                            continue;
                        }
                        const char byte = static_cast<char>(value);
                        if (byte == '\\' || byte == ']' || byte == '-') {
                            output.push_back('\\');
                        }
                        if (value < 0x20 || value == 0x7f) {
                            static constexpr char kHex[] = "0123456789abcdef";
                            output.append("\\x");
                            output.push_back(kHex[value >> 4U]);
                            output.push_back(kHex[value & 0x0fU]);
                        } else {
                            output.push_back(byte);
                        }
                    }
                    output.push_back(']');
                    break;
                }
            }
        }
    }
    return output;
}

static int ExplainPolicy(const fs::path& policy, const std::string& package) {
    std::string raw;
    if (!Read(policy, &raw)) {
        std::cerr << "cannot read policy.bin\n";
        return 1;
    }
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    pathguard::PolicyV6 document;
    const pathguard::PolicyV6DecodeResult decoded =
        pathguard::DecodePolicyV6(bytes, &document);
    if (!decoded.ok) {
        std::cerr << "invalid policy.bin: " << decoded.error << '\n';
        return 1;
    }
    for (const pathguard::PolicyPackageV6& app : document.packages) {
        if (app.package != package) continue;
        std::uint64_t capability_union = 0;
        std::uint64_t operation_union = 0;
        for (const auto& action : app.actions) {
            capability_union |= action.required_capabilities;
            operation_union |= action.required_operations;
        }
        std::cout << "package=" << app.package
                  << "\ncontent_generation=" << decoded.content_generation
                  << "\nplan_generation=" << app.plan_generation
                  << "\nprovider_intent=" << (app.provider_enabled ? 1 : 0)
                  << "\nrequired_capabilities_union="
                  << capability_union
                  << "\nrequired_operations_union="
                  << operation_union << '\n';
        for (const pathguard::PolicyActionV6& action : app.actions) {
            if (action.selector_index >= app.selectors.size()) {
                std::cerr << "invalid selector reference\n";
                return 1;
            }
            const auto& selector = app.selectors[action.selector_index];
            std::cout << "action=" << ActionName(action.kind)
                      << " domain=" << DomainName(action.domain)
                      << " rule_id=" << action.rule_id
                      << " selector=" << action.selector_index
                      << " root=" << selector.root;
            if (selector.match_kind == pathguard::PolicyMatchKind::kGlob) {
                std::cout << " glob=" << RenderPattern(selector.base_pattern);
            }
            if (!action.target.empty()) std::cout << " target=" << action.target;
            std::cout << " priority=" << action.priority
                      << " required_capabilities=" << action.required_capabilities
                      << " required_operations=" << action.required_operations
                      << " options=" << action.options << '\n';
        }
        return 0;
    }
    std::cerr << "package not found\n";
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: pathguardctl validate <rules.toml> --host|--device [--json]\n"
                     "       pathguardctl compile <rules.toml> <output-policy.bin>\n"
                     "       pathguardctl lint <rules.toml>\n"
                     "       pathguardctl plan <old-rules.toml> <new-rules.toml>\n"
                     "       pathguardctl explain --path <rules.toml> <package> <path>\n"
                     "       pathguardctl reload <module-dir>\n"
                     "       pathguardctl explain <policy.bin> <package>\n"
                     "       pathguardctl status <module-dir> [pid]\n";
        return 2;
    }
    const std::string command = argv[1];
    if (command == "lint") {
        if (argc != 3) { std::cerr << "missing rules.toml\n"; return 2; }
        return LintRulesFile(argv[2]);
    }
    if (command == "plan") {
        if (argc != 4) {
            std::cerr << "missing old or new rules.toml\n";
            return 2;
        }
        return PlanRulesFiles(argv[2], argv[3]);
    }
    if (command == "explain" && argc >= 3
        && std::string_view(argv[2]) == "--path") {
        if (argc != 6) {
            std::cerr << "missing rules.toml, package, or path\n";
            return 2;
        }
        return ExplainRulesPath(argv[3], argv[4], argv[5]);
    }
    if (command == "status") {
        if (argc < 3) { std::cerr << "missing module directory\n"; return 2; }
        return PrintStatus(argv[2], argc >= 4 ? argv[3] : nullptr);
    }
    if (command == "explain") {
        if (argc < 4) { std::cerr << "missing policy.bin or package\n"; return 2; }
        return ExplainPolicy(argv[2], argv[3]);
    }
    if (command == "reload") {
        if (argc < 3) { std::cerr << "missing module directory\n"; return 2; }
        const fs::path source = fs::path(argv[2]) / "config" / "rules.toml";
        std::error_code error;
        fs::last_write_time(source, fs::file_time_type::clock::now(), error);
        if (error) {
            std::cerr << "cannot request reload: " << error.message() << '\n';
            return 1;
        }
        std::cout << "reload requested\n";
        return 0;
    }
    if (command == "validate" || command == "compile") {
        return ValidateOrCompile(command, argc, argv);
    }
    std::cerr << "unknown command\n";
    return 2;
}
