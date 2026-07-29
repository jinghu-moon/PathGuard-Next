#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pathguard::provider_redirect {

enum class FuseBoundaryMethod {
    kNone,
    kRequestBegin,
    kRequestEnd,
};

constexpr bool ConsumePrefix(const char** input, const char* prefix) noexcept {
    const char* cursor = *input;
    while (*prefix != '\0') {
        if (*cursor++ != *prefix++) return false;
    }
    *input = cursor;
    return true;
}

constexpr bool StringEquals(const char* lhs, const char* rhs) noexcept {
    if (lhs == nullptr || rhs == nullptr) return false;
    while (*lhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

constexpr FuseBoundaryMethod ClassifyFuseBoundarySymbol(
    const char* symbol) noexcept {
    if (StringEquals(symbol, "fuse_req_userdata")
        || StringEquals(symbol, "fuse_req_ctx")) {
        return FuseBoundaryMethod::kRequestBegin;
    }
    constexpr const char reply_prefix[] = "fuse_reply_";
    const char* cursor = symbol;
    return cursor != nullptr && ConsumePrefix(&cursor, reply_prefix)
        && *cursor != '\0'
        ? FuseBoundaryMethod::kRequestEnd : FuseBoundaryMethod::kNone;
}

}  // namespace pathguard::provider_redirect
