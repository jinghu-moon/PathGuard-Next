#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/policy_index.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t ElapsedNs(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start).count());
}

std::string BuildRules(std::size_t package_count,
                       std::size_t rules_per_package) {
    std::string text = "format = 1\n";
    for (std::size_t package = 0; package < package_count; ++package) {
        text += "[apps.\"org.pathguard.benchmark.app"
            + std::to_string(package) + "\"]\nredirect = [\n";
        for (std::size_t rule = 0; rule < rules_per_package; ++rule) {
            text += "\"Source/" + std::to_string(rule) + "\" -> \"Target/"
                + std::to_string(rule) + "\",\n";
        }
        text += "]\n";
    }
    return text;
}

std::uint64_t Percentile(std::vector<std::uint64_t> values,
                         std::size_t percentile) {
    std::sort(values.begin(), values.end());
    return values[(values.size() - 1) * percentile / 100];
}

bool RunCase(std::size_t package_count, std::size_t rules_per_package,
             std::size_t iterations) {
    using namespace pathguard::rules;
    const std::string text = BuildRules(package_count, rules_per_package);
    std::vector<std::uint64_t> compile_samples;
    std::vector<std::uint64_t> lookup_samples;
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        Diagnostic source_error;
        auto source = SourceBuffer::Create(
            "benchmark.toml", std::string(text), RulesLimits{}, &source_error);
        if (!source.has_value()) return false;
        const auto compile_started = Clock::now();
        const RulesBuildResult result = CompileRules(*source, RulesLimits{});
        compile_samples.push_back(ElapsedNs(compile_started));
        if (!result.ok()) return false;
        const std::vector<std::uint8_t>& bytes = result.blob->bytes;
        const pathguard::binary_format::PolicyIndexView index{
            bytes.data(), bytes.size(),
            pathguard::binary_format::ReadLe32(
                bytes.data() + pathguard::binary_format::kPackageCountOffset),
            pathguard::binary_format::ReadLe32(
                bytes.data() + pathguard::binary_format::kPackageTableOffset),
            pathguard::binary_format::ReadLe32(
                bytes.data() + pathguard::binary_format::kMountRuleTableOffset),
            pathguard::binary_format::ReadLe32(
                bytes.data() + pathguard::binary_format::kStringTableOffset)};
        const std::string package = "org.pathguard.benchmark.app"
            + std::to_string(package_count - 1);
        const auto lookup_started = Clock::now();
        if (pathguard::binary_format::FindPackageEntry(
                index, package.data(), package.size()) == nullptr) {
            return false;
        }
        lookup_samples.push_back(ElapsedNs(lookup_started));
    }
    std::cout << package_count << ',' << rules_per_package << ',' << text.size()
              << ',' << Percentile(compile_samples, 50) / 1000
              << ',' << Percentile(compile_samples, 95) / 1000
              << ',' << Percentile(lookup_samples, 95) << '\n';
    return true;
}

}  // namespace

int main() {
    std::cout << "packages,rules_per_package,source_bytes,compile_p50_us,"
                 "compile_p95_us,lookup_p95_ns\n";
    return RunCase(1, 16, 20) && RunCase(16, 16, 10) ? 0 : 1;
}
