#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pathguard::provider_redirect {

enum class BinderCallerMethod {
    kNone,
    kGetCallingUid,
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

constexpr BinderCallerMethod ClassifyBinderCallerSymbol(
    const char* symbol) noexcept {
    constexpr char const_prefix[] = "_ZNK7android14IPCThreadState";
    constexpr char mutable_prefix[] = "_ZN7android14IPCThreadState";
    if (symbol == nullptr) return BinderCallerMethod::kNone;
    const char* cursor = symbol;
    if (!ConsumePrefix(&cursor, const_prefix)) {
        cursor = symbol;
        if (!ConsumePrefix(&cursor, mutable_prefix)) return BinderCallerMethod::kNone;
    }

    size_t method_length = 0;
    bool has_length = false;
    while (*cursor >= '0' && *cursor <= '9') {
        has_length = true;
        method_length = method_length * 10 + static_cast<size_t>(*cursor - '0');
        if (method_length > 64) return BinderCallerMethod::kNone;
        ++cursor;
    }
    if (!has_length) return BinderCallerMethod::kNone;

    constexpr char method[] = "getCallingUid";
    if (method_length == sizeof(method) - 1) {
        for (size_t index = 0; index < method_length; ++index) {
            if (cursor[index] != method[index]) return BinderCallerMethod::kNone;
        }
        return StringEquals(cursor + method_length, "Ev")
            ? BinderCallerMethod::kGetCallingUid : BinderCallerMethod::kNone;
    }
    return BinderCallerMethod::kNone;
}

}  // namespace pathguard::provider_redirect
