#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy.h"
#include "pathguard/validation.h"

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
        std::cerr << "usage: pathguardctl validate|compile <rules.ini> [policy.bin]\n"
                     "       pathguardctl explain <policy.bin> <package>\n"
                     "       pathguardctl status <module-dir> [pid]\n";
        return 2;
    }
    const std::string command = argv[1];
    if (command == "status") {
        if (argc < 3) { std::cerr << "missing module directory\n"; return 2; }
        return PrintStatus(argv[2], argc >= 4 ? argv[3] : nullptr);
    }
    if (command == "explain") {
        if (argc < 4) { std::cerr << "missing policy.bin or package\n"; return 2; }
        return ExplainPolicy(argv[2], argv[3]);
    }
    if (command != "validate" && command != "compile") { std::cerr << "unknown command\n"; return 2; }
    if (argc < 3) { std::cerr << "missing rules.ini\n"; return 2; }
    std::string text;
    if (!Read(argv[2], &text)) { std::cerr << "cannot read rules.ini\n"; return 1; }
    pathguard::PolicyDocument document;
    pathguard::ParseError error;
    if (!pathguard::ParseRulesIni(text, &document, &error)) { std::cerr << "line " << error.line << ": " << error.message << '\n'; return 1; }
    for (auto& app : document.apps) if (!pathguard::ValidatePolicy(&app, &error)) { std::cerr << "line " << error.line << ": " << error.message << '\n'; return 1; }
    if (command == "validate") { std::cout << "valid: " << document.apps.size() << " package(s)\n"; return 0; }
    if (argc < 4) { std::cerr << "missing policy.bin output\n"; return 2; }
    std::vector<std::uint8_t> bytes;
    if (!pathguard::EncodePolicy(document, &bytes, &error)) { std::cerr << error.message << '\n'; return 1; }
    std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) { std::cerr << "cannot write policy.bin\n"; return 1; }
    std::cout << "compiled: " << bytes.size() << " bytes\n";
    return 0;
}
