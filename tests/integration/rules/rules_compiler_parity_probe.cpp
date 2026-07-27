#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"

namespace {

pathguard::rules::RulesBuildResult Compile(std::string bytes) {
    using namespace pathguard::rules;
    Diagnostic source_error;
    auto source = SourceBuffer::Create("rules.toml", std::move(bytes),
                                       RulesLimits{}, &source_error);
    if (!source.has_value()) {
        RulesBuildResult failed;
        failed.diagnostics.push_back(std::move(source_error));
        return failed;
    }
    return CompileRules(*source, RulesLimits{});
}

void PrintHex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    for (const std::uint8_t byte : bytes) {
        std::cout << digits[byte >> 4] << digits[byte & 0x0f];
    }
}

}  // namespace

int main() {
    using namespace pathguard::rules;
    const RulesBuildResult valid = Compile(
        "format = 1\n"
        "[apps.\"org.localsend.localsend_app\"]\n"
        "users = [0]\n"
        "file_picker = true\n"
        "redirect = [\"Download/localsend-source\" -> "
        "\"Download/localsend-redirect\"]\n");
    if (!valid.ok() || valid.blob->bytes.size() != 207
        || valid.blob->content_generation != UINT64_C(11078014328063549684)) {
        return 1;
    }
    std::cout << "valid|" << valid.blob->content_generation << '|'
              << valid.requirements.mount_actions << '|'
              << (valid.requirements.provider ? 1 : 0) << '|';
    PrintHex(valid.blob->bytes);
    std::cout << '\n';

    const RulesBuildResult invalid = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect = [\"A\" ->]\n");
    if (invalid.ok() || invalid.diagnostics.empty()) return 2;
    const Diagnostic& diagnostic = invalid.diagnostics.front();
    std::cout << "invalid|" << diagnostic.code << '|'
              << diagnostic.primary.begin << '|' << diagnostic.primary.end
              << '\n';
    return 0;
}
