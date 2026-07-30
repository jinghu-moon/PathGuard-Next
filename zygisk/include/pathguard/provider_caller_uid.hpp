#pragma once

#include <stdint.h>

namespace pathguard::provider_redirect {

struct CallerUidContext {
    bool in_path_hook = false;
    int32_t saved_binder_uid = -1;
    uint32_t binder_clear_depth = 0;
    int32_t fuse_uid = -1;
    const void* fuse_request = nullptr;
    void* policy_hazard = nullptr;
    void* policy_domain = nullptr;
    uint32_t policy_hazard_slot = UINT32_MAX;
};

constexpr bool IsApplicationCaller(int32_t uid, int32_t process_uid) noexcept {
    return uid >= 10000 && uid != process_uid;
}

constexpr void BeginBinderIdentityClear(CallerUidContext* context,
                                        int32_t calling_uid,
                                        int32_t process_uid) noexcept {
    if (context == nullptr) return;
    if (context->binder_clear_depth++ == 0) {
        context->saved_binder_uid = IsApplicationCaller(calling_uid, process_uid)
            ? calling_uid : -1;
    }
}

constexpr void EndBinderIdentityClear(CallerUidContext* context) noexcept {
    if (context == nullptr || context->binder_clear_depth == 0) return;
    if (--context->binder_clear_depth == 0) context->saved_binder_uid = -1;
}

constexpr void BeginFuseRequest(CallerUidContext* context,
                                const void* request,
                                int32_t calling_uid,
                                int32_t process_uid) noexcept {
    if (context == nullptr || request == nullptr) return;
    context->fuse_request = request;
    context->fuse_uid = IsApplicationCaller(calling_uid, process_uid)
        ? calling_uid : -1;
}

constexpr void EndFuseRequest(CallerUidContext* context,
                              const void* request) noexcept {
    if (context == nullptr || context->fuse_request != request) return;
    context->fuse_request = nullptr;
    context->fuse_uid = -1;
}

constexpr int32_t EffectiveCallerUid(const CallerUidContext* context,
                                     int32_t current_binder_uid,
                                     int32_t process_uid) noexcept {
    if (context != nullptr && IsApplicationCaller(context->fuse_uid, process_uid)) {
        return context->fuse_uid;
    }
    if (IsApplicationCaller(current_binder_uid, process_uid)) {
        return current_binder_uid;
    }
    return context != nullptr
        && IsApplicationCaller(context->saved_binder_uid, process_uid)
        ? context->saved_binder_uid : -1;
}

}  // namespace pathguard::provider_redirect
