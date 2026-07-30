#pragma once

#include <errno.h>
#include <stdint.h>

namespace pathguard {

enum class DecisionReason : uint8_t {
    kNoMatch,
    kCapabilityMissing,
    kRuntimeUnavailable,
    kBudgetExceeded,
    kInvalidEncoding,
    kUnsafeTarget,
    kDenied,
    kCollision,
    kAmbiguousReverse,
    kProvenanceCorrupt,
};

enum class AdapterDisposition : uint8_t { kPass, kReject };

struct AdapterFailure {
    AdapterDisposition disposition = AdapterDisposition::kPass;
    int error_number = 0;
    bool audit = false;
    uint32_t status_code = 0;
};

constexpr AdapterFailure TranslateFailure(DecisionReason reason) noexcept {
    switch (reason) {
        case DecisionReason::kDenied:
            return {AdapterDisposition::kReject, EACCES, true, 1001};
        case DecisionReason::kCollision:
            return {AdapterDisposition::kReject, EEXIST, true, 1002};
        case DecisionReason::kAmbiguousReverse:
            return {AdapterDisposition::kReject, EXDEV, true, 1003};
        case DecisionReason::kProvenanceCorrupt:
            return {AdapterDisposition::kReject, EIO, true, 1004};
        case DecisionReason::kCapabilityMissing:
            return {AdapterDisposition::kPass, 0, true, 2001};
        case DecisionReason::kRuntimeUnavailable:
            return {AdapterDisposition::kPass, 0, true, 2002};
        case DecisionReason::kBudgetExceeded:
            return {AdapterDisposition::kPass, 0, true, 2003};
        case DecisionReason::kInvalidEncoding:
            return {AdapterDisposition::kPass, 0, true, 2004};
        case DecisionReason::kUnsafeTarget:
            return {AdapterDisposition::kPass, 0, true, 2005};
        case DecisionReason::kNoMatch:
            return {};
    }
    return {};
}

struct DiagnosticKey {
    int32_t caller_uid = -1;
    uint32_t reason = 0;
    uint64_t rule_id = 0;
    bool operator==(const DiagnosticKey&) const = default;
};

class DiagnosticLimiter final {
public:
    static constexpr uint32_t kCapacity = 64;
    bool Allow(const DiagnosticKey& key, uint64_t interval) noexcept;
    uint64_t suppressed() const noexcept { return suppressed_; }
private:
    struct Entry {
        DiagnosticKey key;
        uint64_t last_tick = 0;
        bool used = false;
    };
    Entry entries_[kCapacity]{};
    uint64_t tick_ = 0;
    uint64_t suppressed_ = 0;
};

}  // namespace pathguard
