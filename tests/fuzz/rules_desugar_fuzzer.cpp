#include <cstddef>
#include <cstdint>

#include "pathguard/rules/desugarer.h"
#include "rules_fuzzer_common.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    using namespace pathguard::rules;
    RulesLimits limits;
    if (size > limits.max_source_bytes) return 0;
    auto source = fuzzer::MakeSource(data, size, limits);
    if (!source) return 0;
    const DesugarResult result = DesugarRulesSource(*source, limits);
    if (!result.ok()) {
        if (result.diagnostics.empty() || result.generated_storage.has_value()
            || !result.redirects.empty()) {
            fuzzer::FailInvariant();
        }
        return 0;
    }
    if (result.rewrite_map.query_count() != 0) fuzzer::FailInvariant();
    Diagnostic generated_error;
    auto generated = SourceBuffer::Create(
        "generated.toml", std::string(result.parser_input(*source)), limits,
        &generated_error);
    if (!generated
        || !ScanArrowCandidates(*generated, limits).candidates.empty()) {
        fuzzer::FailInvariant();
    }
    for (const GeneratedRedirect& redirect : result.redirects) {
        if (!source->IsValidSpan(redirect.original_rule)
            || !source->IsValidSpan(redirect.original_source)
            || !source->IsValidSpan(redirect.original_arrow)
            || !source->IsValidSpan(redirect.original_target)
            || !generated->IsValidSpan(redirect.generated_table)
            || !generated->IsValidSpan(redirect.generated_source)
            || !generated->IsValidSpan(redirect.generated_target)) {
            fuzzer::FailInvariant();
        }
    }
    return 0;
}
