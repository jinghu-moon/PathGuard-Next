#pragma once

#include <string_view>

#include "pathguard/pattern_runtime.h"
#include "test_assert.h"

namespace pathguard::pattern::test {

inline PatternProgram Program(std::string_view pattern) {
    auto compiled = CompilePattern(pattern);
    assert(compiled.ok());
    return std::move(*compiled.program);
}

inline PlanSelector Selector(SelectorId id, std::string root,
                             std::string_view pattern,
                             std::uint16_t specificity = 10) {
    PlanSelector selector;
    selector.id = id;
    selector.root = std::move(root);
    selector.base = Program(pattern);
    selector.object_type = ObjectType::kFile;
    selector.specificity = specificity;
    return selector;
}

inline MatchedSelector Matched(const CandidateRef& candidate,
                               std::uint16_t specificity) {
    return {candidate.selector_id, specificity, candidate.action_ids.data(),
            static_cast<std::uint16_t>(candidate.action_ids.size())};
}

}  // namespace pathguard::pattern::test
