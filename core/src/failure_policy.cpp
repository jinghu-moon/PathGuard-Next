#include "pathguard/failure_policy.h"

namespace pathguard {

bool DiagnosticLimiter::Allow(const DiagnosticKey& key,
                              uint64_t interval) noexcept {
    ++tick_;
    Entry* empty = nullptr;
    Entry* oldest = &entries_[0];
    for (auto& entry : entries_) {
        if (!entry.used) {
            if (empty == nullptr) empty = &entry;
            continue;
        }
        if (entry.key == key) {
            if (tick_ - entry.last_tick < interval) {
                ++suppressed_;
                return false;
            }
            entry.last_tick = tick_;
            return true;
        }
        if (!oldest->used || entry.last_tick < oldest->last_tick) oldest = &entry;
    }
    Entry* selected = empty != nullptr ? empty : oldest;
    selected->key = key;
    selected->last_tick = tick_;
    selected->used = true;
    return true;
}

}  // namespace pathguard
