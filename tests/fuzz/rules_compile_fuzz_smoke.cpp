#include <cstdint>
#include <random>
#include <string>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

int main() {
    using namespace pathguard::rules;
    std::mt19937_64 random(UINT64_C(0x524638434f4d5049));
    std::string input =
        "format = 2\n[apps.\"com.example.app\"]\nredirect_rules=[{select={root=\"\",glob=\"A\"},to=\"B\"}]\n";
    for (int iteration = 0; iteration < 2048; ++iteration) {
        std::string mutated = input;
        const std::size_t changes = 1 + random() % 8;
        for (std::size_t change = 0; change < changes; ++change) {
            const std::size_t at = random() % (mutated.size() + 1);
            if (at == mutated.size()) {
                mutated.push_back(static_cast<char>(random()));
            } else {
                mutated[at] = static_cast<char>(random());
            }
        }
        Diagnostic source_error;
        auto source = SourceBuffer::Create("fuzz.toml", std::move(mutated),
                                           RulesLimits{}, &source_error);
        if (!source.has_value()) continue;
        const RulesBuildResult result = CompileRules(*source, RulesLimits{});
        if (result.ok()) {
            assert(result.canonical_v2.has_value());
            assert(result.policy_v6.has_value());
            assert(result.blob.has_value());
            assert(VerifyPolicyBlob(*result.policy_v6, *result.blob));
        } else {
            assert(!result.blob.has_value());
        }
    }
    return 0;
}
