#include "pathguard/complete_vfs_adapter.h"
#include "pathguard/effect_adapter.h"
#include "test_assert.h"

namespace {

class Sink final : public pathguard::effects::EffectSink {
public:
    explicit Sink(bool result) : result_(result) {}
    bool Submit(const pathguard::effects::EffectEvent& event) override {
        last = event;
        ++calls;
        return result_;
    }
    pathguard::effects::EffectEvent last;
    unsigned calls = 0;
private:
    bool result_ = false;
};

class VfsBackend final : public pathguard::complete_vfs::Backend {
public:
    pathguard::OperationMask operations() const override {
        return pathguard::kOperationOpenRead | pathguard::kOperationCreate;
    }
    bool Apply(const pathguard::pattern::OperationPlan&) override {
        ++calls;
        return true;
    }
    unsigned calls = 0;
};

std::uint64_t g_observe_clock = 0;

std::uint64_t ObserveClock() noexcept { return g_observe_clock; }

}  // namespace

int main() {
    using namespace pathguard;
    Sink observed(true);
    Sink rejected(false);
    effects::EffectEvent event;
    event.device = 1;
    event.inode = 2;
    event.generation = 3;
    event.source = "Pictures/a.jpg";
    const auto dispatched = effects::Dispatch(
        pattern::kEffectObserve | pattern::kEffectExport,
        event, &observed, &rejected);
    assert(dispatched.submitted == pattern::kEffectObserve);
    assert(dispatched.failed == pattern::kEffectExport);
    assert(observed.calls == 1 && rejected.calls == 1);

    effects::BoundedEffectQueue observe_queue(1);
    const auto queued_effect = effects::Dispatch(
        pattern::kEffectObserve, event, &observe_queue, nullptr);
    assert(queued_effect.submitted == pattern::kEffectObserve);
    const auto dropped_effect = effects::Dispatch(
        pattern::kEffectObserve, event, &observe_queue, nullptr);
    assert(dropped_effect.failed == pattern::kEffectObserve);
    assert(observe_queue.pending() == 1);
    effects::EffectEvent drained;
    assert(observe_queue.Pop(&drained));
    assert(drained.source == event.source && drained.generation == 3);
    const auto effect_metrics = observe_queue.metrics();
    assert(effect_metrics.accepted == 1);
    assert(effect_metrics.dropped == 1);
    assert(effect_metrics.drained == 1);

    effects::BoundedEffectQueue sanitized_queue(4);
    effects::ObserveEffectSink observe_sink(
        &sanitized_queue, 2, 1000, true, &ObserveClock);
    event.target = "Download/localsend-redirect/a.jpg";
    assert(observe_sink.Submit(event));
    assert(observe_sink.Submit(event));
    assert(!observe_sink.Submit(event));
    assert(observe_sink.metrics().accepted == 2);
    assert(observe_sink.metrics().rate_limited == 1);
    effects::EffectEvent sanitized;
    assert(sanitized_queue.Pop(&sanitized));
    assert(sanitized.source == "<redacted>/a.jpg");
    assert(sanitized.target == "<redacted>/a.jpg");
    g_observe_clock = 1000;
    assert(observe_sink.Submit(event));
    event.kind = effects::EffectKind::kExport;
    assert(!observe_sink.Submit(event));
    assert(observe_sink.metrics().invalid_kind == 1);
    event.kind = effects::EffectKind::kObserve;
    const auto no_effect_dispatch = effects::Dispatch(
        pattern::kEffectNone, event, &observe_sink, nullptr);
    assert(no_effect_dispatch.requested == pattern::kEffectNone);
    assert(no_effect_dispatch.submitted == pattern::kEffectNone);

    pattern::OperationPlan plan;
    plan.accepted = true;
    plan.plan_generation = 9;
    CapabilitySnapshot capabilities;
    VfsBackend backend;
    assert(complete_vfs::Apply(plan, capabilities, kOperationOpenRead, &backend)
           == complete_vfs::ApplyStatus::kUnsupported);
    capabilities.observed_capabilities = kCapabilityFuseCompletePath;
    capabilities.capability_generation = 1;
    capabilities.plan_generation = plan.plan_generation;
    capabilities.domains[static_cast<unsigned>(AdmissionDomain::kCompleteVfs)] = {
        AdapterState::kActive,
        kOperationOpenRead | kOperationCreate,
        0,
    };
    assert(complete_vfs::Apply(plan, capabilities,
                              kOperationOpenRead | kOperationCreate, &backend)
           == complete_vfs::ApplyStatus::kApplied);
    assert(backend.calls == 1);
    ++capabilities.plan_generation;
    assert(complete_vfs::Apply(plan, capabilities, kOperationOpenRead, &backend)
           == complete_vfs::ApplyStatus::kUnsupported);
    assert(backend.calls == 1);
    return 0;
}
