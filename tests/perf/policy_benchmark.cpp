#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/policy_index.h"
#include "pathguard/policy.h"
#include "pathguard/validation.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Sample {
    std::uint64_t parse_ns = 0;
    std::uint64_t validate_ns = 0;
    std::uint64_t encode_ns = 0;
    std::uint64_t lookup_ns = 0;
    std::uint64_t total_ns = 0;
};

std::uint64_t ElapsedNs(Clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - start).count());
}

std::string BuildPolicy(std::size_t package_count, std::size_t rules_per_package,
                        bool redirect_rules) {
    std::string text = "schema = 2\nfailure = open\n\n";
    text.reserve(text.size() + package_count * (96 + rules_per_package * 56));
    for (std::size_t package = 0; package < package_count; ++package) {
        text += "[org.pathguard.benchmark.app" + std::to_string(package) + "]\n";
        text += "users = 0\nprocesses = *\n";
        for (std::size_t rule = 0; rule < rules_per_package; ++rule) {
            if (redirect_rules) {
                text += "redirect PathGuardBenchmark/source/";
                text += std::to_string(package);
                text += "/";
                text += std::to_string(rule);
                text += " -> PathGuardBenchmark/target/";
                text += std::to_string(package);
                text += "/";
                text += std::to_string(rule);
                text += "\n";
            } else {
                text += "deny PathGuardBenchmark/";
                text += std::to_string(package);
                text += "/";
                text += std::to_string(rule);
                text += "\n";
            }
        }
        text += "\n";
    }
    return text;
}

std::uint64_t Percentile(std::vector<std::uint64_t> values, std::size_t percentile) {
    std::sort(values.begin(), values.end());
    const std::size_t index = (values.size() - 1) * percentile / 100;
    return values[index];
}

std::uint64_t Maximum(const std::vector<std::uint64_t>& values) {
    return *std::max_element(values.begin(), values.end());
}

void PrintStats(std::string_view axis, std::size_t size, std::size_t iterations,
                const std::vector<Sample>& samples) {
    std::vector<std::uint64_t> parse;
    std::vector<std::uint64_t> validate;
    std::vector<std::uint64_t> encode;
    std::vector<std::uint64_t> lookup;
    std::vector<std::uint64_t> total;
    parse.reserve(samples.size());
    validate.reserve(samples.size());
    encode.reserve(samples.size());
    lookup.reserve(samples.size());
    total.reserve(samples.size());
    for (const Sample& sample : samples) {
        parse.push_back(sample.parse_ns);
        validate.push_back(sample.validate_ns);
        encode.push_back(sample.encode_ns);
        lookup.push_back(sample.lookup_ns);
        total.push_back(sample.total_ns);
    }
    const auto us = [](std::uint64_t ns) { return ns / 1000ULL; };
    std::cout << axis << ',' << size << ',' << iterations << ','
              << us(Percentile(parse, 50)) << ',' << us(Percentile(parse, 95)) << ','
              << us(Percentile(parse, 99)) << ',' << us(Maximum(parse)) << ','
              << us(Percentile(validate, 50)) << ',' << us(Percentile(validate, 95)) << ','
              << us(Percentile(validate, 99)) << ',' << us(Maximum(validate)) << ','
              << us(Percentile(encode, 50)) << ',' << us(Percentile(encode, 95)) << ','
              << us(Percentile(encode, 99)) << ',' << us(Maximum(encode)) << ','
              << Percentile(lookup, 50) << ',' << Percentile(lookup, 95) << ','
              << Percentile(lookup, 99) << ',' << Maximum(lookup) << ','
              << us(Percentile(total, 50)) << ',' << us(Percentile(total, 95)) << ','
              << us(Percentile(total, 99)) << ',' << us(Maximum(total)) << '\n';
}

bool RunCase(std::string_view axis, std::size_t size, std::size_t package_count,
              std::size_t rules_per_package, std::size_t iterations,
              bool redirect_rules = false) {
    const std::string text = BuildPolicy(package_count, rules_per_package, redirect_rules);
    std::vector<Sample> samples;
    samples.reserve(iterations);
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        pathguard::PolicyDocument document;
        pathguard::ParseError error;
        Sample sample;
        const auto total_started = Clock::now();
        const auto parse_started = Clock::now();
        if (!pathguard::ParseRulesIni(text, &document, &error)) {
            std::cerr << "parse failed: line=" << error.line << " message=" << error.message << '\n';
            return false;
        }
        sample.parse_ns = ElapsedNs(parse_started);

        const auto validate_started = Clock::now();
        for (auto& app : document.apps) {
            if (!pathguard::ValidatePolicy(&app, &error)) {
                std::cerr << "validation failed: line=" << error.line
                          << " message=" << error.message << '\n';
                return false;
            }
        }
        sample.validate_ns = ElapsedNs(validate_started);

        const auto encode_started = Clock::now();
        std::vector<std::uint8_t> bytes;
        if (!pathguard::EncodePolicy(document, &bytes, &error)) {
            std::cerr << "encode failed: " << error.message << '\n';
            return false;
        }
        sample.encode_ns = ElapsedNs(encode_started);
        const std::string lookup_package = "org.pathguard.benchmark.app"
            + std::to_string(package_count - 1);
        const pathguard::binary_format::PolicyIndexView index{
            bytes.data(),
            bytes.size(),
            pathguard::binary_format::ReadLe32(
                bytes.data() + pathguard::binary_format::kPackageCountOffset),
            pathguard::binary_format::ReadLe32(
                bytes.data() + pathguard::binary_format::kPackageTableOffset),
            pathguard::binary_format::ReadLe32(
                bytes.data() + pathguard::binary_format::kMountRuleTableOffset),
            pathguard::binary_format::ReadLe32(
                bytes.data() + pathguard::binary_format::kStringTableOffset),
        };
        const auto lookup_started = Clock::now();
        if (pathguard::binary_format::FindPackageEntry(
                index, lookup_package.data(), lookup_package.size()) == nullptr) {
            std::cerr << "package lookup failed: " << lookup_package << '\n';
            return false;
        }
        sample.lookup_ns = ElapsedNs(lookup_started);
        sample.total_ns = ElapsedNs(total_started);
        samples.push_back(sample);
    }
    PrintStats(axis, size, iterations, samples);
    return true;
}

std::size_t IterationsForPackages(std::size_t package_count) {
    if (package_count <= 100) return 100;
    if (package_count <= 1000) return 30;
    return 10;
}

}  // namespace

int main() {
    std::cout << "axis,size,iterations,parse_p50_us,parse_p95_us,parse_p99_us,parse_max_us,"
                 "validate_p50_us,validate_p95_us,validate_p99_us,validate_max_us,"
                 "encode_p50_us,encode_p95_us,encode_p99_us,encode_max_us,"
                 "lookup_p50_ns,lookup_p95_ns,lookup_p99_ns,lookup_max_ns,"
                 "total_p50_us,total_p95_us,total_p99_us,total_max_us\n";

    for (const std::size_t package_count : {1U, 10U, 100U, 1000U, 10000U}) {
        if (!RunCase("packages", package_count, package_count, 1,
                     IterationsForPackages(package_count))) {
            return 1;
        }
    }
    for (const std::size_t rule_count : {0U, 1U, 4U, 16U, 32U, 64U}) {
        if (!RunCase("rules", rule_count, 1, rule_count, 100)) return 1;
    }
    return 0;
}
