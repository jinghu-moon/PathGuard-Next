#pragma once

#include <stdint.h>
#include <time.h>

namespace pathguard::perf {

inline uint64_t NowNs() {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return static_cast<uint64_t>(value.tv_sec) * 1000000000ULL
        + static_cast<uint64_t>(value.tv_nsec);
}

inline uint64_t ElapsedNs(uint64_t start) {
    const uint64_t now = NowNs();
    return now >= start ? now - start : 0;
}

inline uint64_t NsToUs(uint64_t value) {
    return value / 1000ULL;
}

}  // namespace pathguard::perf
