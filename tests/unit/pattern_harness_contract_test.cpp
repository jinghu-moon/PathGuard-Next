#include <cassert>
#include <cstdint>

#include "pathguard/pattern_limits.h"
#include "pattern_corpus.h"
#include "pattern_harness_common.h"

int main() {
    using namespace pathguard::pattern;
    using namespace pathguard::pattern::test;
    static_assert(kPatternLimitsProfileV1.max_pattern_tokens == 64);
    static_assert(kPatternLimitsProfileV1.matcher_transition_budget == 4096);

    const PatternCorpus corpus = LoadPatternCorpus(PATHGUARD_SOURCE_DIR);
    assert(corpus.random_seed != 0);
    assert(corpus.entries.size() == 2);
    for (const PatternCorpusEntry& entry : corpus.entries) {
        assert(!entry.bytes.empty());
        assert(entry.sha256.size() == 64);
        if (entry.target == "tokenizer") {
            (void)ConsumeTokenizerInput(entry.bytes);
        } else {
            assert(entry.target == "matcher");
            (void)ConsumeMatcherInput(entry.bytes);
        }
    }
    return 0;
}
