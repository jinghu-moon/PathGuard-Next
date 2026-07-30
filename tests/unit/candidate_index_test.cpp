#include <string>

#include "pathguard/pattern_runtime.h"
#include "pattern_runtime_test_support.h"

int main() {
    using namespace pathguard::pattern;
    using namespace pathguard::pattern::test;

    PatternPlan plan;
    plan.packages.push_back({0, "com.example.app"});
    plan.selectors.push_back(Selector(0, "Pictures", "Album"));
    plan.selectors.back().first_literal_component = "Album";
    plan.selectors.push_back(Selector(1, "Pictures", "**/*.jpg"));
    plan.selectors.back().fixed_extension = "jpg";
    plan.selectors.push_back(Selector(2, "Pictures", "*"));
    for (ActionId id = 0; id < 3; ++id) {
        plan.actions.push_back({id, id + 1, 0, id,
            RuntimeActionKind::kRedirect, ExecutionDomain::kAppPath,
            "Download/out", 0, true});
    }
    plan.scopes.push_back({{10001, 0}, 0, true});
    std::string error;
    auto index = CandidateIndex::Build(plan, &error);
    assert(index.has_value());
    PatternEngine engine(plan, *index);
    RuntimeMatchScratch scratch;

    const PathOperand album{ObjectType::kFile, "Pictures", "Album"};
    OperationContext wrong_uid{{10002, 0}, 0,
        AttributionKind::kVerifiedPackage, PathOperation::kOpen,
        std::span<const PathOperand>(&album, 1)};
    MatchSet miss = engine.MatchOperand(wrong_uid, 0, &scratch);
    assert(miss.matches.empty());
    assert(engine.matcher_invocations() == 0);

    OperationContext correct{{10001, 0}, 0,
        AttributionKind::kVerifiedPackage, PathOperation::kOpen,
        std::span<const PathOperand>(&album, 1)};
    MatchSet literal = engine.MatchOperand(correct, 0, &scratch);
    assert(literal.matches.size() == 2);  // literal bucket + general bucket
    assert(engine.matcher_invocations() == 2);

    const PathOperand image{ObjectType::kFile, "Pictures", "Trip/a.jpg"};
    correct.operands = std::span<const PathOperand>(&image, 1);
    const std::uint64_t before_extension = engine.matcher_invocations();
    MatchSet extension = engine.MatchOperand(correct, 0, &scratch);
    assert(extension.matches.size() == 1);
    assert(engine.matcher_invocations() == before_extension + 2);

    PatternPlan over_limit;
    over_limit.packages = plan.packages;
    over_limit.scopes = plan.scopes;
    for (SelectorId id = 0; id < 17; ++id) {
        over_limit.selectors.push_back(Selector(id, "Pictures", "*"));
        over_limit.actions.push_back({id, id + 1, 0, id,
            RuntimeActionKind::kRedirect, ExecutionDomain::kAppPath,
            "Download/out", 0, true});
    }
    assert(!CandidateIndex::Build(over_limit, &error).has_value());
    assert(error == "degenerate pattern/root limit");
    return 0;
}
