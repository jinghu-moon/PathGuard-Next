#include <cstddef>

#include "pathguard/policy_snapshot_domain.hpp"
#include "test_assert.h"

namespace {

struct Snapshot {
    unsigned generation = 0;
};

unsigned destroyed = 0;

void ReleaseSnapshot(Snapshot* snapshot) {
    if (snapshot != nullptr) {
        ++destroyed;
        delete snapshot;
    }
}

}  // namespace

int main() {
    using namespace pathguard::provider_redirect;
    destroyed = 0;
    PolicySnapshotDomain<Snapshot, 1> domain(&ReleaseSnapshot);
    assert(domain.Publish(new Snapshot{1}, sizeof(Snapshot)));

    CallerUidContext first;
    auto held = domain.Acquire(&first);
    assert(held && held->generation == 1);
    CallerUidContext second;
    assert(!domain.Acquire(&second));
    assert(domain.metrics().hazard_slot_acquire_fail_total == 1);
    assert(domain.metrics().hazard_slots_in_use_high_watermark == 1);

    assert(domain.Publish(new Snapshot{2}, sizeof(Snapshot)));
    assert(destroyed == 0);
    held = {};
    assert(domain.Publish(new Snapshot{3}, sizeof(Snapshot)));
    assert(destroyed >= 2);

    auto* oversized = new Snapshot{4};
    assert(!domain.Publish(oversized, 8 * 1024 * 1024 + 1));
    delete oversized;
    assert(domain.metrics().snapshot_reload_rejected_retire_limit_total == 1);

    auto before_fork = domain.Acquire(&first);
    assert(before_fork && before_fork->generation == 3);
    before_fork = {};
    PolicySnapshotAtForkChild();
    assert(domain.Publish(new Snapshot{5}, sizeof(Snapshot)));
    auto after_fork = domain.Acquire(&first);
    assert(after_fork && after_fork->generation == 5);
    const unsigned before_publish = destroyed;
    assert(domain.Publish(new Snapshot{6}, sizeof(Snapshot)));
    assert(destroyed == before_publish);
    after_fork = {};
    assert(domain.Publish(new Snapshot{7}, sizeof(Snapshot)));
    assert(destroyed > before_publish);
    assert(domain.metrics().post_fork_registry_rebuild_total == 1);
    return 0;
}
