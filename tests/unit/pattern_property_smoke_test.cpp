#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include "pathguard/pattern_limits.h"
#include "pattern_corpus.h"
#include "pattern_harness_common.h"

int main() {
    using namespace pathguard::pattern;
    using namespace pathguard::pattern::test;
    const PatternCorpus corpus = LoadPatternCorpus(PATHGUARD_SOURCE_DIR);
    std::uint64_t state = corpus.random_seed;

    for (const PatternCorpusEntry& entry : corpus.entries) {
        std::vector<std::uint8_t> input = entry.bytes;
        input.resize(std::min(input.size(),
                              kPatternLimitsProfileV1.max_pattern_tokens));
        for (std::size_t iteration = 0; iteration < 128; ++iteration) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            if (!input.empty()) {
                input[state % input.size()] ^= static_cast<std::uint8_t>(state);
            }
            const auto tokenizer_first = ConsumeTokenizerInput(input);
            const auto tokenizer_second = ConsumeTokenizerInput(input);
            const auto matcher_first = ConsumeMatcherInput(input);
            const auto matcher_second = ConsumeMatcherInput(input);
            assert(tokenizer_first == tokenizer_second);
            assert(matcher_first == matcher_second);
        }
    }
    return 0;
}
