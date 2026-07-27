#include <cstdint>
#include <array>
#include <string>

#include "pathguard/rules/desugarer.h"
#include "pathguard/rules_contract.h"
#include "rules_fuzz_common.h"

int main() {
    using namespace pathguard::rules;
    using namespace pathguard::rules::fuzz_test;
    const std::string seed = ReadSeed("rules-desugar-3b6c80b98784e2b9.seed");
    RulesLimits limits;
    limits.max_source_bytes = 4096;
    limits.max_container_depth = 32;
    limits.max_tokens_or_nodes = 2048;
    std::uint64_t state = 0xc79318e25af046bdULL;
    const std::array malformed_prefixes{
        std::string("format = 1\nfoo = {\nredirect = [\"A\" -> \"B\"]\n"),
        std::string("format = 1\n[broken\nredirect = [\"A\" -> \"B\"]\n"),
        std::string("format = 1\nvalue = \"\"\"unterminated\n"
                    "redirect = [\"A\" -> \"B\"]\n"),
    };
    for (std::size_t iteration = 0;
         iteration < 4000 + malformed_prefixes.size(); ++iteration) {
        Diagnostic error;
        std::string input;
        if (iteration < malformed_prefixes.size()) {
            input = malformed_prefixes[iteration];
        } else {
            input = iteration == malformed_prefixes.size()
                ? seed : Mutate(seed, &state);
        }
        auto source = SourceBuffer::Create(
            "fuzz.toml", std::move(input), limits, &error);
        assert(source.has_value());
        const DesugarResult result = DesugarRulesSource(*source, limits);
        assert(result.ok() || !result.diagnostics.empty());
        assert(result.ok() || !result.generated_storage.has_value());
        if (result.ok()) {
            assert(result.rewrite_map.query_count() == 0);
            Diagnostic generated_error;
            auto generated = SourceBuffer::Create(
                "generated.toml", std::string(result.parser_input(*source)),
                limits, &generated_error);
            assert(generated.has_value());
            assert(ScanArrowCandidates(*generated, limits).candidates.empty());
            for (const GeneratedRedirect& redirect : result.redirects) {
                assert(source->IsValidSpan(redirect.original_rule));
                assert(source->IsValidSpan(redirect.original_source));
                assert(source->IsValidSpan(redirect.original_arrow));
                assert(source->IsValidSpan(redirect.original_target));
                assert(generated->IsValidSpan(redirect.generated_table));
                assert(generated->IsValidSpan(redirect.generated_source));
                assert(generated->IsValidSpan(redirect.generated_target));
            }
        }
    }
    return 0;
}
