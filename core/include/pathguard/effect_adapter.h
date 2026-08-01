#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

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

struct EffectQueueMetrics {
    std::uint64_t accepted = 0;
    std::uint64_t dropped = 0;
    std::uint64_t drained = 0;
};

struct ObserveMetrics {
    std::uint64_t accepted = 0;
    std::uint64_t rate_limited = 0;
    std::uint64_t downstream_failed = 0;
    std::uint64_t invalid_kind = 0;
};

using ObserveClock = std::uint64_t (*)() noexcept;

inline std::uint64_t MonotonicMilliseconds() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline std::string RedactPath(std::string_view path) {
    if (path.empty()) return {};
    const std::size_t slash = path.find_last_of('/');
    const std::string_view basename = slash == std::string_view::npos
        ? path : path.substr(slash + 1);
    return std::string("<redacted>/") + std::string(basename);
}

// Producers never wait on the consumer. A full or contended queue drops only
// the auxiliary effect; the caller's primary I/O decision remains unchanged.
class BoundedEffectQueue final : public EffectSink {
public:
    explicit BoundedEffectQueue(std::size_t capacity) : capacity_(capacity) {}

    bool Submit(const EffectEvent& event) override {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || capacity_ == 0 || queue_.size() >= capacity_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        queue_.push_back(event);
        accepted_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool Pop(EffectEvent* event) {
        if (event == nullptr) return false;
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return false;
        *event = std::move(queue_.front());
        queue_.pop_front();
        drained_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    std::size_t pending() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    EffectQueueMetrics metrics() const {
        return {
            accepted_.load(std::memory_order_relaxed),
            dropped_.load(std::memory_order_relaxed),
            drained_.load(std::memory_order_relaxed),
        };
    }

private:
    std::size_t capacity_ = 0;
    mutable std::mutex mutex_;
    std::deque<EffectEvent> queue_;
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> drained_{0};
};

// Observe is auxiliary telemetry: it is sampled and sanitized before it enters
// the shared non-blocking sink. Primary I/O decisions never depend on success.
class ObserveEffectSink final : public EffectSink {
public:
    ObserveEffectSink(EffectSink* downstream, std::uint64_t max_events,
                      std::uint64_t window_ms, bool redact_paths,
                      ObserveClock clock = &MonotonicMilliseconds)
        : downstream_(downstream), max_events_(max_events),
          window_ms_(window_ms), redact_paths_(redact_paths), clock_(clock) {}

    bool Submit(const EffectEvent& event) override {
        if (event.kind != EffectKind::kObserve) {
            invalid_kind_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            rate_limited_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const std::uint64_t now = clock_ == nullptr ? 0 : clock_();
        if (!window_initialized_ || now < window_start_
            || now - window_start_ >= window_ms_) {
            window_start_ = now;
            window_count_ = 0;
            window_initialized_ = true;
        }
        if (max_events_ == 0 || window_count_ >= max_events_) {
            rate_limited_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        ++window_count_;
        EffectEvent sanitized = event;
        if (redact_paths_) {
            sanitized.source = RedactPath(event.source);
            sanitized.target = RedactPath(event.target);
        }
        lock.unlock();
        if (downstream_ == nullptr || !downstream_->Submit(sanitized)) {
            downstream_failed_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        accepted_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    ObserveMetrics metrics() const {
        return {
            accepted_.load(std::memory_order_relaxed),
            rate_limited_.load(std::memory_order_relaxed),
            downstream_failed_.load(std::memory_order_relaxed),
            invalid_kind_.load(std::memory_order_relaxed),
        };
    }

private:
    EffectSink* downstream_ = nullptr;
    std::uint64_t max_events_ = 0;
    std::uint64_t window_ms_ = 0;
    bool redact_paths_ = true;
    ObserveClock clock_ = &MonotonicMilliseconds;
    mutable std::mutex mutex_;
    bool window_initialized_ = false;
    std::uint64_t window_start_ = 0;
    std::uint64_t window_count_ = 0;
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> rate_limited_{0};
    std::atomic<std::uint64_t> downstream_failed_{0};
    std::atomic<std::uint64_t> invalid_kind_{0};
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
