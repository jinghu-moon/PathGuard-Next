#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

using Clock = std::chrono::steady_clock;
constexpr std::string_view kSchemaVersion =
    "pathguard.pattern-benchmark.v1";

struct Scenario {
    std::string_view name;
    std::size_t candidate_count;
    std::uint64_t budget_per_iteration_ns;
};

struct Measurement {
    std::uint64_t total_ns = 0;
    std::uint64_t average_ns = 0;
    std::uint64_t p50_ns = 0;
    std::uint64_t p95_ns = 0;
    std::uint64_t p99_ns = 0;
    std::uint64_t max_ns = 0;
    std::uint64_t matcher_calls = 0;
};

struct Fixture {
    pathguard::pattern::PatternPlan plan;
    std::optional<pathguard::pattern::CandidateIndex> index;
};

constexpr std::array<Scenario, 4> kScenarios{{
    {"zero_candidate", 0, 10000},
    {"one_candidate", 1, 200000},
    {"multi_candidate", 8, 1000000},
    {"max_bucket", 64, 5000000},
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

Fixture BuildFixture(std::size_t candidate_count) {
    using namespace pathguard::pattern;
    Fixture fixture;
    fixture.plan.packages.push_back({0, "com.example.benchmark"});
    fixture.plan.scopes.push_back({{10001, 0}, 0, true});
    for (SelectorId id = 0; id < candidate_count; ++id) {
        const PatternCompileResult compiled = CompilePattern("**/*.jpg");
        if (!compiled.ok()) throw std::runtime_error("benchmark pattern compile failed");
        PlanSelector selector;
        selector.id = id;
        selector.root = "Pictures";
        selector.base = *compiled.program;
        selector.object_type = ObjectType::kFile;
        selector.specificity = selector.base.specificity;
        selector.fixed_extension = "jpg";
        fixture.plan.selectors.push_back(std::move(selector));
        PlanAction action;
        action.id = id;
        action.rule_id = id + 1;
        action.package_id = 0;
        action.selector_id = id;
        action.kind = RuntimeActionKind::kRedirect;
        action.domain = ExecutionDomain::kAppPath;
        action.target = "Download/benchmark";
        fixture.plan.actions.push_back(std::move(action));
    }
    std::string error;
    fixture.index = CandidateIndex::Build(fixture.plan, &error);
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
    Fixture fixture = BuildFixture(scenario.candidate_count);
    PatternEngine engine(fixture.plan, *fixture.index);
    RuntimeMatchScratch scratch;
    const PathOperand operand{ObjectType::kFile, "Pictures", "Trip/image.jpg"};
    const OperationContext context{{10001, 0}, 0,
        AttributionKind::kVerifiedPackage, PathOperation::kOpen,
        std::span<const PathOperand>(&operand, 1)};
    std::vector<std::uint64_t> samples;
    samples.reserve(iterations);
    std::uint64_t digest = 0;
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto started = Clock::now();
        const MatchSet matches = engine.MatchOperand(context, 0, &scratch);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - started).count();
        samples.push_back(static_cast<std::uint64_t>(elapsed));
        digest ^= matches.matches.size() + iteration;
    }
    std::sort(samples.begin(), samples.end());
    Measurement result;
    for (const std::uint64_t sample : samples) result.total_ns += sample;
    result.average_ns = result.total_ns / iterations;
    result.p50_ns = Percentile(samples, 50);
    result.p95_ns = Percentile(samples, 95);
    result.p99_ns = Percentile(samples, 99);
    result.max_ns = samples.back();
    result.matcher_calls = engine.matcher_invocations();
    if (digest == UINT64_C(0xffffffffffffffff)) std::cerr << digest;
    return result;
}

void WriteJsonl(std::size_t iterations, std::uint64_t random_seed) {
    for (const Scenario& scenario : kScenarios) {
        const Measurement measured = RunScenario(scenario, iterations);
        std::cout << "{\"schema_version\":\"" << kSchemaVersion
                  << "\",\"environment\":{\"build\":\"release\",\"compiler\":\""
                  << CompilerName() << "\",\"architecture\":\""
                  << ArchitectureName() << "\"},\"scenario\":\""
                  << scenario.name << "\",\"candidate_count\":"
                  << scenario.candidate_count << ",\"iterations\":"
                  << iterations << ",\"random_seed\":" << random_seed
                  << ",\"transition_budget\":"
                  << pathguard::pattern::kPatternLimitsProfileV1.matcher_transition_budget
                  << ",\"total_ns\":" << measured.total_ns
                  << ",\"average_ns\":" << measured.average_ns
                  << ",\"p50_ns\":" << measured.p50_ns
                  << ",\"p95_ns\":" << measured.p95_ns
                  << ",\"p99_ns\":" << measured.p99_ns
                  << ",\"max_ns\":" << measured.max_ns
                  << ",\"matcher_calls\":" << measured.matcher_calls
                  << ",\"budget_ns\":" << scenario.budget_per_iteration_ns
                  << "}\n";
    }
}

void WriteTsv(std::size_t iterations, std::uint64_t random_seed) {
    std::cout << "schema_version\tbuild\tcompiler\tarchitecture\tscenario\t"
                 "candidate_count\titerations\trandom_seed\ttransition_budget\t"
                 "total_ns\taverage_ns\tp50_ns\tp95_ns\tp99_ns\tmax_ns\t"
                 "matcher_calls\tbudget_ns\n";
    for (const Scenario& scenario : kScenarios) {
        const Measurement measured = RunScenario(scenario, iterations);
        std::cout << kSchemaVersion << "\trelease\t" << CompilerName() << '\t'
                  << ArchitectureName() << '\t' << scenario.name << '\t'
                  << scenario.candidate_count << '\t' << iterations << '\t'
                  << random_seed << '\t'
                  << pathguard::pattern::kPatternLimitsProfileV1.matcher_transition_budget
                  << '\t' << measured.total_ns << '\t' << measured.average_ns
                  << '\t' << measured.p50_ns << '\t' << measured.p95_ns
                  << '\t' << measured.p99_ns << '\t' << measured.max_ns
                  << '\t' << measured.matcher_calls << '\t'
                  << scenario.budget_per_iteration_ns << '\n';
    }
}

bool WithinBudgets(std::size_t iterations) {
    for (const Scenario& scenario : kScenarios) {
        const Measurement measured = RunScenario(scenario, iterations);
        const std::uint64_t expected_calls = iterations * scenario.candidate_count;
        if (measured.p99_ns > scenario.budget_per_iteration_ns
            || measured.matcher_calls != expected_calls) {
            std::cerr << "performance budget exceeded: " << scenario.name
                      << " p99_ns=" << measured.p99_ns
                      << " budget_ns=" << scenario.budget_per_iteration_ns << '\n';
            return false;
        }
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
    if (format == "--format=jsonl") {
        WriteJsonl(kIterations, corpus.random_seed);
        return WithinBudgets(kIterations) ? 0 : 1;
    }
    if (format == "--format=tsv") {
        WriteTsv(kIterations, corpus.random_seed);
        return WithinBudgets(kIterations) ? 0 : 1;
    }
    std::cerr << "usage: pathguard_pattern_benchmark "
                 "[--format=jsonl|--format=tsv]\n";
    return 2;
}
