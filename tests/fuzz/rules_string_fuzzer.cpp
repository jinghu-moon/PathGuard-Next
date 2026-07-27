#include <cstddef>
#include <cstdint>

#include "pathguard/rules/arrow_scanner.h"
#include "rules_fuzzer_common.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    using namespace pathguard::rules;
    RulesLimits limits;
    if (size > limits.max_source_bytes) return 0;
    auto source = fuzzer::MakeSource(data, size, limits);
    if (!source) return 0;
    const ArrowScanResult result = ScanArrowCandidates(*source, limits);
    fuzzer::CheckResult(*source, result, limits);
    return 0;
}
