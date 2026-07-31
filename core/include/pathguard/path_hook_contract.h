#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>

#include "pathguard/capabilities.h"
#include "pathguard/storage_path_adapter.h"

namespace pathguard::path_hook_contract {

enum class CanonicalPathDisposition : uint8_t {
    kPass,
    kStaticLogicalPath,
    kProvenanceLookup,
    kAmbiguousReverse,
};

inline CanonicalPathDisposition PlanCanonicalPath(
        const storage_path_adapter::RewriteResult& rewrite) {
    if (rewrite.disposition
        != storage_path_adapter::RewriteDisposition::kRedirect) {
        return CanonicalPathDisposition::kPass;
    }
    // policy format 6: 1=static_unique, 2=provenance. A path hook has no
    // authority to invent a source when a provenance lookup is required.
    if (rewrite.reverse_mode == 1) {
        return CanonicalPathDisposition::kStaticLogicalPath;
    }
    return rewrite.reverse_mode == 2
        ? CanonicalPathDisposition::kProvenanceLookup
        : CanonicalPathDisposition::kAmbiguousReverse;
}

inline bool ShouldCoordinateProvenanceMutation(
        const storage_path_adapter::RewriteResult& rewrite,
        OperationMask observed_operations) noexcept {
    return rewrite.reverse_mode == 2
        && (observed_operations & kOperationReverseMapping) != 0;
}

struct RedirectOpenPlan {
    bool coordinate_provenance = false;
    int effective_flags = 0;
};

inline RedirectOpenPlan PlanRedirectOpen(
        const storage_path_adapter::RewriteResult& rewrite,
        OperationMask observed_operations, int flags) noexcept {
    // MediaProvider may create and reopen the same routed object through
    // different visible path aliases in one request. Collision ownership is
    // only enforceable by the admitted provenance transaction.
    return {
        (flags & O_CREAT) != 0
            && ShouldCoordinateProvenanceMutation(rewrite,
                                                  observed_operations),
        flags,
    };
}

enum class Api : uint8_t {
    kLookupStat,
    kAccess,
    kOpenRead,
    kOpenWrite,
    kCreate,
    kDirectoryIterate,
    kMkdir,
    kRename,
    kUnlink,
    kRmdir,
    kCanonicalPath,
    kReadlink,
    kMetadataMutation,
    kTruncate,
    kWatch,
};

constexpr OperationMask OperationFor(Api api) noexcept {
    switch (api) {
        case Api::kLookupStat: return kOperationLookupStat;
        case Api::kAccess: return kOperationAccess;
        case Api::kOpenRead: return kOperationOpenRead;
        case Api::kOpenWrite: return kOperationOpenWrite;
        case Api::kCreate: return kOperationCreate;
        case Api::kDirectoryIterate: return kOperationDirectoryIterate;
        case Api::kMkdir: return kOperationMkdir;
        case Api::kRename: return kOperationRename;
        case Api::kUnlink: return kOperationUnlink;
        case Api::kRmdir: return kOperationRmdir;
        case Api::kCanonicalPath: return kOperationCanonicalPath;
        case Api::kReadlink: return kOperationReadlink;
        case Api::kMetadataMutation: return kOperationMetadataMutation;
        case Api::kTruncate: return kOperationTruncate;
        case Api::kWatch: return kOperationWatch;
    }
    return 0;
}

constexpr OperationMask OpenOperation(int flags) noexcept {
#if defined(O_ACCMODE)
    const int access = flags & O_ACCMODE;
#else
    const int access = flags & (O_WRONLY | O_RDWR);
#endif
    OperationMask operation = access == O_WRONLY || access == O_RDWR
        ? kOperationOpenWrite : kOperationOpenRead;
    if ((flags & O_CREAT) != 0) operation |= kOperationCreate;
    return operation;
}

enum class Disposition : uint8_t { kPass, kCall, kReject };

struct OperandPlan {
    Disposition disposition = Disposition::kPass;
    const char* path = nullptr;
    int error_number = 0;
    uint64_t rule_id = 0;
};

inline OperandPlan PlanOperand(
        const char* original, const char* rewritten,
        const storage_path_adapter::RewriteResult& result) noexcept {
    OperandPlan output;
    output.path = original;
    output.rule_id = result.rule_id;
    if (result.disposition
        == storage_path_adapter::RewriteDisposition::kDeny) {
        output.disposition = Disposition::kReject;
        output.error_number = EACCES;
    } else if (result.disposition
               == storage_path_adapter::RewriteDisposition::kRedirect) {
        output.disposition = Disposition::kCall;
        output.path = rewritten;
    }
    return output;
}

struct TwoOperandPlan {
    Disposition disposition = Disposition::kPass;
    const char* first = nullptr;
    const char* second = nullptr;
    int error_number = 0;
};

inline TwoOperandPlan PlanRename(const OperandPlan& first,
                                 const OperandPlan& second) noexcept {
    TwoOperandPlan output;
    output.first = first.path;
    output.second = second.path;
    if (first.disposition == Disposition::kReject
        || second.disposition == Disposition::kReject) {
        output.disposition = Disposition::kReject;
        output.error_number = EACCES;
        return output;
    }
    const bool first_redirect = first.disposition == Disposition::kCall;
    const bool second_redirect = second.disposition == Disposition::kCall;
    if (first_redirect != second_redirect) {
        output.disposition = Disposition::kReject;
        output.error_number = EXDEV;
        return output;
    }
    if (first_redirect && second_redirect) {
        if (first.path == nullptr || second.path == nullptr) {
            output.disposition = Disposition::kReject;
            output.error_number = EINVAL;
            return output;
        }
        output.disposition = Disposition::kCall;
    }
    return output;
}

}  // namespace pathguard::path_hook_contract
