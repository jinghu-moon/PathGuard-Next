#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy.h"
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
        std::cout << "valid: " << result.canonical->apps.size()
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

static const char* MountActionName(pathguard::MountAction action) {
    switch (action) {
        case pathguard::MountAction::kDeny: return "deny";
        case pathguard::MountAction::kRedirect: return "redirect";
        case pathguard::MountAction::kRestore: return "allow";
        case pathguard::MountAction::kIsolateRoot: return "isolate";
    }
    return "unknown";
}

static int ExplainPolicy(const fs::path& policy, const std::string& package) {
    std::string raw;
    if (!Read(policy, &raw)) {
        std::cerr << "cannot read policy.bin\n";
        return 1;
    }
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    pathguard::PolicyDocument document;
    pathguard::ParseError error;
    std::uint64_t generation = 0;
    if (!pathguard::DecodePolicy(bytes, &document, &generation, &error)) {
        std::cerr << "invalid policy.bin: " << error.message << '\n';
        return 1;
    }
    for (const pathguard::AppPolicy& app : document.apps) {
        if (app.package != package) continue;
        std::cout << "package=" << app.package
                  << "\nsnapshot_generation=" << generation
                  << "\nplan_generation="
                  << pathguard::ComputePlanGeneration(
                         app, document.failure_mode,
                         document.allow_legacy_string_bind)
                  << "\nmedia=" << static_cast<unsigned>(app.media_compat)
                  << "\nprovider=" << static_cast<unsigned>(app.provider_compat)
                  << "\n";
        for (const pathguard::LogicalMountRule& rule : app.mounts) {
            std::cout << MountActionName(rule.action) << " visible="
                      << rule.visible_path;
            if (!rule.backing_path.empty()) std::cout << " backing=" << rule.backing_path;
            std::cout << '\n';
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
