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

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: pathguardctl validate|compile <rules.ini> [policy.bin]\n"; return 2; }
    const std::string command = argv[1];
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
    if (!pathguard::EncodePolicy(document, 1, &bytes, &error)) { std::cerr << error.message << '\n'; return 1; }
    std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) { std::cerr << "cannot write policy.bin\n"; return 1; }
    std::cout << "compiled: " << bytes.size() << " bytes\n";
    return 0;
}
