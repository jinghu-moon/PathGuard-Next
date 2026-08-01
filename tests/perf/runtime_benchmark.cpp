#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <fstream>
#endif

#include "pathguard/pattern_runtime.h"
#include "pathguard/policy_action_router.h"
#include "pathguard/policy_v6_view.h"
#include "pathguard/route_provenance.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "pathguard/snapshot_publisher.h"

namespace {

std::atomic<std::uint64_t> g_allocations{0};

}  // namespace

void* operator new(std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size)) return pointer;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size)) return pointer;
    throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::string_view kSchema = "pathguard.runtime-benchmark.v1";
constexpr std::size_t kIterations = 1000;
constexpr std::size_t kWarmup = 128;

struct Measurement {
    std::uint64_t p50_ns = 0;
    std::uint64_t p95_ns = 0;
    std::uint64_t p99_ns = 0;
    std::uint64_t max_ns = 0;
    std::uint64_t allocations = 0;
};

constexpr std::string_view CompilerName() {
#if defined(__clang__)
    return "clang";
#elif defined(_MSC_VER)
    return "msvc";
#elif defined(__GNUC__)
    return "gcc";
#else
    return "unknown";
#endif
}

constexpr std::string_view ArchitectureName() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

constexpr std::string_view PlatformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__ANDROID__)
    return "android";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}

std::uint64_t CurrentRssBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                                sizeof(counters))
        ? static_cast<std::uint64_t>(counters.WorkingSetSize) : 0;
#else
    std::ifstream statm("/proc/self/statm");
    std::uint64_t pages = 0;
    std::uint64_t resident = 0;
    if (!(statm >> pages >> resident)) return 0;
    return resident * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#endif
}

template <typename Operation>
Measurement Measure(Operation operation, std::size_t iterations = kIterations) {
    for (std::size_t index = 0; index < kWarmup; ++index) operation(index);
    std::vector<std::uint64_t> samples;
    samples.reserve(iterations);
    const std::uint64_t allocations_before =
        g_allocations.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < iterations; ++index) {
        const auto started = Clock::now();
        operation(index + kWarmup);
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - started).count()));
    }
    const std::uint64_t allocations_after =
        g_allocations.load(std::memory_order_relaxed);
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](std::size_t value) {
        return samples[(samples.size() - 1) * value / 100];
    };
    return {
        percentile(50), percentile(95), percentile(99), samples.back(),
        allocations_after - allocations_before,
    };
}

struct ProviderFixture {
    std::vector<std::uint8_t> bytes;
    pathguard::policy_v6_view::PolicyV6View policy;
    pathguard::policy_v6_view::PackageRef package;
    pathguard::CapabilitySnapshot capabilities;
};

ProviderFixture BuildProviderFixture() {
    using namespace pathguard::rules;
    Diagnostic error;
    auto source = SourceBuffer::Create("runtime-benchmark.toml", R"(format = 2
[apps."org.localsend.localsend_app"]
users = [0]
provider = { enabled = true }
redirect_rules = [
  { select = { root = "Download/localsend-source", glob = "**", type = "file" }, to = "Download/localsend-redirect", enforcement = "provider" },
]
)", RulesLimits{}, &error);
    if (!source.has_value()) throw std::runtime_error("provider fixture source");
    RulesBuildResult built = CompileRules(*source, RulesLimits{});
    if (!built.ok()) throw std::runtime_error("provider fixture compile");
    ProviderFixture fixture;
    fixture.bytes = built.blob->bytes;
    if (!fixture.policy.Initialize(fixture.bytes.data(), fixture.bytes.size())
        || !fixture.policy.FindPackage(
            "org.localsend.localsend_app",
            sizeof("org.localsend.localsend_app") - 1, &fixture.package)) {
        throw std::runtime_error("provider fixture view");
    }
    fixture.capabilities.capability_generation = 1;
    fixture.capabilities.plan_generation = fixture.package.plan_generation;
    fixture.capabilities.observed_capabilities =
        pathguard::kCapabilityProviderCallerUid
        | pathguard::kCapabilityProviderQueryInsertMapping;
    fixture.capabilities.domains[static_cast<unsigned>(
        pathguard::AdmissionDomain::kProvider)] = {
        pathguard::AdapterState::kActive,
        pathguard::kProviderCompositeOperationsV1, 0};
    return fixture;
}

pathguard::provenance::ObjectIdentity Identity(std::uint64_t value) {
    pathguard::provenance::ObjectIdentity identity;
    identity.volume = "primary";
    identity.handle.resize(8);
    for (unsigned byte = 0; byte < 8; ++byte) {
        identity.handle[byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
    return identity;
}

pathguard::provenance::RouteRecord Record(std::uint64_t value) {
    pathguard::provenance::RouteRecord record;
    record.key = {"primary", "Download/redirect/item-" + std::to_string(value)};
    record.scope = {10358, 0, 7, "org.localsend.localsend_app"};
    record.identity = Identity(value + 1);
    record.logical_source_path = "Download/source/item-" + std::to_string(value);
    record.rule_id = 41;
    record.content_generation = 2;
    record.created_plan_generation = 3;
    record.bound_plan_generation = 3;
    return record;
}

class DiscardingJournal final : public pathguard::provenance::RouteJournal {
public:
    pathguard::provenance::Error Append(
            const pathguard::provenance::JournalFrame&) override {
        return pathguard::provenance::Error::kNone;
    }
    pathguard::provenance::Error Replay(
            std::vector<pathguard::provenance::JournalFrame>* frames) override {
        if (frames == nullptr) return pathguard::provenance::Error::kCorrupt;
        frames->clear();
        return pathguard::provenance::Error::kNone;
    }
};

struct ReloadSnapshot {
    explicit ReloadSnapshot(std::uint64_t next) : generation(next) {}
    std::uint64_t generation = 0;
};

void Print(std::string_view scenario, const Measurement& measured,
           std::uint64_t budget_ns, std::uint64_t allocation_budget,
           std::uint64_t rss_bytes, std::uint64_t rss_budget_bytes) {
    std::cout << "{\"schema\":\"" << kSchema
              << "\",\"compiler\":\"" << CompilerName()
              << "\",\"architecture\":\"" << ArchitectureName()
              << "\",\"platform\":\"" << PlatformName()
              << "\",\"hardware_threads\":"
              << std::thread::hardware_concurrency()
              << ",\"scenario\":\"" << scenario
              << "\",\"p50_ns\":" << measured.p50_ns
              << ",\"p95_ns\":" << measured.p95_ns
              << ",\"p99_ns\":" << measured.p99_ns
              << ",\"max_ns\":" << measured.max_ns
              << ",\"allocations\":" << measured.allocations
              << ",\"relative_budget_ns\":" << budget_ns
              << ",\"allocation_budget\":" << allocation_budget
              << ",\"rss_bytes\":" << rss_bytes
              << ",\"rss_budget_bytes\":" << rss_budget_bytes << "}\n";
}

bool RunPerformanceGate() {
    using namespace pathguard;
    ProviderFixture provider = BuildProviderFixture();
    policy_pattern_runtime::MatchScratch scratch;
    policy_action_router::Request match_request{
        provider.package, "Download/localsend-source",
        sizeof("Download/localsend-source") - 1,
        "benchmark/test.jpg", sizeof("benchmark/test.jpg") - 1,
        1, AdmissionDomain::kProvider, kOperationOpenWrite,
    };
    policy_action_router::Request miss_request = match_request;
    miss_request.root = "Pictures";
    miss_request.root_size = sizeof("Pictures") - 1;
    std::uint64_t digest = 0;
    const Measurement provider_miss = Measure([&](std::size_t) {
        const auto result = policy_action_router::Route(
            provider.policy, miss_request, provider.capabilities, &scratch);
        digest += static_cast<std::uint8_t>(result.disposition);
    });
    const Measurement provider_match = Measure([&](std::size_t) {
        const auto result = policy_action_router::Route(
            provider.policy, match_request, provider.capabilities, &scratch);
        digest += result.rule_id;
    });

    const Measurement provenance_prepare_abort = Measure([&](std::size_t index) {
        DiscardingJournal journal;
        provenance::RouteProvenanceStore store(&journal);
        const auto record = Record(index + 1);
        const provenance::TransactionId transaction{1, index + 1};
        digest += static_cast<std::uint8_t>(
            store.Prepare(transaction, provenance::Operation::kCreate, record));
        digest += static_cast<std::uint8_t>(store.Abort(transaction));
    });
    const Measurement provenance_commit = Measure([&](std::size_t index) {
        DiscardingJournal journal;
        provenance::RouteProvenanceStore store(&journal);
        const auto record = Record(index + 1);
        const provenance::TransactionId transaction{2, index + 1};
        digest += static_cast<std::uint8_t>(
            store.Prepare(transaction, provenance::Operation::kCreate, record));
        digest += static_cast<std::uint8_t>(
            store.Materialize(transaction, record.identity));
        digest += static_cast<std::uint8_t>(store.Commit(transaction));
    });

    snapshot::Domain<ReloadSnapshot> reload(snapshot::Profile::kProvider);
    if (!reload.Publish(new ReloadSnapshot(1), sizeof(ReloadSnapshot))) return false;
    const Measurement acquire = Measure([&](std::size_t) {
        auto guard = reload.Acquire();
        if (guard) digest += guard->generation;
    });
    const Measurement publish = Measure([&](std::size_t index) {
        auto* next = new ReloadSnapshot(index + 2);
        if (!reload.Publish(next, sizeof(ReloadSnapshot))) delete next;
    });
    const auto reload_metrics = reload.metrics();

    const std::uint64_t rss_before = CurrentRssBytes();
    for (std::size_t index = 0; index < 10000; ++index) {
        const auto result = policy_action_router::Route(
            provider.policy, match_request, provider.capabilities, &scratch);
        digest += result.rule_id;
        auto* next = new ReloadSnapshot(index + kIterations + 2);
        if (!reload.Publish(next, sizeof(ReloadSnapshot))) delete next;
    }
    const std::uint64_t rss_after = CurrentRssBytes();
    const std::uint64_t rss_noise = std::max<std::uint64_t>(
        provider.bytes.size() * 256U, rss_before / 8U);
    const std::uint64_t rss_budget = rss_before + rss_noise;

    const std::uint64_t provider_budget =
        std::max<std::uint64_t>(1, provider_miss.p99_ns) * 16U;
    const std::uint64_t provenance_budget =
        std::max<std::uint64_t>(1, provenance_prepare_abort.p99_ns) * 4U;
    const std::uint64_t publish_budget =
        std::max<std::uint64_t>(1, acquire.p99_ns) * 64U;
    const std::uint64_t provenance_alloc_budget =
        // prepare/materialize/commit has three durable state transitions;
        // calibrate against the two-transition prepare/abort reference.
        std::max<std::uint64_t>(1, provenance_prepare_abort.allocations) * 3U;

    Print("provider_scope_miss", provider_miss, provider_budget, 0,
          rss_after, rss_budget);
    Print("provider_route_match", provider_match, provider_budget, 0,
          rss_after, rss_budget);
    Print("provenance_prepare_abort", provenance_prepare_abort,
          provenance_budget, provenance_alloc_budget, rss_after, rss_budget);
    Print("provenance_prepare_materialize_commit", provenance_commit,
          provenance_budget, provenance_alloc_budget, rss_after, rss_budget);
    Print("snapshot_acquire", acquire, publish_budget, 0,
          rss_after, rss_budget);
    Print("snapshot_reload", publish, publish_budget, kIterations,
          rss_after, rss_budget);

    if (digest == UINT64_MAX) std::cerr << digest;
    return provider_match.p99_ns <= provider_budget
        && provider_match.allocations == 0 && provider_miss.allocations == 0
        && provenance_commit.p99_ns <= provenance_budget
        && provenance_commit.allocations <= provenance_alloc_budget
        && publish.p99_ns <= publish_budget
        && publish.allocations <= kIterations
        && reload_metrics.snapshot_reload_rejected_retire_limit_total == 0
        && (rss_before == 0 || rss_after <= rss_budget);
}

bool RunSlotExhaustionGate() {
    using namespace pathguard::snapshot;
    Domain<ReloadSnapshot> domain(Profile::kProvider);
    if (!domain.Publish(new ReloadSnapshot(1), sizeof(ReloadSnapshot))) return false;
    std::uint64_t digest = 0;
    const Measurement available = Measure([&](std::size_t) {
        auto guard = domain.Acquire();
        if (guard) digest += guard->generation;
    }, 128);
    domain.ReleaseCurrentThread();
    std::atomic<std::size_t> ready{0};
    std::atomic<bool> release{false};
    std::atomic<bool> acquire_failed{false};
    std::vector<std::thread> threads;
    threads.reserve(domain.slot_capacity());
    for (std::size_t index = 0; index < domain.slot_capacity(); ++index) {
        threads.emplace_back([&] {
            auto guard = domain.Acquire();
            if (!guard) acquire_failed.store(true, std::memory_order_relaxed);
            ready.fetch_add(1, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        });
    }
    while (ready.load(std::memory_order_acquire) != domain.slot_capacity()) {
        std::this_thread::yield();
    }
    const Measurement exhausted = Measure([&](std::size_t) {
        auto guard = domain.Acquire();
        if (guard) acquire_failed.store(true, std::memory_order_relaxed);
    }, 128);
    const auto metrics = domain.metrics();
    release.store(true, std::memory_order_release);
    for (std::thread& thread : threads) thread.join();
    const std::uint64_t exhaustion_budget = std::max<std::uint64_t>(
        1, available.p99_ns) * domain.slot_capacity() * 2U;
    Print("snapshot_slot_available", available, exhaustion_budget, 0,
          CurrentRssBytes(), 0);
    Print("snapshot_slot_exhaustion", exhausted, exhaustion_budget, 0,
          CurrentRssBytes(), 0);
    if (digest == UINT64_MAX) std::cerr << digest;
    return !acquire_failed.load(std::memory_order_relaxed)
        && metrics.hazard_slots_in_use_high_watermark == domain.slot_capacity()
        && metrics.hazard_slot_acquire_fail_total == kWarmup + 128
        && available.allocations == 0 && exhausted.allocations == 0
        && exhausted.p99_ns <= exhaustion_budget;
}

pathguard::pattern::PatternPlan SoakPlan(std::uint64_t generation) {
    using namespace pathguard::pattern;
    PatternPlan plan;
    plan.plan_generation = generation;
    plan.packages.push_back({0, "org.localsend.localsend_app"});
    plan.scopes.push_back({{10358, 0}, 0, true});
    const PatternCompileResult compiled = CompilePattern("**");
    if (!compiled.ok()) throw std::runtime_error("soak pattern");
    PlanSelector selector;
    selector.root = "Download/localsend-source";
    selector.base = *compiled.program;
    selector.object_type = ObjectType::kFile;
    selector.specificity = selector.base.specificity;
    plan.selectors.push_back(std::move(selector));
    PlanAction action;
    action.rule_id = 41;
    action.kind = RuntimeActionKind::kRedirect;
    action.target = "Download/localsend-redirect";
    plan.actions.push_back(std::move(action));
    return plan;
}

bool RunSoak(std::uint64_t seconds) {
    using namespace pathguard::pattern;
    using Domain = pathguard::snapshot::Domain<MatcherSnapshot>;
    Domain domain(pathguard::snapshot::Profile::kProvider);
    std::string error;
    auto initial = MatcherSnapshot::Build(SoakPlan(1), &error);
    if (!initial || !domain.Publish(initial.release(), sizeof(MatcherSnapshot))) return false;
    std::atomic<bool> stop{false};
    std::atomic<bool> invalid{false};
    std::atomic<std::uint64_t> matches{0};
    std::atomic<std::uint64_t> reloads{0};
    std::vector<std::thread> readers;
    for (unsigned index = 0; index < 4; ++index) {
        readers.emplace_back([&] {
            RuntimeMatchScratch scratch;
            const PathOperand operand{
                ObjectType::kFile, "Download/localsend-source", "soak/test.jpg"};
            const OperationContext context{
                {10358, 0}, 0, AttributionKind::kVerifiedPackage,
                PathOperation::kOpen, std::span<const PathOperand>(&operand, 1)};
            while (!stop.load(std::memory_order_acquire)) {
                auto guard = domain.Acquire();
                if (!guard) continue;
                PatternEngine engine(guard->plan(), guard->index());
                const MatchSet result = engine.MatchOperand(context, 0, &scratch);
                if (result.reason != DecisionReason::kMatched
                    || result.matches.size() != 1) invalid.store(true);
                matches.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    const std::uint64_t rss_before = CurrentRssBytes();
    const auto deadline = Clock::now() + std::chrono::seconds(seconds);
    std::uint64_t generation = 2;
    while (Clock::now() < deadline) {
        auto next = MatcherSnapshot::Build(SoakPlan(generation++), &error);
        if (!next) {
            invalid.store(true);
            break;
        }
        const std::size_t bytes = next->estimated_bytes();
        if (domain.Publish(next.release(), bytes)) {
            reloads.fetch_add(1, std::memory_order_relaxed);
        } else {
            invalid.store(true);
            break;
        }
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_release);
    for (std::thread& reader : readers) reader.join();
    domain.Reclaim();
    const std::uint64_t rss_after = CurrentRssBytes();
    const auto metrics = domain.metrics();
    std::cout << "{\"schema\":\"pathguard.runtime-soak.v1\""
              << ",\"duration_seconds\":" << seconds
              << ",\"matches\":" << matches.load()
              << ",\"reloads\":" << reloads.load()
              << ",\"rss_before_bytes\":" << rss_before
              << ",\"rss_after_bytes\":" << rss_after
              << ",\"slot_high_watermark\":"
              << metrics.hazard_slots_in_use_high_watermark
              << ",\"retired_high_watermark\":"
              << metrics.retired_snapshot_count_high_watermark
              << ",\"reload_rejected\":"
              << metrics.snapshot_reload_rejected_retire_limit_total
              << ",\"result\":\""
              << (!invalid.load() && matches.load() != 0 && reloads.load() != 0
                    ? "passed" : "failed") << "\"}\n";
    return !invalid.load() && matches.load() != 0 && reloads.load() != 0
        && metrics.hazard_slot_acquire_fail_total == 0
        && metrics.snapshot_reload_rejected_retire_limit_total == 0;
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(NDEBUG)
    std::cerr << "benchmark requires a release build\n";
    return 2;
#endif
    constexpr std::string_view prefix = "--soak-seconds=";
    if (argc == 2 && std::string_view(argv[1]).starts_with(prefix)) {
        const std::string value(std::string_view(argv[1]).substr(prefix.size()));
        char* end = nullptr;
        const unsigned long long seconds = std::strtoull(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0' || seconds == 0) return 2;
        return RunSoak(seconds) ? 0 : 1;
    }
    if (argc != 1) return 2;
    return RunPerformanceGate() && RunSlotExhaustionGate() ? 0 : 1;
}
