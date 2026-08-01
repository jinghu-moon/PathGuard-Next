#include "pathguard/failure_policy.h"
#include "pathguard/resolver_probe_cache.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    assert(TranslateFailure(DecisionReason::kNoMatch).disposition
           == AdapterDisposition::kPass);
    assert(!TranslateFailure(DecisionReason::kNoMatch).audit);
    assert(TranslateFailure(DecisionReason::kDenied).error_number == EACCES);
    assert(TranslateFailure(DecisionReason::kCollision).error_number == EEXIST);
    assert(TranslateFailure(DecisionReason::kAmbiguousReverse).error_number == EXDEV);
    assert(TranslateFailure(DecisionReason::kBudgetExceeded).disposition
           == AdapterDisposition::kPass);

    DiagnosticLimiter limiter;
    const DiagnosticKey key{10358, 2003, 41};
    assert(limiter.Allow(key, 3));
    assert(!limiter.Allow(key, 3));
    assert(!limiter.Allow(key, 3));
    assert(limiter.Allow(key, 3));
    assert(limiter.suppressed() == 2);

    ResolverProbeCache cache;
    cache.Invalidate(4);
    cache.ObserveOpenAt2(ENOSYS, true);
    assert(cache.mode() == ResolverMode::kComponentWalk);
    assert(cache.capabilities() == kCapabilityComponentFdWalk);
    cache.Invalidate(5);
    assert(cache.mode() == ResolverMode::kUnknown);
    cache.ObserveOpenAt2(EAGAIN, true);
    assert(cache.mode() == ResolverMode::kUnknown);
    cache.ObserveOpenAt2(0, true);
    assert(cache.mode() == ResolverMode::kOpenAt2);
    cache.ObserveOpenAt2(EPERM, true);
    assert(cache.mode() == ResolverMode::kComponentWalk);
    assert(cache.probe_error() == EPERM);
    return 0;
}
