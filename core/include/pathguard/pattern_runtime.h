#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/pattern.h"
#include "pathguard/action_admission.h"

namespace pathguard::pattern {

using SelectorId = std::uint32_t;
using ActionId = std::uint32_t;
using PackageId = std::uint32_t;
using RuntimeRuleId = std::uint64_t;

inline constexpr PackageId kUnknownPackageId = UINT32_MAX;

enum class ObjectType : std::uint8_t {
    kFile,
    kDirectory,
    kAny,
};

enum class RuntimeActionKind : std::uint8_t {
    kDeny,
    kRedirect,
    kObserve,
    kExport,
};

enum class ExecutionDomain : std::uint8_t {
    kMount,
    kAppPath,
    kProvider,
    kCompleteVfs,
    kEvent,
};

enum class AttributionKind : std::uint8_t {
    kUnknown,
    kVerifiedPackage,
};

enum class PathOperation : std::uint8_t {
    kOpen,
    kCreate,
    kRename,
    kLink,
    kUnlink,
    kQuery,
    kInsert,
};

struct IdentityKey {
    std::int32_t caller_uid = -1;
    std::uint32_t user_id = 0;

    bool operator==(const IdentityKey&) const = default;
};

struct PlanSelector {
    SelectorId id = 0;
    std::string root;
    PatternProgram base;
    std::vector<PatternProgram> except;
    ObjectType object_type = ObjectType::kFile;
    std::uint16_t specificity = 0;
    std::string first_literal_component;
    std::string fixed_extension;
};

struct PlanAction {
    ActionId id = 0;
    RuntimeRuleId rule_id = 0;
    PackageId package_id = 0;
    SelectorId selector_id = 0;
    RuntimeActionKind kind = RuntimeActionKind::kDeny;
    ExecutionDomain domain = ExecutionDomain::kAppPath;
    std::string target;
    std::int32_t priority = 0;
    std::uint32_t options = 0;
    bool active = true;
    CapabilityBits required_capabilities = 0;
    OperationMask required_operations = 0;
    ActionAdmission admission;
};

struct PlanPackage {
    PackageId id = 0;
    std::string name;
};

struct ScopeGrant {
    IdentityKey identity;
    PackageId package_id = 0;
    bool requires_package_attribution = true;
};

struct PatternPlan {
    std::uint64_t plan_generation = 0;
    std::uint64_t capability_generation = 0;
    std::vector<PlanPackage> packages;
    std::vector<PlanSelector> selectors;
    std::vector<PlanAction> actions;
    std::vector<ScopeGrant> scopes;
};

struct CandidateRef {
    SelectorId selector_id = 0;
    std::vector<ActionId> action_ids;
};

struct CandidateLookup {
    std::array<std::span<const CandidateRef>, 6> spans{};
    std::size_t span_count = 0;

    bool empty() const { return span_count == 0; }
};

class CandidateIndex {
public:
    static std::optional<CandidateIndex> Build(
        const PatternPlan& plan, std::string* error,
        const PatternLimitsProfile& limits = kPatternLimitsProfileV1);

    CandidateLookup Lookup(const IdentityKey& identity,
                           AttributionKind attribution,
                           PackageId package_id,
                           std::string_view root,
                           std::string_view relative_path) const noexcept;

private:
    struct Impl;
    explicit CandidateIndex(std::shared_ptr<const Impl> impl)
        : impl_(std::move(impl)) {}

    std::shared_ptr<const Impl> impl_;
};

class MatcherSnapshot {
public:
    static std::unique_ptr<MatcherSnapshot> Build(
        PatternPlan plan, std::string* error,
        const PatternLimitsProfile& limits = kPatternLimitsProfileV1);

    const PatternPlan& plan() const { return plan_; }
    const CandidateIndex& index() const { return index_; }
    std::size_t estimated_bytes() const { return estimated_bytes_; }

private:
    MatcherSnapshot(PatternPlan plan, CandidateIndex index,
                    std::size_t estimated_bytes)
        : plan_(std::move(plan)), index_(std::move(index)),
          estimated_bytes_(estimated_bytes) {}

    PatternPlan plan_;
    CandidateIndex index_;
    std::size_t estimated_bytes_ = 0;
};

struct PathOperand {
    ObjectType object_type = ObjectType::kFile;
    std::string_view root;
    std::string_view relative_path;
};

struct OperationContext {
    IdentityKey identity;
    PackageId subject_package = kUnknownPackageId;
    AttributionKind attribution = AttributionKind::kUnknown;
    PathOperation operation = PathOperation::kOpen;
    std::span<const PathOperand> operands;
};

enum class DecisionReason : std::uint16_t {
    kMatched,
    kNoMatch,
    kDenied,
    kCollision,
    kAmbiguousReverse,
    kCapabilityMissing,
    kRuntimeUnavailable,
    kBudgetExceeded,
    kInvalidPathEncoding,
    kUnsafeTarget,
};

enum class PrimaryDisposition : std::uint8_t {
    kPass,
    kDeny,
    kRedirect,
};

enum EffectMask : std::uint8_t {
    kEffectNone = 0,
    kEffectObserve = 1U << 0U,
    kEffectExport = 1U << 1U,
};

struct MatchedSelector {
    SelectorId selector_id = 0;
    std::uint16_t specificity = 0;
    const ActionId* action_ids = nullptr;
    std::uint16_t action_count = 0;
};

struct MatchSet {
    std::span<const MatchedSelector> matches;
    DecisionReason reason = DecisionReason::kNoMatch;
};

struct RuntimeMatchScratch {
    PatternMatchScratch pattern;
    std::array<MatchedSelector, 64> matched{};
};

class PatternEngine {
public:
    PatternEngine(const PatternPlan& plan, const CandidateIndex& index)
        : plan_(plan), index_(index) {}

    MatchSet MatchOperand(const OperationContext& context,
                          std::size_t operand_index,
                          RuntimeMatchScratch* scratch) const noexcept;

    std::uint64_t matcher_invocations() const noexcept {
        return matcher_invocations_.load(std::memory_order_relaxed);
    }

private:
    const PatternPlan& plan_;
    const CandidateIndex& index_;
    mutable std::atomic<std::uint64_t> matcher_invocations_{0};
};

struct Decision {
    PrimaryDisposition primary = PrimaryDisposition::kPass;
    std::uint8_t effects = kEffectNone;
    DecisionReason reason = DecisionReason::kNoMatch;
    RuntimeRuleId rule_id = 0;
    SelectorId selector_id = 0;
    RuntimeRuleId conflict_rule_id = 0;
    std::string_view target;
    ExecutionDomain domain = ExecutionDomain::kAppPath;
    std::uint64_t plan_generation = 0;
    std::uint64_t capability_generation = 0;
};

class ActionEvaluator {
public:
    explicit ActionEvaluator(const PatternPlan& plan) : plan_(plan) {}

    Decision EvaluateOperand(const MatchSet& matches) const noexcept;
    Decision Evaluate(const OperationContext& context,
                      std::span<const MatchSet> operand_matches) const noexcept;

private:
    const PatternPlan& plan_;
};

struct PlannedOperand {
    PrimaryDisposition disposition = PrimaryDisposition::kPass;
    std::array<char, 4096> target{};
    std::uint16_t target_size = 0;
    RuntimeRuleId rule_id = 0;
    SelectorId selector_id = 0;

    std::string_view target_path() const {
        return {target.data(), target_size};
    }
};

struct OperationPlan {
    bool accepted = false;
    int error_number = 0;
    DecisionReason reason = DecisionReason::kNoMatch;
    std::uint8_t operand_count = 0;
    std::uint8_t effects = kEffectNone;
    std::uint64_t plan_generation = 0;
    std::array<PlannedOperand, 2> operands{};
};

OperationPlan BuildOperationPlan(const OperationContext& context,
                                 std::span<const MatchSet> matches,
                                 const ActionEvaluator& evaluator) noexcept;

void AdmitPatternPlan(PatternPlan* plan,
                      const CapabilitySnapshot& snapshot) noexcept;

}  // namespace pathguard::pattern
