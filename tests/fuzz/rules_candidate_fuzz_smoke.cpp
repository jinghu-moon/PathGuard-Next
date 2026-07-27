#include <cstdint>
#include <string>

#include "pathguard/rules_contract.h"
#include "rules_fuzz_common.h"

int main() {
    using namespace pathguard::rules;
    using namespace pathguard::rules::fuzz_test;
    const std::string seed = ReadSeed("rules-candidate-c69f97c7c2b378df.seed");
    RulesLimits limits;
    limits.max_source_bytes = 4096;
    limits.max_container_depth = 32;
    limits.max_tokens_or_nodes = 2048;
    limits.max_diagnostics = 8;
    std::uint64_t state = 0xb8742cd195e630afULL;
    VerifyFaultInjection(seed, limits);
    Validate(seed, limits, true);
    for (std::size_t iteration = 0; iteration < 4000; ++iteration) {
        Validate(Mutate(seed, &state), limits, true);
    }
    return 0;
}
