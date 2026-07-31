#pragma once

#include <cstdint>
#include <string>

#include "pathguard/pattern_runtime.h"

namespace pathguard::effects {

enum class EffectKind : std::uint8_t { kObserve, kExport };

struct EffectEvent {
    EffectKind kind = EffectKind::kObserve;
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::uint64_t generation = 0;
    pattern::RuntimeRuleId rule_id = 0;
    std::string source;
    std::string target;
};

class EffectSink {
public:
    virtual ~EffectSink() = default;
    virtual bool Submit(const EffectEvent& event) = 0;
};

struct DispatchResult {
    std::uint8_t requested = pattern::kEffectNone;
    std::uint8_t submitted = pattern::kEffectNone;
    std::uint8_t failed = pattern::kEffectNone;
};

inline DispatchResult Dispatch(std::uint8_t effects,
                               const EffectEvent& base,
                               EffectSink* observe,
                               EffectSink* export_sink) {
    DispatchResult result;
    result.requested = effects;
    const auto submit = [&](std::uint8_t bit, EffectKind kind,
                            EffectSink* sink) {
        if ((effects & bit) == 0) return;
        EffectEvent event = base;
        event.kind = kind;
        if (sink != nullptr && sink->Submit(event)) result.submitted |= bit;
        else result.failed |= bit;
    };
    submit(pattern::kEffectObserve, EffectKind::kObserve, observe);
    submit(pattern::kEffectExport, EffectKind::kExport, export_sink);
    return result;
}

}  // namespace pathguard::effects
