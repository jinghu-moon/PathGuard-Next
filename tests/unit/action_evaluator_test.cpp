#include <array>

#include "pathguard/pattern_runtime.h"
#include "pattern_runtime_test_support.h"

int main() {
    using namespace pathguard::pattern;
    using namespace pathguard::pattern::test;
    PatternPlan plan;
    plan.plan_generation = 7;
    plan.capability_generation = 9;
    plan.actions = {
        {0, 40, 0, 0, RuntimeActionKind::kRedirect,
         ExecutionDomain::kAppPath, "Download/low", 1, true},
        {1, 30, 0, 1, RuntimeActionKind::kRedirect,
         ExecutionDomain::kAppPath, "Download/high", 10, true},
        {2, 20, 0, 2, RuntimeActionKind::kDeny,
         ExecutionDomain::kProvider, {}, -100, true},
        {3, 50, 0, 0, RuntimeActionKind::kObserve,
         ExecutionDomain::kEvent, {}, 0, true},
        {4, 60, 0, 0, RuntimeActionKind::kExport,
         ExecutionDomain::kEvent, {}, 0, true},
        {5, 10, 0, 3, RuntimeActionKind::kRedirect,
         ExecutionDomain::kAppPath, "Download/tie", 10, true},
    };
    const ActionEvaluator evaluator(plan);

    MatchSet empty{{}, DecisionReason::kNoMatch};
    Decision pass = evaluator.EvaluateOperand(empty);
    assert(pass.primary == PrimaryDisposition::kPass);
    assert(pass.reason == DecisionReason::kNoMatch);

    const CandidateRef one_ref{1, {1}};
    const MatchedSelector one_match = Matched(one_ref, 20);
    const MatchSet one{std::span<const MatchedSelector>(&one_match, 1),
                       DecisionReason::kMatched};
    Decision redirected = evaluator.EvaluateOperand(one);
    assert(redirected.primary == PrimaryDisposition::kRedirect);
    assert(redirected.rule_id == 30);
    assert(redirected.plan_generation == 7);
    assert(redirected.capability_generation == 9);

    const CandidateRef effects_ref{0, {0, 3, 4}};
    const CandidateRef tie_ref{3, {5}};
    const std::array<MatchedSelector, 2> multi_matches{
        Matched(effects_ref, 20), Matched(tie_ref, 20)};
    const MatchSet multi{multi_matches, DecisionReason::kMatched};
    Decision tie = evaluator.EvaluateOperand(multi);
    assert(tie.rule_id == 10);  // RuleId is the final deterministic tie-break.
    assert((tie.effects & kEffectObserve) != 0);
    assert((tie.effects & kEffectExport) != 0);

    const CandidateRef deny_ref{2, {2}};
    const MatchedSelector deny_match = Matched(deny_ref, 1);
    const std::array<MatchedSelector, 2> deny_matches{one_match, deny_match};
    const MatchSet denied_set{deny_matches, DecisionReason::kMatched};
    Decision denied = evaluator.EvaluateOperand(denied_set);
    assert(denied.primary == PrimaryDisposition::kDeny);
    assert(denied.reason == DecisionReason::kDenied);
    assert(denied.rule_id == 20);
    return 0;
}
