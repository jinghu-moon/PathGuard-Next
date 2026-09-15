#include "hide_vfs_model.h"
#include "test_assert.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace {

pg_hide1_rule_model MakeRule() {
    pg_hide1_rule_model rule{};
    rule.parent = {280, 15771};
    rule.target = {10123, 0xabc};
    rule.generation = 41;
    assert(pg_hide1_rule_set_basename(&rule, "hidden", 6));
    return rule;
}

}  // namespace

int main() {
    const pg_hide1_rule_model rule = MakeRule();
    const pg_hide1_observer target{10123, 0xabc};
    const pg_hide1_observer other{10124, 0xabc};
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;

    for (int i = 0; i < 20; ++i) {
        workers.emplace_back([&] {
            for (int n = 0; n < 10000; ++n) {
                const auto lookup = pg_hide1_evaluate(
                    &rule, PG_HIDE1_LOOKUP, &target, rule.parent,
                    "hidden", 6);
                const auto readdir = pg_hide1_evaluate(
                    &rule, PG_HIDE1_READDIR, &target, rule.parent,
                    "hidden", 6);
                const auto pass = pg_hide1_evaluate(
                    &rule, PG_HIDE1_LOOKUP, &other, rule.parent,
                    "hidden", 6);
                const auto rename = pg_hide1_evaluate_rename(
                    &rule, &target, rule.parent, "hidden", 6,
                    rule.parent, "new", 3);
                if (lookup.outcome != PG_HIDE1_HIDE_NEGATIVE ||
                    readdir.outcome != PG_HIDE1_OMIT ||
                    pass.outcome != PG_HIDE1_PASS ||
                    rename.outcome != PG_HIDE1_REJECT)
                    failed.store(true, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    assert(!failed.load(std::memory_order_relaxed));
    return 0;
}
