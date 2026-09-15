#include "hide_vfs_model.h"
#include "test_assert.h"

#include <cerrno>
#include <cstring>

namespace {

constexpr pg_hide1_parent_identity kPictures{280, 15771};
constexpr pg_hide1_observer kTarget{10123, 0xabc};
constexpr pg_hide1_observer kControl{10124, 0xabc};
constexpr pg_hide1_observer kOtherNamespace{10123, 0xdef};
constexpr pg_hide1_observer kOracle{0, 0xabc};

pg_hide1_rule_model MakeRule() {
    pg_hide1_rule_model rule{};
    rule.parent = kPictures;
    rule.target = kTarget;
    rule.generation = 41;
    assert(pg_hide1_rule_set_basename(&rule, "hidden", 6));
    return rule;
}

pg_hide1_decision Evaluate(const pg_hide1_rule_model& rule,
                           pg_hide1_operation operation,
                           const pg_hide1_observer& observer,
                           pg_hide1_parent_identity parent = kPictures,
                           const char* basename = "hidden") {
    return pg_hide1_evaluate(&rule, operation, &observer, parent, basename,
                             static_cast<pg_hide1_u16>(std::strlen(basename)));
}

pg_hide1_decision EvaluateCache(const pg_hide1_rule_model& rule,
                                const pg_hide1_observer& observer,
                                pg_hide1_parent_identity parent,
                                pg_hide1_cache_kind cache,
                                pg_hide1_u64 generation) {
    return pg_hide1_evaluate_cache(&rule, &observer, parent, "hidden", 6,
                                   cache, generation);
}

}  // namespace

int main() {
    pg_hide1_rule_model rule = MakeRule();
    assert(pg_hide1_rule_valid(&rule));
    assert(pg_hide1_accepts_generation(&rule, 41));
    assert(!pg_hide1_accepts_generation(&rule, 0));
    assert(!pg_hide1_accepts_generation(&rule, 42));

    pg_hide1_rule_model invalid = rule;
    invalid.parent = {};
    assert(!pg_hide1_rule_valid(&invalid));
    invalid = rule;
    invalid.generation = 0;
    assert(!pg_hide1_rule_valid(&invalid));
    assert(!pg_hide1_rule_set_basename(&invalid, "nested/name", 11));

    const pg_hide1_operation operations[] = {
        PG_HIDE1_LOOKUP, PG_HIDE1_ATOMIC_OPEN, PG_HIDE1_READDIR,
        PG_HIDE1_CREATE, PG_HIDE1_MKDIR, PG_HIDE1_MKNOD,
        PG_HIDE1_SYMLINK, PG_HIDE1_UNLINK, PG_HIDE1_RMDIR,
        PG_HIDE1_LINK, PG_HIDE1_RENAME,
    };
    for (const pg_hide1_operation operation : operations) {
        const pg_hide1_decision result = Evaluate(rule, operation, kTarget);
        if (operation == PG_HIDE1_LOOKUP) {
            assert(result.outcome == PG_HIDE1_HIDE_NEGATIVE);
            assert(result.error_number == ENOENT);
        } else if (operation == PG_HIDE1_READDIR) {
            assert(result.outcome == PG_HIDE1_OMIT);
        } else {
            assert(result.outcome == PG_HIDE1_REJECT);
            assert(result.error_number == ENOENT);
        }
        assert(!result.call_original);
    }

    const pg_hide1_observer non_targets[] = {
        kControl, kOtherNamespace, kOracle
    };
    for (const pg_hide1_observer observer : non_targets) {
        for (const pg_hide1_operation operation : operations) {
            const pg_hide1_decision result = Evaluate(rule, operation, observer);
            assert(result.outcome == PG_HIDE1_PASS);
            assert(result.call_original);
        }
    }
    assert(Evaluate(rule, PG_HIDE1_LOOKUP, kTarget, {281, 15771}).outcome ==
           PG_HIDE1_PASS);
    assert(Evaluate(rule, PG_HIDE1_LOOKUP, kTarget, {280, 15772}).outcome ==
           PG_HIDE1_PASS);
    assert(Evaluate(rule, PG_HIDE1_LOOKUP, kTarget, kPictures, "other").outcome ==
           PG_HIDE1_PASS);

    assert(EvaluateCache(rule, kTarget, kPictures,
                         PG_HIDE1_SYNTHETIC_NEGATIVE, 41).outcome ==
           PG_HIDE1_KEEP_CACHE);
    assert(EvaluateCache(rule, kTarget, kPictures,
                         PG_HIDE1_SYNTHETIC_NEGATIVE, 40).outcome ==
           PG_HIDE1_INVALIDATE_CACHE);
    assert(EvaluateCache(rule, kTarget, kPictures,
                         PG_HIDE1_REAL_POSITIVE, 0).outcome ==
           PG_HIDE1_INVALIDATE_CACHE);
    const pg_hide1_decision target_real_negative = EvaluateCache(
        rule, kTarget, kPictures, PG_HIDE1_REAL_NEGATIVE, 0);
    assert(target_real_negative.outcome == PG_HIDE1_KEEP_CACHE);
    assert(!target_real_negative.call_original);
    assert(EvaluateCache(rule, kControl, kPictures,
                         PG_HIDE1_SYNTHETIC_NEGATIVE, 41).outcome ==
           PG_HIDE1_INVALIDATE_CACHE);
    assert(EvaluateCache(rule, kControl, kPictures,
                         PG_HIDE1_REAL_POSITIVE, 0).outcome ==
           PG_HIDE1_KEEP_CACHE);
    assert(EvaluateCache(rule, kControl, kPictures,
                         PG_HIDE1_REAL_NEGATIVE, 0).call_original);
    assert(EvaluateCache(rule, kTarget, {281, 15771},
                         PG_HIDE1_SYNTHETIC_NEGATIVE, 41).outcome ==
           PG_HIDE1_INVALIDATE_CACHE);

    assert(pg_hide1_evaluate_rename(&rule, &kTarget,
               kPictures, "hidden", 6, kPictures, "new", 3).outcome ==
           PG_HIDE1_REJECT);
    assert(pg_hide1_evaluate_rename(&rule, &kTarget,
               kPictures, "old", 3, kPictures, "hidden", 6).outcome ==
           PG_HIDE1_REJECT);
    assert(pg_hide1_evaluate_rename(&rule, &kTarget,
               kPictures, "old", 3, kPictures, "new", 3).outcome ==
           PG_HIDE1_PASS);
    assert(pg_hide1_evaluate_rename(&rule, &kTarget,
               {281, 1}, "hidden", 6, kPictures, "new", 3).outcome ==
           PG_HIDE1_PASS);
    assert(pg_hide1_evaluate_rename(&rule, &kControl,
               kPictures, "hidden", 6, kPictures, "new", 3).outcome ==
           PG_HIDE1_PASS);
    assert(pg_hide1_evaluate(&rule,
               static_cast<pg_hide1_operation>(999), &kTarget,
               kPictures, "hidden", 6).outcome == PG_HIDE1_PASS);
    assert(pg_hide1_evaluate(nullptr, PG_HIDE1_LOOKUP, &kTarget,
               kPictures, "hidden", 6).outcome == PG_HIDE1_PASS);
    return 0;
}
