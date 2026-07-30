#include <atomic>
#include <thread>
#include <vector>

#include "pathguard/snapshot_publisher.h"
#include "test_assert.h"

namespace {

struct TestSnapshot {
    static std::atomic<unsigned> destroyed;
    explicit TestSnapshot(unsigned value) : generation(value), inverse(~value) {}
    ~TestSnapshot() { destroyed.fetch_add(1); }
    unsigned generation;
    unsigned inverse;
};

std::atomic<unsigned> TestSnapshot::destroyed{0};

}  // namespace

int main() {
    using namespace pathguard::snapshot;
    TestSnapshot::destroyed.store(0);
    Domain<TestSnapshot> domain(Profile::kAppPath);
    assert(domain.slot_capacity() == 128);
    assert(domain.Publish(new TestSnapshot(1), sizeof(TestSnapshot)));
    auto old = domain.Acquire();
    assert(old && old->generation == 1);
    assert(domain.Publish(new TestSnapshot(2), sizeof(TestSnapshot)));
    assert(TestSnapshot::destroyed.load() == 0);
    domain.Reclaim();
    assert(TestSnapshot::destroyed.load() == 0);
    old.Reset();
    domain.Reclaim();
    assert(TestSnapshot::destroyed.load() == 1);

    std::atomic<bool> stop{false};
    std::atomic<bool> invalid{false};
    std::vector<std::thread> readers;
    for (unsigned i = 0; i < 8; ++i) {
        readers.emplace_back([&] {
            while (!stop.load()) {
                auto guard = domain.Acquire();
                if (guard && guard->inverse != ~guard->generation) {
                    invalid.store(true);
                }
            }
        });
    }
    for (unsigned generation = 3; generation < 100; ++generation) {
        auto* next = new TestSnapshot(generation);
        if (!domain.Publish(next, sizeof(TestSnapshot))) delete next;
    }
    stop.store(true);
    for (auto& reader : readers) reader.join();
    assert(!invalid.load());
    domain.Reclaim();

    Domain<TestSnapshot> limited(Profile::kProvider);
    assert(limited.Publish(new TestSnapshot(1), kMaxRetiredSnapshotBytes));
    auto held = limited.Acquire();
    assert(limited.Publish(new TestSnapshot(2), 1));
    auto* rejected = new TestSnapshot(3);
    assert(!limited.Publish(rejected, 1));
    delete rejected;
    assert(limited.metrics().snapshot_reload_rejected_retire_limit_total == 1);
    assert(limited.metrics().retired_snapshot_bytes_high_watermark
           == kMaxRetiredSnapshotBytes);
    held.Reset();
    limited.Reclaim();
    assert(limited.retired_count() == 0);

    Domain<TestSnapshot> forked(Profile::kAppPath);
    assert(forked.atfork_registered());
    assert(forked.Publish(new TestSnapshot(10), sizeof(TestSnapshot)));
    Domain<TestSnapshot>::MarkPostForkDirtyForTesting();
    assert(!forked.Acquire());
    assert(forked.metrics().post_fork_registry_rebuild_total == 1);
    assert(forked.Publish(new TestSnapshot(11), sizeof(TestSnapshot)));
    auto rebuilt = forked.Acquire();
    assert(rebuilt && rebuilt->generation == 11);
    return 0;
}
