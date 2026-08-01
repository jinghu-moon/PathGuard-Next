#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/pattern_limits.h"
#include "pathguard/pattern_runtime.h"
#include "pattern_corpus.h"

namespace {
std::atomic<std::uint64_t> g_allocation_count{0};
}

void* operator new(std::size_t size) {
    g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size)) return pointer;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size)) return pointer;
    throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::string_view kSchemaVersion = "pathguard.pattern-benchmark.v2";
constexpr std::size_t kWarmupIterations = 128;
constexpr std::size_t kFloodPatternCount = 1000;
constexpr std::uint64_t kRelativeNoiseTolerance = 8;
constexpr std::uint64_t kBypassNoiseTolerance = 16;

enum class ScenarioSetup : std::uint8_t { kNoRules, kScopeMiss, kCandidates };

struct Scenario {
    std::string_view name;
    ScenarioSetup setup;
    std::size_t candidate_count;
    std::size_t except_count;
};

struct Measurement {
    std::uint64_t total_ns = 0;
    std::uint64_t average_ns = 0;
    std::uint64_t p50_ns = 0;
    std::uint64_t p95_ns = 0;
    std::uint64_t p99_ns = 0;
    std::uint64_t max_ns = 0;
    std::uint64_t matcher_calls = 0;
    std::uint64_t allocations = 0;
};

struct FloodGate {
    bool rejected = false;
    std::string reason;
    std::uint64_t elapsed_ns = 0;
};

struct Fixture {
    pathguard::pattern::PatternPlan plan;
    std::optional<pathguard::pattern::CandidateIndex> index;
};

constexpr std::array<Scenario, 8> kScenarios{{
    {"no_rules", ScenarioSetup::kNoRules, 0, 0},
    {"scope_miss", ScenarioSetup::kScopeMiss, 1, 0},
    {"zero_candidate", ScenarioSetup::kCandidates, 0, 0},
    {"one_candidate", ScenarioSetup::kCandidates, 1, 0},
    {"one_candidate_except_2", ScenarioSetup::kCandidates, 1, 2},
    {"multi_candidate", ScenarioSetup::kCandidates, 8, 0},
    {"multi_candidate_except_8", ScenarioSetup::kCandidates, 8, 8},
    {"max_bucket", ScenarioSetup::kCandidates, 64, 0},
}};

constexpr std::string_view CompilerName() {
#if defined(_MSC_VER)
    return "msvc";
#elif defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#else
    return "unknown";
#endif
}

constexpr std::string_view ArchitectureName() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#else
    return "unknown";
#endif
}

pathguard::pattern::PatternPlan BuildPlan(std::size_t candidate_count,
                                          std::size_t except_count,
                                          bool include_scope) {
    using namespace pathguard::pattern;
    PatternPlan plan;
    if (include_scope) {
        plan.packages.push_back({0, "com.example.benchmark"});
        plan.scopes.push_back({{10001, 0}, 0, true});
    }
    for (SelectorId id = 0; id < candidate_count; ++id) {
        const PatternCompileResult compiled = CompilePattern("**/*.jpg");
        if (!compiled.ok()) throw std::runtime_error("benchmark pattern compile failed");
        PlanSelector selector;
        selector.id = id;
        selector.root = "Pictures";
        selector.base = *compiled.program;
        for (std::size_t except_index = 0;
             except_index < except_count; ++except_index) {
            const PatternCompileResult except = CompilePattern(
                "**/private-" + std::to_string(except_index) + "/**");
            if (!except.ok()) {
                throw std::runtime_error("benchmark except compile failed");
            }
            selector.except.push_back(*except.program);
        }
        selector.object_type = ObjectType::kFile;
        selector.specificity = selector.base.specificity;
        selector.fixed_extension = "jpg";
        plan.selectors.push_back(std::move(selector));
        PlanAction action;
        action.id = id;
        action.rule_id = id + 1;
        action.package_id = 0;
        action.selector_id = id;
        action.kind = RuntimeActionKind::kRedirect;
        action.domain = ExecutionDomain::kAppPath;
        action.target = "Download/benchmark";
        plan.actions.push_back(std::move(action));
    }
    return plan;
}

Fixture BuildFixture(const Scenario& scenario) {
    Fixture fixture;
    fixture.plan = BuildPlan(
        scenario.candidate_count, scenario.except_count,
        scenario.setup != ScenarioSetup::kNoRules);
    std::string error;
    fixture.index = pathguard::pattern::CandidateIndex::Build(fixture.plan, &error);
    if (!fixture.index.has_value()) {
        throw std::runtime_error("benchmark index build failed: " + error);
    }
    return fixture;
}

std::uint64_t Percentile(const std::vector<std::uint64_t>& sorted,
                         std::size_t numerator) {
    const std::size_t index = ((sorted.size() - 1) * numerator + 99) / 100;
    return sorted[index];
}

Measurement RunScenario(const Scenario& scenario, std::size_t iterations) {
    using namespace pathguard::pattern;
    Fixture fixture = BuildFixture(scenario);
    PatternEngine engine(fixture.plan, *fixture.index);
    RuntimeMatchScratch scratch;
    const PathOperand operand{ObjectType::kFile, "Pictures", "Trip/image.jpg"};
    const IdentityKey identity{
        scenario.setup == ScenarioSetup::kScopeMiss ? 10002 : 10001, 0};
    const OperationContext context{identity, 0,
        AttributionKind::kVerifiedPackage, PathOperation::kOpen,
        std::span<const PathOperand>(&operand, 1)};
    std::vector<std::uint64_t> samples;
    samples.reserve(iterations);
    std::uint64_t digest = 0;
    for (std::size_t iteration = 0; iteration < kWarmupIterations; ++iteration) {
        digest ^= engine.MatchOperand(context, 0, &scratch).matches.size() + iteration;
    }
    const std::uint64_t matcher_before = engine.matcher_invocations();
    const std::uint64_t allocations_before =
        g_allocation_count.load(std::memory_order_relaxed);
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto started = Clock::now();
        const MatchSet matches = engine.MatchOperand(context, 0, &scratch);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - started).count();
        samples.push_back(static_cast<std::uint64_t>(elapsed));
        digest ^= matches.matches.size() + iteration;
    }
    const std::uint64_t allocations_after =
        g_allocation_count.load(std::memory_order_relaxed);
    const std::uint64_t matcher_after = engine.matcher_invocations();
    std::sort(samples.begin(), samples.end());
    Measurement result;
    for (const std::uint64_t sample : samples) result.total_ns += sample;
    result.average_ns = result.total_ns / iterations;
    result.p50_ns = Percentile(samples, 50);
    result.p95_ns = Percentile(samples, 95);
    result.p99_ns = Percentile(samples, 99);
    result.max_ns = samples.back();
    result.matcher_calls = matcher_after - matcher_before;
    result.allocations = allocations_after - allocations_before;
    if (digest == UINT64_C(0xffffffffffffffff)) std::cerr << digest;
    return result;
}

std::vector<Measurement> MeasureAll(std::size_t iterations) {
    std::vector<Measurement> output;
    output.reserve(kScenarios.size());
    for (const Scenario& scenario : kScenarios) {
        output.push_back(RunScenario(scenario, iterations));
    }
    return output;
}

FloodGate RunFloodGate() {
    using namespace pathguard::pattern;
    PatternPlan plan = BuildPlan(kFloodPatternCount, 0, true);
    std::string error;
    const auto started = Clock::now();
    const auto index = CandidateIndex::Build(plan, &error);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - started).count();
    return {
        !index.has_value(),
        std::move(error),
        static_cast<std::uint64_t>(elapsed),
    };
}

std::uint64_t RelativeBudget(const Scenario& scenario,
                             std::uint64_t reference_ns) {
    if (scenario.setup != ScenarioSetup::kCandidates
        || scenario.candidate_count == 0) {
        return reference_ns * kBypassNoiseTolerance;
    }
    const std::uint64_t work = scenario.candidate_count
        * (scenario.except_count + 1);
    return reference_ns * work * kRelativeNoiseTolerance;
}

void WriteJsonl(std::size_t iterations, std::uint64_t random_seed,
                const std::vector<Measurement>& measurements,
                const FloodGate& flood) {
    const std::uint64_t reference = std::max<std::uint64_t>(
        1, measurements[3].p99_ns);
    for (std::size_t index = 0; index < kScenarios.size(); ++index) {
        const Scenario& scenario = kScenarios[index];
        const Measurement& measured = measurements[index];
        std::cout << "{\"schema_version\":\"" << kSchemaVersion
                  << "\",\"environment\":{\"build\":\"release\",\"compiler\":\""
                  << CompilerName() << "\",\"architecture\":\""
                  << ArchitectureName() << "\"},\"scenario\":\""
                  << scenario.name << "\",\"candidate_count\":"
                  << scenario.candidate_count << ",\"except_count\":"
                  << scenario.except_count << ",\"iterations\":" << iterations
                  << ",\"warmup_iterations\":" << kWarmupIterations
                  << ",\"random_seed\":" << random_seed
                  << ",\"transition_budget\":"
                  << pathguard::pattern::kPatternLimitsProfileV1.matcher_transition_budget
                  << ",\"total_ns\":" << measured.total_ns
                  << ",\"average_ns\":" << measured.average_ns
                  << ",\"p50_ns\":" << measured.p50_ns
                  << ",\"p95_ns\":" << measured.p95_ns
                  << ",\"p99_ns\":" << measured.p99_ns
                  << ",\"max_ns\":" << measured.max_ns
                  << ",\"matcher_calls\":" << measured.matcher_calls
                  << ",\"allocations\":" << measured.allocations
                  << ",\"relative_reference\":\"one_candidate.p99_ns\""
                  << ",\"relative_budget_ns\":"
                  << RelativeBudget(scenario, reference)
                  << ",\"gate_result\":\"measured\"}\n";
    }
    std::cout << "{\"schema_version\":\"" << kSchemaVersion
              << "\",\"environment\":{\"build\":\"release\",\"compiler\":\""
              << CompilerName() << "\",\"architecture\":\""
              << ArchitectureName() << "\"},\"scenario\":\"reject_1000_patterns\""
              << ",\"input_pattern_count\":" << kFloodPatternCount
              << ",\"elapsed_ns\":" << flood.elapsed_ns
              << ",\"matcher_calls\":0,\"allocations\":0"
              << ",\"gate_result\":\""
              << (flood.rejected ? "rejected" : "accepted")
              << "\",\"gate_reason\":\"" << flood.reason << "\"}\n";
}

void WriteTsv(std::size_t iterations, std::uint64_t random_seed,
              const std::vector<Measurement>& measurements,
              const FloodGate& flood) {
    const std::uint64_t reference = std::max<std::uint64_t>(
        1, measurements[3].p99_ns);
    std::cout << "schema_version\tbuild\tcompiler\tarchitecture\tscenario\t"
                 "candidate_count\texcept_count\titerations\twarmup_iterations\t"
                 "random_seed\ttransition_budget\ttotal_ns\taverage_ns\tp50_ns\t"
                 "p95_ns\tp99_ns\tmax_ns\tmatcher_calls\tallocations\t"
                 "relative_budget_ns\tgate_result\tgate_reason\n";
    for (std::size_t index = 0; index < kScenarios.size(); ++index) {
        const Scenario& scenario = kScenarios[index];
        const Measurement& measured = measurements[index];
        std::cout << kSchemaVersion << "\trelease\t" << CompilerName() << '\t'
                  << ArchitectureName() << '\t' << scenario.name << '\t'
                  << scenario.candidate_count << '\t' << scenario.except_count
                  << '\t' << iterations << '\t' << kWarmupIterations << '\t'
                  << random_seed << '\t'
                  << pathguard::pattern::kPatternLimitsProfileV1.matcher_transition_budget
                  << '\t' << measured.total_ns << '\t' << measured.average_ns
                  << '\t' << measured.p50_ns << '\t' << measured.p95_ns
                  << '\t' << measured.p99_ns << '\t' << measured.max_ns
                  << '\t' << measured.matcher_calls << '\t' << measured.allocations
                  << '\t' << RelativeBudget(scenario, reference)
                  << "\tmeasured\t\n";
    }
    std::cout << kSchemaVersion << "\trelease\t" << CompilerName() << '\t'
              << ArchitectureName() << "\treject_1000_patterns\t"
              << kFloodPatternCount << "\t0\t0\t0\t" << random_seed << '\t'
              << pathguard::pattern::kPatternLimitsProfileV1.matcher_transition_budget
              << "\t" << flood.elapsed_ns << "\t0\t0\t0\t0\t0\t0\t0\t0\t"
              << (flood.rejected ? "rejected" : "accepted") << '\t'
              << flood.reason << '\n';
}

bool WithinBudgets(std::size_t iterations,
                   const std::vector<Measurement>& measurements,
                   const FloodGate& flood) {
    const std::uint64_t reference = std::max<std::uint64_t>(
        1, measurements[3].p99_ns);
    for (std::size_t index = 0; index < kScenarios.size(); ++index) {
        const Scenario& scenario = kScenarios[index];
        const Measurement& measured = measurements[index];
        const std::uint64_t expected_calls =
            scenario.setup == ScenarioSetup::kCandidates
            ? iterations * scenario.candidate_count * (scenario.except_count + 1)
            : 0;
        const std::uint64_t relative_budget = RelativeBudget(scenario, reference);
        if (measured.p99_ns > relative_budget
            || measured.matcher_calls != expected_calls
            || measured.allocations != 0) {
            std::cerr << "performance gate failed: " << scenario.name
                      << " p99_ns=" << measured.p99_ns
                      << " relative_budget_ns=" << relative_budget
                      << " matcher_calls=" << measured.matcher_calls
                      << " expected_calls=" << expected_calls
                      << " allocations=" << measured.allocations << '\n';
            return false;
        }
    }
    if (!flood.rejected || flood.reason != "candidate/bucket limit") {
        std::cerr << "1000-pattern gate failed: rejected=" << flood.rejected
                  << " reason=" << flood.reason << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(NDEBUG)
    std::cerr << "Pattern benchmark requires a Release build\n";
    return 2;
#endif
    constexpr std::size_t kIterations = 1000;
    const auto corpus = pathguard::pattern::test::LoadPatternCorpus(
        PATHGUARD_SOURCE_DIR);
    const std::string_view format = argc > 1 ? argv[1] : "--format=jsonl";
    const std::vector<Measurement> measurements = MeasureAll(kIterations);
    const FloodGate flood = RunFloodGate();
    if (format == "--format=jsonl") {
        WriteJsonl(kIterations, corpus.random_seed, measurements, flood);
        return WithinBudgets(kIterations, measurements, flood) ? 0 : 1;
    }
    if (format == "--format=tsv") {
        WriteTsv(kIterations, corpus.random_seed, measurements, flood);
        return WithinBudgets(kIterations, measurements, flood) ? 0 : 1;
    }
    std::cerr << "usage: pathguard_pattern_benchmark "
                 "[--format=jsonl|--format=tsv]\n";
    return 2;
}
