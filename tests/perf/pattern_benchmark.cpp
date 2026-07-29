#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#include "pathguard/pattern_limits.h"
#include "pattern_corpus.h"
#include "pattern_harness_common.h"

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::string_view kSchemaVersion =
    "pathguard.pattern-benchmark.v1";

struct Scenario {
    std::string_view name;
    std::size_t candidate_count;
};

constexpr std::array<Scenario, 3> kScenarios{{
    {"zero_candidate", 0},
    {"one_candidate", 1},
    {"multi_candidate", 8},
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

std::uint64_t RunScenario(const Scenario& scenario, std::size_t iterations) {
    const std::array<std::uint8_t, 8> input{'P', 'i', 'c', 't', '/', '*', '.', 'j'};
    std::uint64_t digest = 0;
    const auto started = Clock::now();
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        digest ^= pathguard::pattern::test::ConsumeMatcherInput(input);
        digest += scenario.candidate_count;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - started).count();
    if (digest == UINT64_C(0xffffffffffffffff)) std::cerr << digest;
    return static_cast<std::uint64_t>(elapsed);
}

void WriteJsonl(std::size_t iterations, std::uint64_t random_seed) {
    for (const Scenario& scenario : kScenarios) {
        const auto elapsed = RunScenario(scenario, iterations);
        std::cout << "{\"schema_version\":\"" << kSchemaVersion
                  << "\",\"environment\":{\"build\":\"release\",\"compiler\":\""
                  << CompilerName() << "\",\"architecture\":\""
                  << ArchitectureName() << "\"},\"scenario\":\""
                  << scenario.name << "\",\"candidate_count\":"
                  << scenario.candidate_count << ",\"iterations\":"
                  << iterations << ",\"random_seed\":" << random_seed
                  << ",\"transition_budget\":"
                  << pathguard::pattern::kPatternLimitsProfileV1.matcher_transition_budget
                  << ",\"total_ns\":" << elapsed << "}\n";
    }
}

void WriteTsv(std::size_t iterations, std::uint64_t random_seed) {
    std::cout << "schema_version\tbuild\tcompiler\tarchitecture\tscenario\t"
                 "candidate_count\titerations\trandom_seed\ttransition_budget\t"
                 "total_ns\n";
    for (const Scenario& scenario : kScenarios) {
        const auto elapsed = RunScenario(scenario, iterations);
        std::cout << kSchemaVersion << "\trelease\t" << CompilerName() << '\t'
                  << ArchitectureName() << '\t' << scenario.name << '\t'
                  << scenario.candidate_count << '\t' << iterations << '\t'
                  << random_seed << '\t'
                  << pathguard::pattern::kPatternLimitsProfileV1.matcher_transition_budget
                  << '\t' << elapsed << '\n';
    }
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
        return 0;
    }
    if (format == "--format=tsv") {
        WriteTsv(kIterations, corpus.random_seed);
        return 0;
    }
    std::cerr << "usage: pathguard_pattern_benchmark "
                 "[--format=jsonl|--format=tsv]\n";
    return 2;
}
