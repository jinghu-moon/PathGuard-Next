#include <array>
#include <cerrno>

#include "pathguard/pattern_runtime.h"
#include "pattern_runtime_test_support.h"

int main() {
    using namespace pathguard::pattern;
    using namespace pathguard::pattern::test;
    PatternPlan plan;
    plan.plan_generation = 77;
    plan.actions = {
        {0, 1, 0, 0, RuntimeActionKind::kDeny,
         ExecutionDomain::kProvider, {}, 0, true},
        {1, 2, 0, 1, RuntimeActionKind::kRedirect,
         ExecutionDomain::kProvider, "Download/out", 0, true},
        {2, 3, 0, 2, RuntimeActionKind::kRedirect,
         ExecutionDomain::kProvider, "Download/out", 0, true},
        {3, 4, 0, 3, RuntimeActionKind::kRedirect,
         ExecutionDomain::kAppPath, "Download/other", 0, true},
    };
    const ActionEvaluator evaluator(plan);
    const CandidateRef deny_ref{0, {0}};
    const CandidateRef first_ref{1, {1}};
    const CandidateRef second_ref{2, {2}};
    const CandidateRef cross_ref{3, {3}};
    const MatchedSelector deny_match = Matched(deny_ref, 10);
    const MatchedSelector first_match = Matched(first_ref, 10);
    const MatchedSelector second_match = Matched(second_ref, 10);
    const MatchedSelector cross_match = Matched(cross_ref, 10);
    const MatchSet deny_set{std::span<const MatchedSelector>(&deny_match, 1),
                            DecisionReason::kMatched};
    const MatchSet first_set{std::span<const MatchedSelector>(&first_match, 1),
                             DecisionReason::kMatched};
    const MatchSet second_set{std::span<const MatchedSelector>(&second_match, 1),
                              DecisionReason::kMatched};
    const MatchSet cross_set{std::span<const MatchedSelector>(&cross_match, 1),
                             DecisionReason::kMatched};

    const std::array<PathOperand, 2> same_operands{{
        {ObjectType::kFile, "Pictures", "same.jpg"},
        {ObjectType::kFile, "Pictures", "same.jpg"},
    }};
    OperationContext context{{10001, 0}, 0,
        AttributionKind::kVerifiedPackage, PathOperation::kRename,
        same_operands};

    OperationPlan denied = BuildOperationPlan(
        context, std::array<MatchSet, 2>{deny_set, second_set}, evaluator);
    assert(!denied.accepted && denied.error_number == EACCES);
    assert(denied.reason == DecisionReason::kDenied);

    OperationPlan collision = BuildOperationPlan(
        context, std::array<MatchSet, 2>{first_set, second_set}, evaluator);
    assert(!collision.accepted && collision.error_number == EEXIST);
    assert(collision.reason == DecisionReason::kCollision);

    OperationPlan cross_domain = BuildOperationPlan(
        context, std::array<MatchSet, 2>{first_set, cross_set}, evaluator);
    assert(!cross_domain.accepted && cross_domain.error_number == EXDEV);
    assert(cross_domain.reason == DecisionReason::kRuntimeUnavailable);

    const std::array<PathOperand, 2> distinct_operands{{
        {ObjectType::kFile, "Pictures", "source.jpg"},
        {ObjectType::kFile, "Pictures", "target.jpg"},
    }};
    context.operands = distinct_operands;
    OperationPlan accepted = BuildOperationPlan(
        context, std::array<MatchSet, 2>{first_set, second_set}, evaluator);
    assert(accepted.accepted);
    assert(accepted.operand_count == 2);
    assert(accepted.operands[0].target_path()
           == "Download/out/source.jpg");
    assert(accepted.operands[1].target_path()
           == "Download/out/target.jpg");
    assert(accepted.plan_generation == 77);
    return 0;
}
