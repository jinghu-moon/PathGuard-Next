#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/compiler.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules_control.h"

namespace {

using Clock = std::chrono::steady_clock;
using pathguard::rules::CompileStatistics;

struct Sample {
    std::uint64_t total_ns = 0;
    std::uint64_t admission_ns = 0;
    CompileStatistics stages;
    std::size_t blob_bytes = 0;
};

std::uint64_t ElapsedNs(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start).count());
}

std::uint64_t PeakMemoryBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                                sizeof(counters))
        ? static_cast<std::uint64_t>(counters.PeakWorkingSetSize) : 0;
#else
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0
        ? static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U : 0;
#endif
}

std::string BuildRules(std::size_t rule_count, bool arrows) {
    std::string text = "format = 2\n[apps.\"org.pathguard.benchmark\"]\n";
    if (!arrows) return text + "enabled = false\n";
    text += "redirect_rules = [\n";
    for (std::size_t rule = 0; rule < rule_count; ++rule) {
        text += "{select={root=\"Source/" + std::to_string(rule)
            + "\",glob=\"item\"},to=\"Target/" + std::to_string(rule)
            + "\"},\n";
    }
    return text + "]\n";
}

std::uint64_t Percentile(std::vector<std::uint64_t> values,
                         std::size_t percentile) {
    std::sort(values.begin(), values.end());
    return values[(values.size() - 1) * percentile / 100];
}

std::uint64_t FieldP95(const std::vector<Sample>& samples,
                       std::uint64_t CompileStatistics::*field) {
    std::vector<std::uint64_t> values;
    values.reserve(samples.size());
    for (const Sample& sample : samples) values.push_back(sample.stages.*field);
    return Percentile(std::move(values), 95);
}

bool RunCase(std::string_view name, std::size_t rule_count,
             std::size_t iterations, std::uint64_t budget_ns, bool arrows) {
    using namespace pathguard::rules;
    const std::string text = BuildRules(rule_count, arrows);
    std::vector<Sample> samples;
    samples.reserve(iterations);
    for (std::size_t iteration = 0; iteration <= iterations; ++iteration) {
        Diagnostic source_error;
        auto source = SourceBuffer::Create(
            "benchmark.toml", std::string(text), RulesLimits{}, &source_error);
        if (!source.has_value()) return false;
        const auto compile_started = Clock::now();
        RulesBuildResult result;
        if (!arrows) {
            RulesV2ParseResult parsed = ParseRulesDocumentV2(*source, RulesLimits{});
            const std::uint64_t total_ns = ElapsedNs(compile_started);
            if (!parsed.ok()) return false;
            if (iteration != 0) {
                samples.push_back({total_ns, 0, {}, 0});
            }
            continue;
        }
        result = CompileRules(*source, RulesLimits{});
        const std::uint64_t total_ns = ElapsedNs(compile_started);
        if (!result.ok()) return false;
        DeviceSnapshot snapshot;
        snapshot.mount.primitives = pathguard::kCapabilityOpenTreeMoveMount;
        snapshot.mount.strict_actions = pathguard::kMountActionRedirect;
        snapshot.provider_supported = true;
        snapshot.topology_supported = true;
        const auto admission_started = Clock::now();
        if (!AdmitPolicy(*result.policy_v6, result.requirements,
                         snapshot).admitted) return false;
        const std::uint64_t admission_ns = ElapsedNs(admission_started);
        if (iteration != 0) {
            samples.push_back({total_ns, admission_ns, result.statistics,
                               result.blob->bytes.size()});
        }
    }
    std::vector<std::uint64_t> totals;
    std::vector<std::uint64_t> admissions;
    for (const Sample& sample : samples) {
        totals.push_back(sample.total_ns);
        admissions.push_back(sample.admission_ns);
    }
    const std::uint64_t p95 = Percentile(totals, 95);
    const Sample& first = samples.front();
    std::cout << "{\"case\":\"" << name
              << "\",\"build\":\"release\",\"backend\":\"cpp20\""
                 ",\"parser\":\"toml++-3.4.0-toml-1.0\""
              << ",\"source_bytes\":" << text.size()
              << ",\"generated_bytes\":" << first.stages.generated_bytes
              << ",\"arrow_count\":" << first.stages.arrow_count
              << ",\"rewrite_count\":" << first.stages.rewrite_count
              << ",\"blob_bytes\":" << first.blob_bytes
              << ",\"peak_memory_bytes\":" << PeakMemoryBytes()
              << ",\"total_p95_ns\":" << p95
              << ",\"format_probe_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::format_probe_ns)
              << ",\"lex_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::lex_ns)
              << ",\"rewrite_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::rewrite_ns)
              << ",\"parse_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::parse_ns)
              << ",\"scope_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::scope_ns)
              << ",\"decode_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::decode_ns)
              << ",\"normalize_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::normalize_ns)
              << ",\"conflict_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::conflict_ns)
              << ",\"canonicalize_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::canonicalize_ns)
              << ",\"admission_p95_ns\":" << Percentile(admissions, 95)
              << ",\"encode_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::encode_ns)
              << ",\"verify_p95_ns\":"
              << FieldP95(samples, &CompileStatistics::verify_ns)
              << "}\n";
    return p95 < budget_ns;
}

bool RunPublishCase() {
    namespace fs = std::filesystem;
    using namespace pathguard::rules;
    Diagnostic error;
    auto source = SourceBuffer::Create("benchmark.toml", BuildRules(16, true),
                                       RulesLimits{}, &error);
    if (!source.has_value()) return false;
    RulesBuildResult first = CompileRules(*source, RulesLimits{});
    auto changed_source = SourceBuffer::Create(
        "benchmark.toml", BuildRules(17, true), RulesLimits{}, &error);
    if (!first.ok() || !changed_source.has_value()) return false;
    RulesBuildResult second = CompileRules(*changed_source, RulesLimits{});
    if (!second.ok()) return false;
    const fs::path root = fs::temp_directory_path()
        / ("pathguard-rf8-benchmark-" + std::to_string(
            Clock::now().time_since_epoch().count()));
    const pathguard::control::Publisher publisher(root);
    const auto first_started = Clock::now();
    const auto first_publish = publisher.Publish(*first.blob);
    const std::uint64_t first_ns = ElapsedNs(first_started);
    const auto second_started = Clock::now();
    const auto second_publish = publisher.Publish(*second.blob);
    const std::uint64_t second_ns = ElapsedNs(second_started);
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    std::cout << "{\"case\":\"publish\",\"publish_first_ns\":"
              << first_ns << ",\"publish_replace_ns\":" << second_ns
              << ",\"includes_fsync\":true}\n";
    return first_publish.published && second_publish.published;
}

}  // namespace

int main() {
#if !defined(NDEBUG)
    std::cerr << "benchmark requires a release build\n";
    return 2;
#endif
    const bool no_arrow = RunCase("no-arrow", 0, 20, UINT64_C(10000000), false);
    const bool typical = RunCase("typical", 64, 20, UINT64_C(10000000), true);
    const bool large = RunCase("large", 128, 7, UINT64_C(50000000), true);
    const bool extreme = RunCase("extreme", 256, 3, UINT64_C(100000000), true);
    return no_arrow && typical && large && extreme && RunPublishCase() ? 0 : 1;
}
