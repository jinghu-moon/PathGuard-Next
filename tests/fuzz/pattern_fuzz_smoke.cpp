#include <array>
#include <cstdint>
#include <span>

#include "pattern_corpus.h"
#include "pattern_harness_common.h"
#include "pathguard/pattern_limits.h"

int main() {
    using namespace pathguard::pattern::test;
    const std::span<const std::uint8_t> empty;
    const std::array<std::uint8_t, 4> short_input{'*', '?', '/', 'a'};
    const PatternCorpus corpus = LoadPatternCorpus(PATHGUARD_SOURCE_DIR);

    const auto tokenizer_empty = ConsumeTokenizerInput(empty);
    const auto matcher_empty = ConsumeMatcherInput(empty);
    const auto tokenizer_short = ConsumeTokenizerInput(short_input);
    const auto matcher_short = ConsumeMatcherInput(short_input);

    std::uint64_t corpus_digest = corpus.random_seed;
    for (const PatternCorpusEntry& entry : corpus.entries) {
        corpus_digest ^= entry.target == "tokenizer"
            ? ConsumeTokenizerInput(entry.bytes)
            : ConsumeMatcherInput(entry.bytes);
    }
    return tokenizer_empty != tokenizer_short && matcher_empty != matcher_short
        && corpus_digest != 0 ? 0 : 1;
}
