#include <cassert>

#include "pathguard/provider_caller_uid.hpp"
#include "pathguard/provider_redirect_symbols.hpp"

int main() {
    using namespace pathguard::provider_redirect;

    constexpr int32_t provider_uid = 1023;
    CallerUidContext context;
    assert(EffectiveCallerUid(&context, provider_uid, provider_uid) == -1);
    assert(EffectiveCallerUid(&context, 10358, provider_uid) == 10358);

    BeginBinderIdentityClear(&context, 10358, provider_uid);
    assert(context.binder_clear_depth == 1);
    assert(EffectiveCallerUid(&context, provider_uid, provider_uid) == 10358);

    BeginBinderIdentityClear(&context, provider_uid, provider_uid);
    assert(context.binder_clear_depth == 2);
    assert(EffectiveCallerUid(&context, provider_uid, provider_uid) == 10358);
    EndBinderIdentityClear(&context);
    assert(EffectiveCallerUid(&context, provider_uid, provider_uid) == 10358);

    const int request_a = 1;
    const int request_b = 2;
    BeginFuseRequest(&context, &request_a, 10359, provider_uid);
    assert(EffectiveCallerUid(&context, 10358, provider_uid) == 10359);
    EndFuseRequest(&context, &request_b);
    assert(EffectiveCallerUid(&context, provider_uid, provider_uid) == 10359);
    EndFuseRequest(&context, &request_a);
    assert(EffectiveCallerUid(&context, provider_uid, provider_uid) == 10358);

    EndBinderIdentityClear(&context);
    assert(context.binder_clear_depth == 0);
    assert(EffectiveCallerUid(&context, provider_uid, provider_uid) == -1);
    EndBinderIdentityClear(&context);
    assert(context.binder_clear_depth == 0);

    BeginFuseRequest(&context, &request_a, provider_uid, provider_uid);
    assert(EffectiveCallerUid(&context, provider_uid, provider_uid) == -1);
    EndFuseRequest(&context, &request_a);

    assert(ClassifyFuseBoundarySymbol("fuse_req_userdata")
           == FuseBoundaryMethod::kRequestBegin);
    assert(ClassifyFuseBoundarySymbol("fuse_req_ctx")
           == FuseBoundaryMethod::kRequestBegin);
    assert(ClassifyFuseBoundarySymbol("fuse_reply_create")
           == FuseBoundaryMethod::kRequestEnd);
    assert(ClassifyFuseBoundarySymbol("fuse_reply_err")
           == FuseBoundaryMethod::kRequestEnd);
    assert(ClassifyFuseBoundarySymbol("fuse_reply_")
           == FuseBoundaryMethod::kNone);
    assert(ClassifyFuseBoundarySymbol(nullptr) == FuseBoundaryMethod::kNone);
    assert(ClassifyFuseBoundarySymbol("openat") == FuseBoundaryMethod::kNone);
    return 0;
}
