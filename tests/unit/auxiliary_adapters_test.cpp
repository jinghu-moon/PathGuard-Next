#include "pathguard/complete_vfs_adapter.h"
#include "pathguard/effect_adapter.h"
#include "pathguard/export_worker.h"
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

    exporting::ExportWorker worker(1);
    exporting::ExportTask task{{1, 2, 30, 3}, "Pictures/a.jpg",
                               "Download/export/a.jpg"};
    assert(worker.Enqueue(task) == exporting::EnqueueResult::kQueued);
    assert(worker.Enqueue(task) == exporting::EnqueueResult::kDuplicate);
    assert(worker.Enqueue({{1, 4, 40, 3}, "Pictures/b.jpg", "Download/b.jpg"})
           == exporting::EnqueueResult::kFull);
    assert(worker.RunNext([](const exporting::ExportTask&) { return true; }));
    assert(worker.State(task.key) == exporting::TaskState::kComplete);
    assert(worker.metrics().duplicates == 1 && worker.metrics().overflow == 1);

    pattern::OperationPlan plan;
    plan.accepted = true;
    CapabilitySnapshot capabilities;
    VfsBackend backend;
    assert(complete_vfs::Apply(plan, capabilities, kOperationOpenRead, &backend)
           == complete_vfs::ApplyStatus::kUnsupported);
    capabilities.observed_capabilities = kCapabilityFuseCompletePath;
    assert(complete_vfs::Apply(plan, capabilities,
                              kOperationOpenRead | kOperationCreate, &backend)
           == complete_vfs::ApplyStatus::kApplied);
    assert(backend.calls == 1);
    return 0;
}
