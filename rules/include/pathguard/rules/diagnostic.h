#pragma once

#include <cstdint>
#include <string_view>

namespace pathguard::rules {

struct ByteSpan {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;

    bool operator==(const ByteSpan&) const = default;
};

struct Diagnostic {
    std::string_view code;
    std::string_view message_key;
    ByteSpan primary;
    bool omitted = false;
};

}  // namespace pathguard::rules
