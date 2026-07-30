#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "pathguard/policy_format.h"
#include "pathguard/policy_v6.h"
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
        "format = 2\n"
        "[compatibility]\n"
        "allow_legacy_mount = true\n"
        "[apps.\"org.localsend.localsend_app\"]\n"
        "users = [0]\n"
        "provider = { enabled = true }\n"
        "redirect_rules = [{ select = { root = \"Download\", "
        "glob = \"localsend-source\", type = \"directory\" }, "
        "to = \"Download/localsend-redirect\" }]\n");
    pathguard::PolicyV6 decoded;
    const auto decoded_result = valid.blob
        ? pathguard::DecodePolicyV6(valid.blob->bytes, &decoded)
        : pathguard::PolicyV6DecodeResult{};
    if (!valid.ok() || !decoded_result.ok
        || decoded_result.content_generation != valid.blob->content_generation
        || valid.blob->bytes.size() < pathguard::binary_format::kHeaderSize) {
        return 1;
    }
    std::cout << "valid|" << valid.blob->content_generation << '|'
              << valid.requirements.mount_actions << '|'
              << (valid.requirements.provider ? 1 : 0) << '|';
    PrintHex(valid.blob->bytes);
    std::cout << '\n';

    const RulesBuildResult mixed = Compile(
        "format = 2\n"
        "[apps.\"org.localsend.localsend_app\"]\n"
        "users = [0]\n"
        "deny_rules = ["
        "{ select = { root = \"Pictures\", glob = \"Nagram\", type = \"directory\" } },"
        "{ select = { root = \"DCIM\", glob = \"Screenshots\", type = \"directory\" } }]\n"
        "redirect_rules = [{ select = { root = \"Download\", "
        "glob = \"localsend-source\", type = \"directory\" }, "
        "to = \"Download/localsend-redirect\" }]\n");
    if (!mixed.ok()
        || mixed.requirements.mount_actions
            != (pathguard::kMountActionRedirect
                | pathguard::kMountActionDenyAnchor)) {
        return 3;
    }
    std::cout << "mixed|" << mixed.blob->content_generation << '|'
              << mixed.requirements.mount_actions << '|';
    PrintHex(mixed.blob->bytes);
    std::cout << '\n';

    const RulesBuildResult invalid = Compile(
        "format = 2\n[apps.\"com.example.app\"]\n"
        "redirect_rules = [{ select = { root = \"Pictures\", "
        "glob = \"*.jpg\" } }]\n");
    if (invalid.ok() || invalid.diagnostics.empty()) return 2;
    const Diagnostic& diagnostic = invalid.diagnostics.front();
    std::cout << "invalid|" << diagnostic.code << '|'
              << diagnostic.primary.begin << '|' << diagnostic.primary.end
              << '\n';
    return 0;
}
