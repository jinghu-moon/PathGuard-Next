#include <cstddef>
#include <cstdint>
#include <span>

#include "pattern_harness_common.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    const auto input = std::span<const std::uint8_t>(data, size);
    (void)pathguard::pattern::test::ConsumeMatcherInput(input);
    return 0;
}
