#include "pathguard/provider_redirect_hook.hpp"
#include "pathguard/path_hook_contract.h"
#include "pathguard/provider_caller_uid.hpp"
#include "pathguard/provider_route_context.h"
#include "pathguard/provenance_protocol.h"
#include "pathguard/secure_path_resolver.h"
#include "pathguard/policy_snapshot_domain.hpp"

#include <android/dlext.h>
#include <android/log.h>

#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <linux/stat.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/statvfs.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace pathguard::provider_redirect {
namespace {

constexpr char kLogTag[] = "PathGuard";
constexpr uint32_t kMaxScopes = 64;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

using OpenFn = int (*)(const char*, int, ...);
using OpenAtFn = int (*)(int, const char*, int, ...);
using StatFn = int (*)(const char*, struct stat*);
using FstatAtFn = int (*)(int, const char*, struct stat*, int);
#if defined(__LP64__)
using Stat64 = struct stat;
#else
using Stat64 = struct stat64;
#endif
using Stat64Fn = int (*)(const char*, Stat64*);
using FstatAt64Fn = int (*)(int, const char*, Stat64*, int);
using AccessFn = int (*)(const char*, int);
using FaccessAtFn = int (*)(int, const char*, int, int);
using OpenDirFn = DIR* (*)(const char*);
using MkdirFn = int (*)(const char*, mode_t);
using MkdirAtFn = int (*)(int, const char*, mode_t);
using UnlinkFn = int (*)(const char*);
using UnlinkAtFn = int (*)(int, const char*, int);
using RemoveFn = int (*)(const char*);
using RmdirFn = int (*)(const char*);
using RenameFn = int (*)(const char*, const char*);
using RenameAtFn = int (*)(int, const char*, int, const char*);
using LinkFn = int (*)(const char*, const char*);
using LinkAtFn = int (*)(int, const char*, int, const char*, int);
using ReadlinkFn = ssize_t (*)(const char*, char*, size_t);
using RealpathFn = char* (*)(const char*, char*);
using ChmodFn = int (*)(const char*, mode_t);
using FchmodAtFn = int (*)(int, const char*, mode_t, int);
using ChownFn = int (*)(const char*, uid_t, gid_t);
using LchownFn = int (*)(const char*, uid_t, gid_t);
using FchownAtFn = int (*)(int, const char*, uid_t, gid_t, int);
using TruncateFn = int (*)(const char*, off_t);
using Truncate64Fn = int (*)(const char*, off64_t);
using UtimensAtFn = int (*)(int, const char*, const struct timespec[2], int);
using StatVfsFn = int (*)(const char*, struct statvfs*);
#if defined(__LP64__)
using StatVfs64 = struct statvfs;
#else
using StatVfs64 = struct statvfs64;
#endif
using StatVfs64Fn = int (*)(const char*, StatVfs64*);
using InotifyAddWatchFn = int (*)(int, const char*, uint32_t);
using BinderGetCallingUidFn = jint (*)();
using BinderClearIdentityFn = jlong (*)();
using BinderRestoreIdentityFn = void (*)(jlong);
using DlopenFn = void* (*)(const char*, int);
using AndroidDlopenExtFn = void* (*)(const char*, int, const android_dlextinfo*);

using FuseRequest = void*;
struct FuseContext {
    uid_t uid;
    gid_t gid;
    pid_t pid;
    mode_t umask;
};
struct FuseEntryParam;
struct FuseFileInfo;
struct FuseBufferVector;
using FuseReqUserdataFn = void* (*)(FuseRequest);
using FuseReqContextFn = const FuseContext* (*)(FuseRequest);
using FuseReplyErrFn = int (*)(FuseRequest, int);
using FuseReplyEntryFn = int (*)(FuseRequest, const FuseEntryParam*);
using FuseReplyAttrFn = int (*)(FuseRequest, const struct stat*, double);
using FuseReplyOpenFn = int (*)(FuseRequest, const FuseFileInfo*);
using FuseReplyWriteFn = int (*)(FuseRequest, size_t);
using FuseReplyBufferFn = int (*)(FuseRequest, const char*, size_t);
using FuseReplyDataFn = int (*)(FuseRequest, FuseBufferVector*, int);
using FuseReplyStatVfsFn = int (*)(FuseRequest, const struct statvfs*);
using FuseReplyCreateFn = int (*)(FuseRequest, const FuseEntryParam*,
                                  const FuseFileInfo*);
using FuseReplyNoneFn = void (*)(FuseRequest);

struct RuntimePolicySnapshot final {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    policy_v6_view::PolicyV6View policy;
    storage_path_adapter::PolicyScope scopes[kMaxScopes]{};
    uint32_t scope_count = 0;
    AdmissionDomain domain = AdmissionDomain::kAppPath;
    CapabilitySnapshot capabilities{};

};

void ReleaseRuntimePolicy(RuntimePolicySnapshot* snapshot) {
    if (snapshot == nullptr) return;
    free(snapshot->bytes);
    free(snapshot);
}

PolicySnapshotDomain<RuntimePolicySnapshot, 128> g_app_policy_domain(
    ReleaseRuntimePolicy);
PolicySnapshotDomain<RuntimePolicySnapshot, 256> g_provider_policy_domain(
    ReleaseRuntimePolicy);
PolicySnapshotDomainBase<RuntimePolicySnapshot>* g_policy_domain = nullptr;
AdmissionDomain g_domain = AdmissionDomain::kAppPath;
IdentityMode g_identity_mode = IdentityMode::kProcessUid;
CapabilitySnapshot g_capabilities{};
uint64_t g_snapshot_generation = 0;
ResolverProbeCache g_resolver_probe;
OpenFn g_open = nullptr;
OpenFn g_open64 = nullptr;
OpenAtFn g_openat = nullptr;
OpenAtFn g_openat64 = nullptr;
StatFn g_stat = nullptr;
StatFn g_lstat = nullptr;
FstatAtFn g_fstatat = nullptr;
Stat64Fn g_stat64 = nullptr;
Stat64Fn g_lstat64 = nullptr;
FstatAt64Fn g_fstatat64 = nullptr;
AccessFn g_access = nullptr;
FaccessAtFn g_faccessat = nullptr;
OpenDirFn g_opendir = nullptr;
MkdirFn g_mkdir = nullptr;
MkdirAtFn g_mkdirat = nullptr;
UnlinkFn g_unlink = nullptr;
UnlinkAtFn g_unlinkat = nullptr;
RemoveFn g_remove = nullptr;
RmdirFn g_rmdir = nullptr;
RenameFn g_rename = nullptr;
RenameAtFn g_renameat = nullptr;
RenameAtFn g_renameat2 = nullptr;
LinkFn g_link = nullptr;
LinkAtFn g_linkat = nullptr;
ReadlinkFn g_readlink = nullptr;
RealpathFn g_realpath = nullptr;
ChmodFn g_chmod = nullptr;
FchmodAtFn g_fchmodat = nullptr;
ChownFn g_chown = nullptr;
LchownFn g_lchown = nullptr;
FchownAtFn g_fchownat = nullptr;
TruncateFn g_truncate = nullptr;
Truncate64Fn g_truncate64 = nullptr;
UtimensAtFn g_utimensat = nullptr;
StatVfsFn g_statvfs = nullptr;
StatVfs64Fn g_statvfs64 = nullptr;
InotifyAddWatchFn g_inotify_add_watch = nullptr;
BinderGetCallingUidFn g_binder_get_calling_uid = nullptr;
BinderClearIdentityFn g_binder_clear_identity = nullptr;
BinderRestoreIdentityFn g_binder_restore_identity = nullptr;
DlopenFn g_dlopen = nullptr;
AndroidDlopenExtFn g_android_dlopen_ext = nullptr;
FuseReqUserdataFn g_fuse_req_userdata = nullptr;
FuseReqContextFn g_fuse_req_context = nullptr;
FuseReplyErrFn g_fuse_reply_err = nullptr;
FuseReplyEntryFn g_fuse_reply_entry = nullptr;
FuseReplyAttrFn g_fuse_reply_attr = nullptr;
FuseReplyOpenFn g_fuse_reply_open = nullptr;
FuseReplyWriteFn g_fuse_reply_write = nullptr;
FuseReplyBufferFn g_fuse_reply_buffer = nullptr;
FuseReplyDataFn g_fuse_reply_data = nullptr;
FuseReplyStatVfsFn g_fuse_reply_statvfs = nullptr;
FuseReplyCreateFn g_fuse_reply_create = nullptr;
FuseReplyNoneFn g_fuse_reply_none = nullptr;

pthread_key_t g_thread_state_key{};
uint32_t g_thread_state_key_ready = 0;
uint32_t g_rewrite_log_count = 0;
uint32_t g_hooks_enabled = 0;
uint32_t g_fuse_install_state = 0;
uint32_t g_provenance_transactions_available = 0;
uint64_t g_transaction_nonce = 0;
uint64_t g_transaction_sequence = 0;
uint32_t g_transaction_atfork_state = 0;
zygisk::Api* g_api = nullptr;

void ResetTransactionStateAfterFork() {
    g_transaction_nonce = 0;
    g_transaction_sequence = 0;
}

bool InitializeTransactionAtFork() {
    uint32_t state = __atomic_load_n(
        &g_transaction_atfork_state, __ATOMIC_ACQUIRE);
    if (state != 0) return state == 1;
    const bool ready = pthread_atfork(
        nullptr, nullptr, ResetTransactionStateAfterFork) == 0;
    __atomic_store_n(&g_transaction_atfork_state, ready ? 1u : 2u,
                     __ATOMIC_RELEASE);
    return ready;
}

void DestroyThreadState(void* value) {
    auto* state = static_cast<CallerUidContext*>(value);
    if (state != nullptr && state->policy_domain != nullptr) {
        static_cast<PolicySnapshotDomainBase<RuntimePolicySnapshot>*>(
            state->policy_domain)->Release(state);
    }
    free(value);
}

bool InitializeThreadStateKey() {
    if (__atomic_load_n(&g_thread_state_key_ready, __ATOMIC_ACQUIRE) != 0) return true;
    if (pthread_key_create(&g_thread_state_key, DestroyThreadState) != 0) return false;
    __atomic_store_n(&g_thread_state_key_ready, 1, __ATOMIC_RELEASE);
    return true;
}

CallerUidContext* GetThreadState() {
    if (__atomic_load_n(&g_thread_state_key_ready, __ATOMIC_ACQUIRE) == 0) return nullptr;
    auto* state = static_cast<CallerUidContext*>(pthread_getspecific(g_thread_state_key));
    if (state != nullptr) return state;
    state = static_cast<CallerUidContext*>(calloc(1, sizeof(CallerUidContext)));
    if (state == nullptr) return nullptr;
    state->saved_binder_uid = -1;
    state->fuse_uid = -1;
    if (pthread_setspecific(g_thread_state_key, state) != 0) {
        free(state);
        return nullptr;
    }
    return state;
}

class HookGuard final {
public:
    HookGuard()
        : state_(GetThreadState()),
          outer_(state_ != nullptr && !state_->in_path_hook) {
        if (outer_) state_->in_path_hook = true;
    }
    ~HookGuard() { if (outer_) state_->in_path_hook = false; }
    bool outer() const { return outer_; }
private:
    CallerUidContext* state_;
    bool outer_;
};

int32_t RawCallingUid() {
    return g_binder_get_calling_uid != nullptr
        ? static_cast<int32_t>(g_binder_get_calling_uid()) : -1;
}

int32_t EffectiveCallingUid() {
    if (g_identity_mode == IdentityMode::kProcessUid) {
        return static_cast<int32_t>(getuid());
    }
    return EffectiveCallerUid(GetThreadState(), RawCallingUid(),
                              static_cast<int32_t>(getuid()));
}

int64_t HookedClearCallingIdentity() {
    if (g_binder_clear_identity == nullptr) return 0;
    BeginBinderIdentityClear(GetThreadState(), RawCallingUid(),
                             static_cast<int32_t>(getuid()));
    return g_binder_clear_identity();
}

void HookedRestoreCallingIdentity(int64_t token) {
    if (g_binder_restore_identity == nullptr) return;
    g_binder_restore_identity(token);
    EndBinderIdentityClear(GetThreadState());
}

bool InstallBinderIdentityHooks(zygisk::Api* api, JNIEnv* env,
                                bool* attempted) {
    JNINativeMethod methods[] = {
        {const_cast<char*>("getCallingUid"), const_cast<char*>("()I"),
         reinterpret_cast<void*>(RawCallingUid)},
        {const_cast<char*>("clearCallingIdentity"), const_cast<char*>("()J"),
         reinterpret_cast<void*>(HookedClearCallingIdentity)},
        {const_cast<char*>("restoreCallingIdentity"), const_cast<char*>("(J)V"),
         reinterpret_cast<void*>(HookedRestoreCallingIdentity)},
    };
    api->hookJniNativeMethods(env, "android/os/Binder", methods, 3);
    const bool get_uid_ready = IsResolvedJniHook(
        methods[0].fnPtr, reinterpret_cast<void*>(RawCallingUid));
    const bool clear_ready = IsResolvedJniHook(
        methods[1].fnPtr, reinterpret_cast<void*>(HookedClearCallingIdentity));
    const bool restore_ready = IsResolvedJniHook(
        methods[2].fnPtr, reinterpret_cast<void*>(HookedRestoreCallingIdentity));
    g_binder_get_calling_uid = get_uid_ready
        ? reinterpret_cast<BinderGetCallingUidFn>(methods[0].fnPtr) : nullptr;
    g_binder_clear_identity = clear_ready
        ? reinterpret_cast<BinderClearIdentityFn>(methods[1].fnPtr) : nullptr;
    g_binder_restore_identity = restore_ready
        ? reinterpret_cast<BinderRestoreIdentityFn>(methods[2].fnPtr) : nullptr;
    *attempted = true;
    LOGI("provider Binder JNI hooks: get_uid=%d clear=%d restore=%d",
         get_uid_ready ? 1 : 0, clear_ready ? 1 : 0,
         restore_ready ? 1 : 0);
    return get_uid_ready && clear_ready && restore_ready;
}

bool ResolveAtPath(int dirfd, const char* path, char* output, size_t capacity) {
    if (path == nullptr || path[0] == '\0') return false;
    if (path[0] == '/') {
        const size_t size = strlen(path) + 1;
        if (size > capacity) return false;
        memcpy(output, path, size);
        return true;
    }
    char directory[PATH_MAX]{};
    if (dirfd == AT_FDCWD) {
        if (getcwd(directory, sizeof(directory)) == nullptr) return false;
    } else {
        char link[64]{};
        if (snprintf(link, sizeof(link), "/proc/self/fd/%d", dirfd) <= 0) return false;
        const ssize_t count = readlink(link, directory, sizeof(directory) - 1);
        if (count <= 0) return false;
        directory[count] = '\0';
    }
    const int written = snprintf(output, capacity, "%s/%s", directory, path);
    return written > 0 && static_cast<size_t>(written) < capacity;
}

storage_path_adapter::RewriteResult RewriteAtPath(
        const RuntimePolicySnapshot& snapshot,
        int dirfd, const char* path, OperationMask operation,
        uint8_t object_type, char* absolute, char* output,
        int32_t* caller_uid) {
    storage_path_adapter::RewriteResult result;
    if (!ResolveAtPath(dirfd, path, absolute, PATH_MAX)) return result;
    const int32_t uid = EffectiveCallingUid();
    *caller_uid = uid;
    policy_pattern_runtime::MatchScratch scratch;
    return storage_path_adapter::Rewrite(
        snapshot.policy, snapshot.scopes, snapshot.scope_count, uid, absolute,
        snapshot.domain, operation, object_type, snapshot.capabilities, &scratch,
        output, PATH_MAX);
}

void LogRewrite(const char* operation, int32_t uid,
                const char* from, const char* to) {
    if (__atomic_fetch_add(&g_rewrite_log_count, 1, __ATOMIC_RELAXED) >= 64) return;
    LOGI("provider virtual path: op=%s caller_uid=%d from=%s to=%s",
         operation, uid, from, to);
}

template <size_t Capacity>
bool CopyWireText(char (&output)[Capacity], const char* input, size_t size) {
    if (input == nullptr || size == 0 || size >= Capacity) return false;
    memcpy(output, input, size);
    output[size] = '\0';
    return true;
}

template <size_t Capacity>
bool CopyLogicalRelative(char (&output)[Capacity], const char* root,
                         size_t root_size, const char* relative,
                         size_t relative_size) {
    if (root == nullptr || relative == nullptr || root_size == 0
        || relative_size == 0 || root_size + 1 + relative_size >= Capacity) {
        return false;
    }
    memcpy(output, root, root_size);
    output[root_size] = '/';
    memcpy(output + root_size + 1, relative, relative_size);
    output[root_size + 1 + relative_size] = '\0';
    return true;
}

bool BuildProvenanceRecord(
        const storage_path_adapter::RewriteResult& rewrite, int32_t uid,
        const char* logical_path, const char* target_path,
        provenance_protocol::Record* output) {
    if (output == nullptr || rewrite.rule_id == 0) return false;
    storage_path_adapter::LogicalPath logical;
    storage_path_adapter::LogicalPath target;
    if (!storage_path_adapter::ParseLogicalPath(logical_path, &logical)
        || !storage_path_adapter::ParseLogicalPath(target_path, &target)) return false;
    *output = {};
    output->caller_uid = uid;
    output->user_id = rewrite.user_id;
    output->identity_epoch = rewrite.content_generation;
    output->rule_id = rewrite.rule_id;
    output->content_generation = rewrite.content_generation;
    output->created_plan_generation = rewrite.plan_generation;
    output->bound_plan_generation = rewrite.plan_generation;
    char storage_root[provenance_protocol::kVolumeCapacity]{};
    const int root_size = snprintf(storage_root, sizeof(storage_root),
                                   "emulated:%u", rewrite.user_id);
    return root_size > 0 && static_cast<size_t>(root_size) < sizeof(storage_root)
        && CopyWireText(output->storage_root, storage_root,
                        static_cast<size_t>(root_size))
        && CopyLogicalRelative(output->logical_source, logical.root,
                               logical.root_size, logical.relative,
                               logical.relative_size)
        && CopyLogicalRelative(output->target_relative, target.root,
                               target.root_size, target.relative,
                               target.relative_size);
}

bool CallProvenance(const provenance_protocol::Request& request,
                    provenance_protocol::Response* response) {
    if (g_api == nullptr || response == nullptr) return false;
    const int socket = g_api->connectCompanion();
    if (socket < 0) return false;
    timeval timeout{0, 250000};
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const bool ok = send(socket, &request, sizeof(request), MSG_NOSIGNAL)
            == static_cast<ssize_t>(sizeof(request))
        && recv(socket, response, sizeof(*response), MSG_WAITALL)
            == static_cast<ssize_t>(sizeof(*response))
        && response->magic == provenance_protocol::kMagic
        && response->version == provenance_protocol::kVersion;
    close(socket);
    return ok;
}

provenance_protocol::Request MakeProvenanceRequest(
        provenance_protocol::Command command,
        const provenance_protocol::Record& record, int32_t uid) {
    provenance_protocol::Request request;
    request.command = command;
    request.record = record;
    uint64_t nonce = __atomic_load_n(&g_transaction_nonce, __ATOMIC_ACQUIRE);
    if (nonce == 0) {
        uint64_t candidate = 0;
        if (syscall(SYS_getrandom, &candidate, sizeof(candidate), 0)
                != static_cast<ssize_t>(sizeof(candidate))
            || candidate == 0) {
            timespec now{};
            clock_gettime(CLOCK_BOOTTIME, &now);
            candidate = (static_cast<uint64_t>(now.tv_sec) << 32)
                ^ static_cast<uint64_t>(now.tv_nsec)
                ^ (static_cast<uint64_t>(static_cast<uint32_t>(getpid())) << 16)
                ^ static_cast<uint32_t>(uid);
            if (candidate == 0) candidate = 1;
        }
        uint64_t empty = 0;
        __atomic_compare_exchange_n(&g_transaction_nonce, &empty, candidate,
                                    false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
        nonce = __atomic_load_n(&g_transaction_nonce, __ATOMIC_ACQUIRE);
    }
    request.transaction_high = nonce;
    request.transaction_low = __atomic_add_fetch(
        &g_transaction_sequence, UINT64_C(1), __ATOMIC_RELAXED);
    return request;
}

struct KernelFileHandle {
    uint32_t handle_bytes;
    int32_t handle_type;
    uint8_t bytes[provenance_protocol::kIdentityHandleCapacity];
};

bool CaptureIdentity(int fd, provenance_protocol::Identity* output) {
    if (fd < 0 || output == nullptr) return false;
    struct statx value {};
    if (syscall(SYS_statx, fd, "", AT_EMPTY_PATH | AT_STATX_SYNC_AS_STAT,
                STATX_INO | STATX_BTIME | STATX_TYPE, &value) != 0
        || (value.stx_mask & STATX_INO) == 0
        || value.stx_ino == 0) return false;
    *output = {};
    output->inode = value.stx_ino;
    output->birth_seconds = value.stx_btime.tv_sec;
    output->birth_nanoseconds = value.stx_btime.tv_nsec;
    output->object_type = S_ISDIR(value.stx_mode) ? 2 : 1;
    const int size = snprintf(output->volume, sizeof(output->volume),
                              "dev:%u:%u", value.stx_dev_major,
                              value.stx_dev_minor);
    if (size <= 0 || static_cast<size_t>(size) >= sizeof(output->volume)) {
        return false;
    }
    char descriptor_path[64]{};
    const int descriptor_size = snprintf(
        descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d", fd);
    KernelFileHandle handle{};
    handle.handle_bytes = sizeof(handle.bytes);
    int mount_id = 0;
    if (descriptor_size > 0
        && static_cast<size_t>(descriptor_size) < sizeof(descriptor_path)
        && syscall(SYS_name_to_handle_at, AT_FDCWD, descriptor_path,
                   &handle, &mount_id, AT_SYMLINK_FOLLOW) == 0
        && handle.handle_bytes != 0
        && handle.handle_bytes <= sizeof(handle.bytes)) {
        output->kind = provenance_protocol::IdentityKind::kFileHandle;
        output->handle_type = handle.handle_type;
        output->handle_size = static_cast<uint16_t>(handle.handle_bytes);
        memcpy(output->handle, handle.bytes, handle.handle_bytes);
        return true;
    }
    if ((value.stx_mask & STATX_BTIME) == 0) return false;
    output->kind = provenance_protocol::IdentityKind::kStatxBirthTime;
    return true;
}

bool SendTransactionStep(provenance_protocol::Request* request,
                         provenance_protocol::Command command) {
    request->command = command;
    provenance_protocol::Response response;
    return CallProvenance(*request, &response)
        && response.provenance_error == provenance_protocol::Error::kNone;
}

bool ResolveOwnedRecord(const provenance_protocol::Record& record,
                        int32_t uid) {
    const auto request = MakeProvenanceRequest(
        provenance_protocol::Command::kResolve, record, uid);
    provenance_protocol::Response response;
    return CallProvenance(request, &response)
        && response.provenance_error == provenance_protocol::Error::kNone
        && response.resolve_status == provenance_protocol::ResolveStatus::kUnique
        && strcmp(response.logical_source, record.logical_source) == 0;
}

bool ProvenanceTransactionsAvailable() {
    return __atomic_load_n(
        &g_provenance_transactions_available, __ATOMIC_ACQUIRE) != 0;
}

template <typename T>
T FailureValue(T*) { return static_cast<T>(-1); }

template <typename T>
T* FailureValue(T**) { return nullptr; }

template <typename Function>
auto WithPath(const char* operation_name, OperationMask operation,
              uint8_t object_type, int dirfd, const char* path,
              Function function) -> decltype(function(dirfd, path)) {
    if (__atomic_load_n(&g_hooks_enabled, __ATOMIC_ACQUIRE) == 0) {
        return function(dirfd, path);
    }
    HookGuard guard;
    if (!guard.outer() || path == nullptr) return function(dirfd, path);
    if (g_policy_domain == nullptr) return function(dirfd, path);
    auto snapshot_guard = g_policy_domain->Acquire(GetThreadState());
    if (!snapshot_guard) return function(dirfd, path);
    const int saved_errno = errno;
    char absolute[PATH_MAX]{};
    char rewritten[PATH_MAX]{};
    int32_t uid = -1;
    const auto rewrite = RewriteAtPath(
        *snapshot_guard, dirfd, path, operation, object_type, absolute,
        rewritten, &uid);
    if (rewrite.disposition == storage_path_adapter::RewriteDisposition::kDeny) {
        errno = EACCES;
        using Return = decltype(function(dirfd, path));
        return FailureValue(static_cast<Return*>(nullptr));
    }
    if (rewrite.disposition != storage_path_adapter::RewriteDisposition::kRedirect) {
        errno = saved_errno;
        return function(dirfd, path);
    }
    LogRewrite(operation_name, uid, absolute, rewritten);
    errno = saved_errno;
    const bool create_parents = (operation & (kOperationCreate | kOperationMkdir)) != 0;
    SecureResolvedPath pinned = ResolveStoragePathParent(
        rewritten, create_parents, &g_resolver_probe);
    if (!pinned.ok()) {
        errno = pinned.error == 0 ? EACCES : pinned.error;
        using Return = decltype(function(dirfd, path));
        return FailureValue(static_cast<Return*>(nullptr));
    }
    char pinned_path[PATH_MAX]{};
    if (!BuildPinnedProcPath(pinned, pinned_path, sizeof(pinned_path))) {
        CloseSecureResolvedPath(&pinned);
        errno = ENAMETOOLONG;
        using Return = decltype(function(dirfd, path));
        return FailureValue(static_cast<Return*>(nullptr));
    }
    using Return = decltype(function(dirfd, path));
    if constexpr (__is_same(Return, int)) {
        const bool transactional_create = ProvenanceTransactionsAvailable()
            && rewrite.reverse_mode == 2
            && (operation & kOperationMkdir) != 0;
        if (transactional_create) {
            provenance_protocol::Record record;
            if (!BuildProvenanceRecord(
                    rewrite, uid, absolute, rewritten, &record)) {
                CloseSecureResolvedPath(&pinned);
                errno = EIO;
                return -1;
            }
            const int existing = openat(
                pinned.parent_fd, pinned.basename,
                O_PATH | O_NOFOLLOW | O_CLOEXEC);
            if (existing >= 0) {
                close(existing);
                CloseSecureResolvedPath(&pinned);
                errno = EEXIST;
                return -1;
            }
            if (errno != ENOENT) {
                const int call_errno = errno;
                CloseSecureResolvedPath(&pinned);
                errno = call_errno;
                return -1;
            }
            provenance_protocol::Request transaction = MakeProvenanceRequest(
                provenance_protocol::Command::kPrepareCreate, record, uid);
            provenance_protocol::Response response;
            if (!CallProvenance(transaction, &response)
                || response.provenance_error
                    != provenance_protocol::Error::kNone) {
                const bool collision = response.provenance_error
                    == provenance_protocol::Error::kRouteBusy;
                CloseSecureResolvedPath(&pinned);
                errno = collision ? EEXIST : EIO;
                return -1;
            }
            const int value = function(AT_FDCWD, pinned_path);
            const int call_errno = errno;
            const int created = value == 0 ? openat(
                pinned.parent_fd, pinned.basename,
                O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
            const bool committed = created >= 0
                && CaptureIdentity(created, &transaction.record.identity)
                && SendTransactionStep(
                    &transaction, provenance_protocol::Command::kMaterialize)
                && SendTransactionStep(
                    &transaction, provenance_protocol::Command::kCommit);
            if (created >= 0) close(created);
            if (value != 0) {
                SendTransactionStep(&transaction,
                                    provenance_protocol::Command::kAbort);
            } else if (!committed) {
                unlinkat(pinned.parent_fd, pinned.basename, AT_REMOVEDIR);
                SendTransactionStep(&transaction,
                                    provenance_protocol::Command::kAbort);
                CloseSecureResolvedPath(&pinned);
                errno = EIO;
                return -1;
            }
            CloseSecureResolvedPath(&pinned);
            errno = call_errno;
            return value;
        }
        const bool transactional_delete = ProvenanceTransactionsAvailable()
            && rewrite.reverse_mode == 2
            && (operation & (kOperationUnlink | kOperationRmdir)) != 0;
        if (transactional_delete) {
            provenance_protocol::Record record;
            const int target = openat(pinned.parent_fd, pinned.basename,
                                      O_PATH | O_NOFOLLOW | O_CLOEXEC);
            const bool owned = target >= 0
                && BuildProvenanceRecord(
                    rewrite, uid, absolute, rewritten, &record)
                && CaptureIdentity(target, &record.identity)
                && ResolveOwnedRecord(record, uid);
            if (target >= 0) close(target);
            if (!owned) {
                CloseSecureResolvedPath(&pinned);
                errno = EXDEV;
                return -1;
            }
            provenance_protocol::Request transaction = MakeProvenanceRequest(
                provenance_protocol::Command::kPrepareDelete, record, uid);
            transaction.previous = record;
            provenance_protocol::Response response;
            if (!CallProvenance(transaction, &response)
                || response.provenance_error
                    != provenance_protocol::Error::kNone) {
                CloseSecureResolvedPath(&pinned);
                errno = EIO;
                return -1;
            }
            const int value = function(AT_FDCWD, pinned_path);
            const int call_errno = errno;
            if (value != 0) {
                SendTransactionStep(&transaction,
                                    provenance_protocol::Command::kAbort);
            } else if (!SendTransactionStep(
                           &transaction,
                           provenance_protocol::Command::kCommit)) {
                LOGE("provenance delete commit failed: uid=%d rule=%llu",
                     uid, static_cast<unsigned long long>(rewrite.rule_id));
            }
            CloseSecureResolvedPath(&pinned);
            errno = call_errno;
            return value;
        }
    }
    const auto value = function(AT_FDCWD, pinned_path);
    const int call_errno = errno;
    CloseSecureResolvedPath(&pinned);
    errno = call_errno;
    return value;
}

template <typename Function>
int WithOpenPath(const char* operation_name, int dirfd, const char* path,
                 int flags, Function function) {
    if (__atomic_load_n(&g_hooks_enabled, __ATOMIC_ACQUIRE) == 0) {
        return function(dirfd, path, flags);
    }
    HookGuard guard;
    if (!guard.outer() || path == nullptr || g_policy_domain == nullptr) {
        return function(dirfd, path, flags);
    }
    auto snapshot_guard = g_policy_domain->Acquire(GetThreadState());
    if (!snapshot_guard) return function(dirfd, path, flags);
    const int saved_errno = errno;
    char absolute[PATH_MAX]{};
    char rewritten[PATH_MAX]{};
    int32_t uid = -1;
    const OperationMask operation = path_hook_contract::OpenOperation(flags);
    const auto rewrite = RewriteAtPath(
        *snapshot_guard, dirfd, path, operation, 1, absolute, rewritten, &uid);
    if (rewrite.disposition == storage_path_adapter::RewriteDisposition::kDeny) {
        errno = EACCES;
        return -1;
    }
    if (rewrite.disposition != storage_path_adapter::RewriteDisposition::kRedirect) {
        errno = saved_errno;
        return function(dirfd, path, flags);
    }
    LogRewrite(operation_name, uid, absolute, rewritten);
    SecureResolvedPath pinned = ResolveStoragePathParent(
        rewritten, (flags & O_CREAT) != 0, &g_resolver_probe);
    if (!pinned.ok()) {
        errno = pinned.error == 0 ? EACCES : pinned.error;
        return -1;
    }
    char pinned_path[PATH_MAX]{};
    if (!BuildPinnedProcPath(pinned, pinned_path, sizeof(pinned_path))) {
        CloseSecureResolvedPath(&pinned);
        errno = ENAMETOOLONG;
        return -1;
    }
    const auto& domain_capabilities = snapshot_guard->capabilities.domains[
        static_cast<uint8_t>(snapshot_guard->domain)];
    const OperationMask provenance_operations =
        domain_capabilities.observed_operations
        | (ProvenanceTransactionsAvailable()
            ? kOperationReverseMapping : OperationMask{0});
    const auto open_plan = path_hook_contract::PlanRedirectOpen(
        rewrite, provenance_operations, flags);
    if (!open_plan.coordinate_provenance) {
        errno = saved_errno;
        const int value = function(AT_FDCWD, pinned_path,
                                   open_plan.effective_flags);
        const int call_errno = errno;
        CloseSecureResolvedPath(&pinned);
        errno = call_errno;
        return value;
    }

    provenance_protocol::Record record;
    if (!BuildProvenanceRecord(rewrite, uid, absolute, rewritten, &record)) {
        CloseSecureResolvedPath(&pinned);
        errno = EIO;
        return -1;
    }
    const int existing = openat(pinned.parent_fd, pinned.basename,
                                O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (existing >= 0) {
        const bool identified = CaptureIdentity(existing, &record.identity);
        close(existing);
        provenance_protocol::Request resolve = MakeProvenanceRequest(
            provenance_protocol::Command::kResolve, record, uid);
        provenance_protocol::Response response;
        const bool same_owner = identified && CallProvenance(resolve, &response)
            && response.provenance_error == provenance_protocol::Error::kNone
            && response.resolve_status
                == provenance_protocol::ResolveStatus::kUnique
            && strcmp(response.logical_source, record.logical_source) == 0;
        if (!same_owner) {
            CloseSecureResolvedPath(&pinned);
            errno = EEXIST;
            return -1;
        }
        errno = saved_errno;
        const int value = function(AT_FDCWD, pinned_path, flags);
        const int call_errno = errno;
        CloseSecureResolvedPath(&pinned);
        errno = call_errno;
        return value;
    }
    if (errno != ENOENT) {
        const int call_errno = errno;
        CloseSecureResolvedPath(&pinned);
        errno = call_errno;
        return -1;
    }

    provenance_protocol::Request transaction = MakeProvenanceRequest(
        provenance_protocol::Command::kPrepareCreate, record, uid);
    provenance_protocol::Response response;
    if (!CallProvenance(transaction, &response)
        || response.provenance_error != provenance_protocol::Error::kNone) {
        const bool collision = response.provenance_error
            == provenance_protocol::Error::kRouteBusy;
        CloseSecureResolvedPath(&pinned);
        errno = collision ? EEXIST : EIO;
        return -1;
    }
    errno = saved_errno;
    const int value = function(AT_FDCWD, pinned_path, flags | O_EXCL);
    const int call_errno = errno;
    if (value < 0) {
        SendTransactionStep(&transaction,
                            provenance_protocol::Command::kAbort);
        CloseSecureResolvedPath(&pinned);
        errno = call_errno;
        return -1;
    }
    const bool identified = CaptureIdentity(value, &transaction.record.identity);
    const bool committed = identified
        && SendTransactionStep(&transaction,
                               provenance_protocol::Command::kMaterialize)
        && SendTransactionStep(&transaction,
                               provenance_protocol::Command::kCommit);
    if (!committed) {
        close(value);
        unlinkat(pinned.parent_fd, pinned.basename, 0);
        SendTransactionStep(&transaction,
                            provenance_protocol::Command::kAbort);
        CloseSecureResolvedPath(&pinned);
        errno = EIO;
        return -1;
    }
    CloseSecureResolvedPath(&pinned);
    errno = call_errno;
    return value;
}

int HookedOpenCommon(OpenFn original, const char* operation,
                     const char* path, int flags, mode_t mode) {
    if (original == nullptr) { errno = ENOSYS; return -1; }
    return WithOpenPath(operation, AT_FDCWD, path, flags,
        [&](int, const char* final_path, int final_flags) {
            return original(final_path, final_flags, mode);
        });
}

int HookedOpen(const char* path, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list args; va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int)); va_end(args);
    }
    return HookedOpenCommon(g_open, "open", path, flags, mode);
}

int HookedOpen64(const char* path, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list args; va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int)); va_end(args);
    }
    return HookedOpenCommon(g_open64 != nullptr ? g_open64 : g_open,
                            "open64", path, flags, mode);
}

int HookedOpenAtCommon(OpenAtFn original, const char* operation, int dirfd,
                       const char* path, int flags, mode_t mode) {
    if (original == nullptr) { errno = ENOSYS; return -1; }
    return WithOpenPath(operation, dirfd, path, flags,
        [&](int final_dirfd, const char* final_path, int final_flags) {
            return original(final_dirfd, final_path, final_flags, mode);
        });
}

int HookedOpenAt(int dirfd, const char* path, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list args; va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int)); va_end(args);
    }
    return HookedOpenAtCommon(g_openat, "openat", dirfd, path, flags, mode);
}

int HookedOpenAt64(int dirfd, const char* path, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list args; va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int)); va_end(args);
    }
    return HookedOpenAtCommon(g_openat64 != nullptr ? g_openat64 : g_openat,
                              "openat64", dirfd, path, flags, mode);
}

int HookedStat(const char* path, struct stat* value) {
    return WithPath("stat", kOperationLookupStat, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_stat(final_path, value); });
}

int HookedLstat(const char* path, struct stat* value) {
    return WithPath("lstat", kOperationLookupStat, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_lstat(final_path, value); });
}

int HookedFstatAt(int dirfd, const char* path, struct stat* value, int flags) {
    return WithPath("fstatat", kOperationLookupStat, 1, dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_fstatat(final_dirfd, final_path, value, flags);
        });
}

int HookedStat64(const char* path, Stat64* value) {
    return WithPath("stat64", kOperationLookupStat, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_stat64(final_path, value); });
}

int HookedLstat64(const char* path, Stat64* value) {
    return WithPath("lstat64", kOperationLookupStat, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_lstat64(final_path, value); });
}

int HookedFstatAt64(int dirfd, const char* path, Stat64* value, int flags) {
    return WithPath("fstatat64", kOperationLookupStat, 1, dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_fstatat64(final_dirfd, final_path, value, flags);
        });
}

int HookedAccess(const char* path, int mode) {
    return WithPath("access", kOperationAccess, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_access(final_path, mode); });
}

int HookedFaccessAt(int dirfd, const char* path, int mode, int flags) {
    return WithPath("faccessat", kOperationAccess, 1, dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_faccessat(final_dirfd, final_path, mode, flags);
        });
}

DIR* HookedOpenDir(const char* path) {
    return WithPath("opendir", kOperationDirectoryIterate, 2, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_opendir(final_path); });
}

int HookedMkdir(const char* path, mode_t mode) {
    return WithPath("mkdir", kOperationMkdir, 2, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_mkdir(final_path, mode); });
}

int HookedMkdirAt(int dirfd, const char* path, mode_t mode) {
    return WithPath("mkdirat", kOperationMkdir, 2, dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_mkdirat(final_dirfd, final_path, mode);
        });
}

int HookedUnlink(const char* path) {
    return WithPath("unlink", kOperationUnlink, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_unlink(final_path); });
}

int HookedUnlinkAt(int dirfd, const char* path, int flags) {
    return WithPath("unlinkat", (flags & AT_REMOVEDIR) != 0
        ? kOperationRmdir : kOperationUnlink, (flags & AT_REMOVEDIR) != 0 ? 2 : 1,
        dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_unlinkat(final_dirfd, final_path, flags);
        });
}

int HookedRemove(const char* path) {
    return WithPath("remove", kOperationUnlink, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_remove(final_path); });
}

int HookedRmdir(const char* path) {
    return WithPath("rmdir", kOperationRmdir, 2, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_rmdir(final_path); });
}

template <typename Function>
int WithTwoPaths(const char* operation, OperationMask operation_mask,
                 int old_dirfd, const char* old_path,
                 int new_dirfd, const char* new_path, unsigned mutation_flags,
                 Function function) {
    if (__atomic_load_n(&g_hooks_enabled, __ATOMIC_ACQUIRE) == 0) {
        return function(old_dirfd, old_path, new_dirfd, new_path);
    }
    HookGuard guard;
    if (!guard.outer() || old_path == nullptr || new_path == nullptr) {
        return function(old_dirfd, old_path, new_dirfd, new_path);
    }
    if (g_policy_domain == nullptr) {
        return function(old_dirfd, old_path, new_dirfd, new_path);
    }
    auto snapshot_guard = g_policy_domain->Acquire(GetThreadState());
    if (!snapshot_guard) {
        return function(old_dirfd, old_path, new_dirfd, new_path);
    }
    char old_absolute[PATH_MAX]{}, old_rewritten[PATH_MAX]{};
    char new_absolute[PATH_MAX]{}, new_rewritten[PATH_MAX]{};
    int32_t old_uid = -1;
    int32_t new_uid = -1;
    const auto old_result = RewriteAtPath(
        *snapshot_guard, old_dirfd, old_path, operation_mask, 1,
        old_absolute, old_rewritten, &old_uid);
    const auto new_result = RewriteAtPath(
        *snapshot_guard, new_dirfd, new_path, operation_mask, 1,
        new_absolute, new_rewritten, &new_uid);
    const auto old_plan = path_hook_contract::PlanOperand(
        old_path, old_rewritten, old_result);
    const auto new_plan = path_hook_contract::PlanOperand(
        new_path, new_rewritten, new_result);
    const auto plan = path_hook_contract::PlanRename(old_plan, new_plan);
    if (plan.disposition == path_hook_contract::Disposition::kReject) {
        errno = plan.error_number;
        return -1;
    }
    const bool old_changed = old_result.disposition
        == storage_path_adapter::RewriteDisposition::kRedirect;
    const bool new_changed = new_result.disposition
        == storage_path_adapter::RewriteDisposition::kRedirect;
    const int32_t uid = old_changed ? old_uid : new_uid;
    if (old_changed) LogRewrite(operation, uid, old_absolute, old_rewritten);
    if (new_changed) LogRewrite(operation, uid, new_absolute, new_rewritten);
    SecureResolvedPath old_pinned;
    SecureResolvedPath new_pinned;
    if (old_changed) old_pinned = ResolveStoragePathParent(
        plan.first, false, &g_resolver_probe);
    if (new_changed) new_pinned = ResolveStoragePathParent(
        plan.second, true, &g_resolver_probe);
    if ((old_changed && !old_pinned.ok()) || (new_changed && !new_pinned.ok())) {
        const int error = old_changed && !old_pinned.ok()
            ? old_pinned.error : new_pinned.error;
        CloseSecureResolvedPath(&old_pinned);
        CloseSecureResolvedPath(&new_pinned);
        errno = error == 0 ? EACCES : error;
        return -1;
    }
    char old_pinned_path[PATH_MAX]{};
    char new_pinned_path[PATH_MAX]{};
    if ((old_changed && !BuildPinnedProcPath(
            old_pinned, old_pinned_path, sizeof(old_pinned_path)))
        || (new_changed && !BuildPinnedProcPath(
            new_pinned, new_pinned_path, sizeof(new_pinned_path)))) {
        CloseSecureResolvedPath(&old_pinned);
        CloseSecureResolvedPath(&new_pinned);
        errno = ENAMETOOLONG;
        return -1;
    }
    const bool transactional_link = ProvenanceTransactionsAvailable()
        && operation_mask == kOperationHardLink
        && new_changed && new_result.reverse_mode == 2;
    if (transactional_link) {
        provenance_protocol::Record candidate;
        bool source_owned = true;
        if (old_result.reverse_mode == 2) {
            provenance_protocol::Record previous;
            const int old_target = openat(
                old_pinned.parent_fd, old_pinned.basename,
                O_PATH | O_NOFOLLOW | O_CLOEXEC);
            source_owned = old_target >= 0
                && BuildProvenanceRecord(
                    old_result, old_uid, old_absolute, old_rewritten, &previous)
                && CaptureIdentity(old_target, &previous.identity)
                && ResolveOwnedRecord(previous, old_uid);
            if (old_target >= 0) close(old_target);
        }
        const bool candidate_ready = BuildProvenanceRecord(
            new_result, new_uid, new_absolute, new_rewritten, &candidate);
        if (!source_owned || !candidate_ready) {
            CloseSecureResolvedPath(&old_pinned);
            CloseSecureResolvedPath(&new_pinned);
            errno = source_owned ? EIO : EXDEV;
            return -1;
        }
        provenance_protocol::Request transaction = MakeProvenanceRequest(
            provenance_protocol::Command::kPrepareCreate, candidate, new_uid);
        provenance_protocol::Response response;
        if (!CallProvenance(transaction, &response)
            || response.provenance_error != provenance_protocol::Error::kNone) {
            const bool collision = response.provenance_error
                == provenance_protocol::Error::kRouteBusy;
            CloseSecureResolvedPath(&old_pinned);
            CloseSecureResolvedPath(&new_pinned);
            errno = collision ? EEXIST : EIO;
            return -1;
        }
        const int value = function(
            AT_FDCWD, old_pinned_path, AT_FDCWD, new_pinned_path);
        const int call_errno = errno;
        const int new_target = value == 0 ? openat(
            new_pinned.parent_fd, new_pinned.basename,
            O_PATH | O_NOFOLLOW | O_CLOEXEC) : -1;
        const bool committed = new_target >= 0
            && CaptureIdentity(new_target, &transaction.record.identity)
            && SendTransactionStep(
                &transaction, provenance_protocol::Command::kMaterialize)
            && SendTransactionStep(
                &transaction, provenance_protocol::Command::kCommit);
        if (new_target >= 0) close(new_target);
        if (value != 0) {
            SendTransactionStep(&transaction,
                                provenance_protocol::Command::kAbort);
        } else if (!committed) {
            unlinkat(new_pinned.parent_fd, new_pinned.basename, 0);
            SendTransactionStep(&transaction,
                                provenance_protocol::Command::kAbort);
            CloseSecureResolvedPath(&old_pinned);
            CloseSecureResolvedPath(&new_pinned);
            errno = EIO;
            return -1;
        }
        CloseSecureResolvedPath(&old_pinned);
        CloseSecureResolvedPath(&new_pinned);
        errno = call_errno;
        return value;
    }
    const bool transactional_rename = ProvenanceTransactionsAvailable()
        && operation_mask == kOperationRename
        && old_changed && new_changed
        && old_result.reverse_mode == 2 && new_result.reverse_mode == 2;
    if (transactional_rename) {
        if ((mutation_flags & ~static_cast<unsigned>(RENAME_NOREPLACE)) != 0) {
            CloseSecureResolvedPath(&old_pinned);
            CloseSecureResolvedPath(&new_pinned);
            errno = EOPNOTSUPP;
            return -1;
        }
        provenance_protocol::Record previous;
        provenance_protocol::Record candidate;
        const int old_target = openat(old_pinned.parent_fd, old_pinned.basename,
                                      O_PATH | O_NOFOLLOW | O_CLOEXEC);
        const bool owned = old_target >= 0
            && BuildProvenanceRecord(
                old_result, old_uid, old_absolute, old_rewritten, &previous)
            && CaptureIdentity(old_target, &previous.identity)
            && ResolveOwnedRecord(previous, old_uid)
            && BuildProvenanceRecord(
                new_result, new_uid, new_absolute, new_rewritten, &candidate);
        if (old_target >= 0) close(old_target);
        if (!owned) {
            CloseSecureResolvedPath(&old_pinned);
            CloseSecureResolvedPath(&new_pinned);
            errno = EXDEV;
            return -1;
        }
        candidate.identity = previous.identity;
        provenance_protocol::Request transaction = MakeProvenanceRequest(
            provenance_protocol::Command::kPrepareRename, candidate, old_uid);
        transaction.previous = previous;
        provenance_protocol::Response response;
        if (!CallProvenance(transaction, &response)
            || response.provenance_error != provenance_protocol::Error::kNone) {
            const bool collision = response.provenance_error
                == provenance_protocol::Error::kRouteBusy;
            CloseSecureResolvedPath(&old_pinned);
            CloseSecureResolvedPath(&new_pinned);
            errno = collision ? EEXIST : EIO;
            return -1;
        }
        const int value = static_cast<int>(syscall(
            SYS_renameat2, AT_FDCWD, old_pinned_path, AT_FDCWD,
            new_pinned_path, RENAME_NOREPLACE));
        const int call_errno = errno;
        if (value != 0) {
            SendTransactionStep(&transaction,
                                provenance_protocol::Command::kAbort);
            CloseSecureResolvedPath(&old_pinned);
            CloseSecureResolvedPath(&new_pinned);
            errno = call_errno;
            return -1;
        }
        const int new_target = openat(
            new_pinned.parent_fd, new_pinned.basename,
            O_PATH | O_NOFOLLOW | O_CLOEXEC);
        const bool identified = new_target >= 0
            && CaptureIdentity(new_target, &transaction.record.identity);
        if (new_target >= 0) close(new_target);
        const bool committed = identified
            && SendTransactionStep(&transaction,
                                   provenance_protocol::Command::kMaterialize)
            && SendTransactionStep(&transaction,
                                   provenance_protocol::Command::kCommit);
        if (!committed) {
            const bool compensated = syscall(
                SYS_renameat2, AT_FDCWD, new_pinned_path, AT_FDCWD,
                old_pinned_path, RENAME_NOREPLACE) == 0;
            SendTransactionStep(&transaction,
                                provenance_protocol::Command::kAbort);
            LOGE("provenance rename commit failed: uid=%d compensated=%d",
                 old_uid, compensated ? 1 : 0);
            CloseSecureResolvedPath(&old_pinned);
            CloseSecureResolvedPath(&new_pinned);
            errno = EIO;
            return -1;
        }
        CloseSecureResolvedPath(&old_pinned);
        CloseSecureResolvedPath(&new_pinned);
        errno = call_errno;
        return 0;
    }
    const int value = function(
        old_changed ? AT_FDCWD : old_dirfd,
        old_changed ? old_pinned_path : plan.first,
        new_changed ? AT_FDCWD : new_dirfd,
        new_changed ? new_pinned_path : plan.second);
    const int call_errno = errno;
    CloseSecureResolvedPath(&old_pinned);
    CloseSecureResolvedPath(&new_pinned);
    errno = call_errno;
    return value;
}

int HookedRename(const char* old_path, const char* new_path) {
    return WithTwoPaths("rename", kOperationRename,
        AT_FDCWD, old_path, AT_FDCWD, new_path, 0,
        [&](int, const char* final_old, int, const char* final_new) {
            return g_rename(final_old, final_new);
        });
}

int HookedRenameAt(int old_dirfd, const char* old_path,
                   int new_dirfd, const char* new_path) {
    return WithTwoPaths("renameat", kOperationRename,
        old_dirfd, old_path, new_dirfd, new_path, 0,
        [&](int final_old_dirfd, const char* final_old,
            int final_new_dirfd, const char* final_new) {
            return g_renameat(final_old_dirfd, final_old,
                              final_new_dirfd, final_new);
        });
}

int HookedRenameAt2(int old_dirfd, const char* old_path,
                    int new_dirfd, const char* new_path, unsigned flags) {
    return WithTwoPaths("renameat2", kOperationRename,
        old_dirfd, old_path, new_dirfd, new_path, flags,
        [&](int final_old_dirfd, const char* final_old,
            int final_new_dirfd, const char* final_new) {
            using RenameAt2Fn = int (*)(int, const char*, int, const char*, unsigned);
            return reinterpret_cast<RenameAt2Fn>(g_renameat2)(
                final_old_dirfd, final_old, final_new_dirfd, final_new, flags);
        });
}

int HookedLink(const char* old_path, const char* new_path) {
    return WithTwoPaths("link", kOperationHardLink,
        AT_FDCWD, old_path, AT_FDCWD, new_path, 0,
        [&](int, const char* final_old, int, const char* final_new) {
            return g_link(final_old, final_new);
        });
}

int HookedLinkAt(int old_dirfd, const char* old_path,
                 int new_dirfd, const char* new_path, int flags) {
    return WithTwoPaths("linkat", kOperationHardLink,
        old_dirfd, old_path, new_dirfd, new_path, 0,
        [&](int final_old_dirfd, const char* final_old,
            int final_new_dirfd, const char* final_new) {
            return g_linkat(final_old_dirfd, final_old,
                            final_new_dirfd, final_new, flags);
        });
}

ssize_t HookedReadlink(const char* path, char* buffer, size_t size) {
    return WithPath("readlink", kOperationReadlink, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_readlink(final_path, buffer, size); });
}

char* HookedRealpath(const char* path, char* resolved) {
    if (__atomic_load_n(&g_hooks_enabled, __ATOMIC_ACQUIRE) == 0) {
        return g_realpath(path, resolved);
    }
    HookGuard guard;
    if (!guard.outer() || path == nullptr) return g_realpath(path, resolved);
    if (g_policy_domain == nullptr) return g_realpath(path, resolved);
    auto snapshot_guard = g_policy_domain->Acquire(GetThreadState());
    if (!snapshot_guard) return g_realpath(path, resolved);
    char absolute[PATH_MAX]{}, rewritten[PATH_MAX]{};
    int32_t uid = -1;
    const auto rewrite = RewriteAtPath(
        *snapshot_guard, AT_FDCWD, path, kOperationCanonicalPath, 1,
        absolute, rewritten, &uid);
    if (rewrite.disposition == storage_path_adapter::RewriteDisposition::kDeny) {
        errno = EACCES;
        return nullptr;
    }
    if (rewrite.disposition != storage_path_adapter::RewriteDisposition::kRedirect) {
        return g_realpath(path, resolved);
    }
    const auto canonical_plan = path_hook_contract::PlanCanonicalPath(rewrite);
    if (canonical_plan
        == path_hook_contract::CanonicalPathDisposition::kAmbiguousReverse) {
        errno = EXDEV;
        return nullptr;
    }
    SecureResolvedPath pinned = ResolveStoragePathParent(
        rewritten, false, &g_resolver_probe);
    if (!pinned.ok()) {
        errno = pinned.error == 0 ? EACCES : pinned.error;
        return nullptr;
    }
    char pinned_path[PATH_MAX]{};
    if (!BuildPinnedProcPath(pinned, pinned_path, sizeof(pinned_path))) {
        CloseSecureResolvedPath(&pinned);
        errno = ENAMETOOLONG;
        return nullptr;
    }
    if (canonical_plan
        == path_hook_contract::CanonicalPathDisposition::kProvenanceLookup) {
        provenance_protocol::Record record;
        const int target = openat(pinned.parent_fd, pinned.basename,
                                  O_PATH | O_NOFOLLOW | O_CLOEXEC);
        const bool record_ready = target >= 0
            && BuildProvenanceRecord(rewrite, uid, absolute, rewritten, &record)
            && CaptureIdentity(target, &record.identity);
        if (target >= 0) close(target);
        provenance_protocol::Response response;
        const auto request = MakeProvenanceRequest(
            provenance_protocol::Command::kResolve, record, uid);
        const bool unique = record_ready && CallProvenance(request, &response)
            && response.provenance_error == provenance_protocol::Error::kNone
            && response.resolve_status
                == provenance_protocol::ResolveStatus::kUnique
            && strcmp(response.logical_source, record.logical_source) == 0;
        if (!unique) {
            CloseSecureResolvedPath(&pinned);
            errno = EXDEV;
            return nullptr;
        }
    }
    char canonical[PATH_MAX]{};
    if (g_realpath(pinned_path, canonical) == nullptr) {
        const int call_errno = errno;
        CloseSecureResolvedPath(&pinned);
        errno = call_errno;
        return nullptr;
    }
    CloseSecureResolvedPath(&pinned);
    char* target = resolved;
    if (target == nullptr) {
        target = static_cast<char*>(malloc(strlen(absolute) + 1));
        if (target == nullptr) return nullptr;
    }
    // Do not leak the backing path through realpath. A provenance-aware
    // reverse adapter may refine the logical source later.
    strcpy(target, absolute);
    LogRewrite("realpath", uid, absolute, rewritten);
    return target;
}

int HookedChmod(const char* path, mode_t mode) {
    return WithPath("chmod", kOperationMetadataMutation, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_chmod(final_path, mode); });
}

int HookedFchmodAt(int dirfd, const char* path, mode_t mode, int flags) {
    return WithPath("fchmodat", kOperationMetadataMutation, 1, dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_fchmodat(final_dirfd, final_path, mode, flags);
        });
}

int HookedChown(const char* path, uid_t owner, gid_t group) {
    return WithPath("chown", kOperationMetadataMutation, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_chown(final_path, owner, group); });
}

int HookedLchown(const char* path, uid_t owner, gid_t group) {
    return WithPath("lchown", kOperationMetadataMutation, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_lchown(final_path, owner, group); });
}

int HookedFchownAt(int dirfd, const char* path, uid_t owner, gid_t group, int flags) {
    return WithPath("fchownat", kOperationMetadataMutation, 1, dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_fchownat(final_dirfd, final_path, owner, group, flags);
        });
}

int HookedTruncate(const char* path, off_t length) {
    return WithPath("truncate", kOperationTruncate, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_truncate(final_path, length); });
}

int HookedTruncate64(const char* path, off64_t length) {
    return WithPath("truncate64", kOperationTruncate, 1, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_truncate64(final_path, length); });
}

int HookedUtimensAt(int dirfd, const char* path,
                    const struct timespec times[2], int flags) {
    return WithPath("utimensat", kOperationMetadataMutation, 1, dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_utimensat(final_dirfd, final_path, times, flags);
        });
}

int HookedStatVfs(const char* path, struct statvfs* value) {
    return WithPath("statvfs", kOperationLookupStat, 2, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_statvfs(final_path, value); });
}

int HookedStatVfs64(const char* path, StatVfs64* value) {
    return WithPath("statvfs64", kOperationLookupStat, 2, AT_FDCWD, path,
        [&](int, const char* final_path) { return g_statvfs64(final_path, value); });
}

int HookedInotifyAddWatch(int fd, const char* path, uint32_t mask) {
    return WithPath("inotify_add_watch", kOperationWatch, 2, AT_FDCWD, path,
        [&](int, const char* final_path) {
            return g_inotify_add_watch(fd, final_path, mask);
        });
}

void CaptureFuseRequest(FuseRequest request, const FuseContext* context) {
    if (context == nullptr) return;
    BeginFuseRequest(GetThreadState(), request, static_cast<int32_t>(context->uid),
                     static_cast<int32_t>(getuid()));
}

void* HookedFuseReqUserdata(FuseRequest request) {
    void* userdata = g_fuse_req_userdata(request);
    if (g_fuse_req_context != nullptr) {
        CaptureFuseRequest(request, g_fuse_req_context(request));
    }
    return userdata;
}

const FuseContext* HookedFuseReqContext(FuseRequest request) {
    const FuseContext* context = g_fuse_req_context(request);
    CaptureFuseRequest(request, context);
    return context;
}

void FinishFuseRequest(FuseRequest request) {
    EndFuseRequest(GetThreadState(), request);
}

int HookedFuseReplyErr(FuseRequest request, int error) {
    const int result = g_fuse_reply_err(request, error);
    FinishFuseRequest(request);
    return result;
}

int HookedFuseReplyEntry(FuseRequest request, const FuseEntryParam* entry) {
    const int result = g_fuse_reply_entry(request, entry);
    FinishFuseRequest(request);
    return result;
}

int HookedFuseReplyAttr(FuseRequest request, const struct stat* value,
                        double timeout) {
    const int result = g_fuse_reply_attr(request, value, timeout);
    FinishFuseRequest(request);
    return result;
}

int HookedFuseReplyOpen(FuseRequest request, const FuseFileInfo* file_info) {
    const int result = g_fuse_reply_open(request, file_info);
    FinishFuseRequest(request);
    return result;
}

int HookedFuseReplyWrite(FuseRequest request, size_t count) {
    const int result = g_fuse_reply_write(request, count);
    FinishFuseRequest(request);
    return result;
}

int HookedFuseReplyBuffer(FuseRequest request, const char* buffer, size_t size) {
    const int result = g_fuse_reply_buffer(request, buffer, size);
    FinishFuseRequest(request);
    return result;
}

int HookedFuseReplyData(FuseRequest request, FuseBufferVector* buffer, int flags) {
    const int result = g_fuse_reply_data(request, buffer, flags);
    FinishFuseRequest(request);
    return result;
}

int HookedFuseReplyStatVfs(FuseRequest request, const struct statvfs* value) {
    const int result = g_fuse_reply_statvfs(request, value);
    FinishFuseRequest(request);
    return result;
}

int HookedFuseReplyCreate(FuseRequest request, const FuseEntryParam* entry,
                          const FuseFileInfo* file_info) {
    const int result = g_fuse_reply_create(request, entry, file_info);
    FinishFuseRequest(request);
    return result;
}

void HookedFuseReplyNone(FuseRequest request) {
    g_fuse_reply_none(request);
    FinishFuseRequest(request);
}

void MaybeInstallFuseHooks(const char* path, void* handle);

void* HookedDlopen(const char* path, int flags) {
    if (g_dlopen == nullptr) return nullptr;
    void* handle = g_dlopen(path, flags);
    MaybeInstallFuseHooks(path, handle);
    return handle;
}

void* HookedAndroidDlopenExt(const char* path, int flags,
                             const android_dlextinfo* info) {
    if (g_android_dlopen_ext == nullptr) return nullptr;
    void* handle = g_android_dlopen_ext(path, flags, info);
    MaybeInstallFuseHooks(path, handle);
    return handle;
}

// Only PLT-hook libraries that live for the entire lifetime of the process.
// Hooking transient images is unsafe: ART unloads short-lived libraries (JIT
// caches, dynamically (un)loaded HAL/AIDL client libraries) during
// PostZygoteFork, but the PLT-hook backend keeps a backup pointer into the
// now-unmapped image and dereferences it during the dlclose destructor walk,
// crashing the process (SEGV_MAPERR). The provider's Java file operations
// reach libc through these permanent runtime libraries, so hooking their GOT
// is both sufficient and stable. A missed caller degrades to fail-open (no
// redirect) rather than a crash, matching the ADR-0006 safety contract.
const char* const kHookTargetBasenames[] = {
    "libjavacore.so",        // libcore.io.Linux JNI: java.io.File, android.system.Os
    "libopenjdk.so",         // java.nio / java.io native file access
    "libnativehelper.so",    // JNI file helpers
    "libandroid_runtime.so", // framework native file access
    "libnativeloader.so",    // Runtime.nativeLoad -> android_dlopen_ext
};

const char* PathBasename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

bool IsPermanentHookTarget(const char* path) {
    const char* name = PathBasename(path);
    for (const char* candidate : kHookTargetBasenames) {
        if (strcmp(name, candidate) == 0) return true;
    }
    return false;
}

bool IsFuseHookTarget(const char* path) {
    return path != nullptr && strcmp(PathBasename(path), "libfuse_jni.so") == 0;
}

struct HookSpec {
    const char* symbol;
    void* replacement;
    void** original;
};

struct RegistrationContext {
    zygisk::Api* api;
    const HookSpec* specs;
    size_t spec_count;
    bool (*target)(const char*);
    uint32_t image_count = 0;
    uint32_t registration_count = 0;
};

bool AddressInImage(const dl_phdr_info& info, uintptr_t address) {
    for (ElfW(Half) index = 0; index < info.dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = info.dlpi_phdr[index];
        if (header.p_type != PT_LOAD) continue;
        const uintptr_t begin = info.dlpi_addr + header.p_vaddr;
        if (address >= begin && address < begin + header.p_memsz) return true;
    }
    return false;
}

uintptr_t ResolveDynamicAddress(const dl_phdr_info& info, ElfW(Addr) value) {
    const uintptr_t absolute = static_cast<uintptr_t>(value);
    if (AddressInImage(info, absolute)) return absolute;
    const uintptr_t relative = info.dlpi_addr + absolute;
    return AddressInImage(info, relative) ? relative : 0;
}

size_t RelocationSymbolIndex(ElfW(Xword) info) {
#if defined(__LP64__)
    return ELF64_R_SYM(info);
#else
    return ELF32_R_SYM(info);
#endif
}

bool StatMappedImage(const char* path, struct stat* identity) {
    if (path == nullptr || identity == nullptr) return false;
    if (stat(path, identity) == 0) return true;
    const char* separator = strchr(path, '!');
    if (separator == nullptr) return false;
    const size_t length = static_cast<size_t>(separator - path);
    if (length == 0 || length >= PATH_MAX) return false;
    char container[PATH_MAX]{};
    memcpy(container, path, length);
    return stat(container, identity) == 0;
}

void RegisterImportedSymbol(RegistrationContext* context, dev_t device,
                            ino_t inode, const char* symbol,
                            uint64_t* registered) {
    for (size_t index = 0; index < context->spec_count; ++index) {
        const HookSpec& spec = context->specs[index];
        if (strcmp(spec.symbol, symbol) != 0) continue;
        const uint64_t bit = uint64_t{1} << index;
        if ((*registered & bit) != 0) return;
        context->api->pltHookRegister(
            device, inode, spec.symbol, spec.replacement, spec.original);
        *registered |= bit;
        ++context->registration_count;
        return;
    }

}

int RegisterImageImports(dl_phdr_info* info, size_t, void* opaque) {
    auto* context = static_cast<RegistrationContext*>(opaque);
    if (info == nullptr || info->dlpi_name == nullptr || context == nullptr
        || context->target == nullptr
        || !context->target(info->dlpi_name)) return 0;

    struct stat identity{};
    if (!StatMappedImage(info->dlpi_name, &identity)
        || identity.st_dev == 0 || identity.st_ino == 0) return 0;

    const ElfW(Dyn)* dynamic = nullptr;
    size_t dynamic_count = 0;
    for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = info->dlpi_phdr[index];
        if (header.p_type != PT_DYNAMIC) continue;
        dynamic = reinterpret_cast<const ElfW(Dyn)*>(
            info->dlpi_addr + header.p_vaddr);
        dynamic_count = header.p_memsz / sizeof(ElfW(Dyn));
        break;
    }
    if (dynamic == nullptr || dynamic_count == 0) return 0;

    uintptr_t string_table = 0;
    uintptr_t symbol_table = 0;
    uintptr_t relocations = 0;
    size_t string_size = 0;
    size_t relocation_size = 0;
    ElfW(Sxword) relocation_type = 0;
    for (size_t index = 0; index < dynamic_count; ++index) {
        const ElfW(Dyn)& entry = dynamic[index];
        if (entry.d_tag == DT_NULL) break;
        switch (entry.d_tag) {
            case DT_STRTAB:
                string_table = ResolveDynamicAddress(*info, entry.d_un.d_ptr);
                break;
            case DT_STRSZ: string_size = entry.d_un.d_val; break;
            case DT_SYMTAB:
                symbol_table = ResolveDynamicAddress(*info, entry.d_un.d_ptr);
                break;
            case DT_JMPREL:
                relocations = ResolveDynamicAddress(*info, entry.d_un.d_ptr);
                break;
            case DT_PLTRELSZ: relocation_size = entry.d_un.d_val; break;
            case DT_PLTREL: relocation_type = entry.d_un.d_val; break;
            default: break;
        }
    }
    if (string_table == 0 || symbol_table == 0 || relocations == 0
        || string_size == 0 || relocation_size == 0
        || (relocation_type != DT_REL && relocation_type != DT_RELA)) return 0;

    ++context->image_count;
    const auto* symbols = reinterpret_cast<const ElfW(Sym)*>(symbol_table);
    const auto* strings = reinterpret_cast<const char*>(string_table);
    const size_t entry_size = relocation_type == DT_RELA
        ? sizeof(ElfW(Rela)) : sizeof(ElfW(Rel));
    const size_t count = relocation_size / entry_size;
    uint64_t registered = 0;
    for (size_t index = 0; index < count; ++index) {
        const ElfW(Xword) relocation_info = relocation_type == DT_RELA
            ? reinterpret_cast<const ElfW(Rela)*>(relocations)[index].r_info
            : reinterpret_cast<const ElfW(Rel)*>(relocations)[index].r_info;
        const ElfW(Sym)& imported = symbols[RelocationSymbolIndex(relocation_info)];
        if (imported.st_name >= string_size) continue;
        RegisterImportedSymbol(context, identity.st_dev, identity.st_ino,
                               strings + imported.st_name, &registered);
    }
    return 0;
}

#define HOOK(symbol, hook, original) \
        {symbol, reinterpret_cast<void*>(hook), \
         reinterpret_cast<void**>(&(original))}
const HookSpec kHookSpecs[] = {
        HOOK("open", HookedOpen, g_open),
        HOOK("open64", HookedOpen64, g_open64),
        HOOK("openat", HookedOpenAt, g_openat),
        HOOK("openat64", HookedOpenAt64, g_openat64),
        HOOK("stat", HookedStat, g_stat),
        HOOK("lstat", HookedLstat, g_lstat),
        HOOK("fstatat", HookedFstatAt, g_fstatat),
        HOOK("stat64", HookedStat64, g_stat64),
        HOOK("lstat64", HookedLstat64, g_lstat64),
        HOOK("fstatat64", HookedFstatAt64, g_fstatat64),
        HOOK("access", HookedAccess, g_access),
        HOOK("faccessat", HookedFaccessAt, g_faccessat),
        HOOK("opendir", HookedOpenDir, g_opendir),
        HOOK("mkdir", HookedMkdir, g_mkdir),
        HOOK("mkdirat", HookedMkdirAt, g_mkdirat),
        HOOK("unlink", HookedUnlink, g_unlink),
        HOOK("unlinkat", HookedUnlinkAt, g_unlinkat),
        HOOK("remove", HookedRemove, g_remove),
        HOOK("rmdir", HookedRmdir, g_rmdir),
        HOOK("rename", HookedRename, g_rename),
        HOOK("renameat", HookedRenameAt, g_renameat),
        HOOK("renameat2", HookedRenameAt2, g_renameat2),
        HOOK("link", HookedLink, g_link),
        HOOK("linkat", HookedLinkAt, g_linkat),
        HOOK("readlink", HookedReadlink, g_readlink),
        HOOK("realpath", HookedRealpath, g_realpath),
        HOOK("chmod", HookedChmod, g_chmod),
        HOOK("fchmodat", HookedFchmodAt, g_fchmodat),
        HOOK("chown", HookedChown, g_chown),
        HOOK("lchown", HookedLchown, g_lchown),
        HOOK("fchownat", HookedFchownAt, g_fchownat),
        HOOK("truncate", HookedTruncate, g_truncate),
        HOOK("truncate64", HookedTruncate64, g_truncate64),
        HOOK("utimensat", HookedUtimensAt, g_utimensat),
        HOOK("statvfs", HookedStatVfs, g_statvfs),
        HOOK("statvfs64", HookedStatVfs64, g_statvfs64),
        HOOK("inotify_add_watch", HookedInotifyAddWatch, g_inotify_add_watch),
        HOOK("dlopen", HookedDlopen, g_dlopen),
        HOOK("android_dlopen_ext", HookedAndroidDlopenExt, g_android_dlopen_ext),
        HOOK("fuse_req_userdata", HookedFuseReqUserdata, g_fuse_req_userdata),
        HOOK("fuse_req_ctx", HookedFuseReqContext, g_fuse_req_context),
        HOOK("fuse_reply_err", HookedFuseReplyErr, g_fuse_reply_err),
        HOOK("fuse_reply_entry", HookedFuseReplyEntry, g_fuse_reply_entry),
        HOOK("fuse_reply_attr", HookedFuseReplyAttr, g_fuse_reply_attr),
        HOOK("fuse_reply_open", HookedFuseReplyOpen, g_fuse_reply_open),
        HOOK("fuse_reply_write", HookedFuseReplyWrite, g_fuse_reply_write),
        HOOK("fuse_reply_buf", HookedFuseReplyBuffer, g_fuse_reply_buffer),
        HOOK("fuse_reply_data", HookedFuseReplyData, g_fuse_reply_data),
        HOOK("fuse_reply_statfs", HookedFuseReplyStatVfs, g_fuse_reply_statvfs),
        HOOK("fuse_reply_create", HookedFuseReplyCreate, g_fuse_reply_create),
        HOOK("fuse_reply_none", HookedFuseReplyNone, g_fuse_reply_none),
};
#undef HOOK
static_assert(sizeof(kHookSpecs) / sizeof(kHookSpecs[0]) <= 64);

RegistrationContext RegisterMappedHooks(zygisk::Api* api,
                                        bool (*target)(const char*)) {
    RegistrationContext context{
        api, kHookSpecs, sizeof(kHookSpecs) / sizeof(kHookSpecs[0]), target};
    dl_iterate_phdr(RegisterImageImports, &context);
    return context;
}

InstallResult RegisterHooks(zygisk::Api* api, bool binder_identity_hooks) {
    const RegistrationContext context = RegisterMappedHooks(
        api, IsPermanentHookTarget);
    InstallResult result;
    result.hook_registration_attempted = context.registration_count != 0;
    result.hooks_committed = api->pltHookCommit();
    const bool has_open = g_open != nullptr || g_openat != nullptr;
    const bool has_stat = g_stat != nullptr || g_stat64 != nullptr
        || g_fstatat != nullptr || g_fstatat64 != nullptr;
    result.identity_hook_attempted = binder_identity_hooks;
    result.identity_hooks = binder_identity_hooks;
    OperationMask operations = 0;
    if (has_stat) operations |= kOperationLookupStat;
    if (g_access != nullptr || g_faccessat != nullptr) operations |= kOperationAccess;
    if (has_open) operations |= kOperationOpenRead | kOperationOpenWrite
        | kOperationCreate;
    if (g_opendir != nullptr) operations |= kOperationDirectoryIterate;
    if (g_mkdir != nullptr || g_mkdirat != nullptr) operations |= kOperationMkdir;
    if (g_rename != nullptr || g_renameat != nullptr) operations |= kOperationRename;
    if (g_link != nullptr || g_linkat != nullptr) operations |= kOperationHardLink;
    if (g_remove != nullptr || g_unlink != nullptr) operations |= kOperationUnlink;
    if (g_rmdir != nullptr) operations |= kOperationRmdir;
    if (g_realpath != nullptr) operations |= kOperationCanonicalPath;
    if (g_readlink != nullptr) operations |= kOperationReadlink;
    if (g_chmod != nullptr && g_chown != nullptr) {
        operations |= kOperationMetadataMutation;
    }
    if (g_truncate != nullptr || g_truncate64 != nullptr) operations |= kOperationTruncate;
    if (g_inotify_add_watch != nullptr) operations |= kOperationWatch;
    result.observed_operations = operations;
    result.virtualization_active = result.hooks_committed
        && operations != 0 && binder_identity_hooks;
    uint32_t capability = 0;
    if (has_open) capability |= 1u << 0;
    if (has_stat) capability |= 1u << 1;
    if (g_access != nullptr) capability |= 1u << 2;
    if (g_opendir != nullptr) capability |= 1u << 3;
    if (g_mkdir != nullptr) capability |= 1u << 4;
    if (g_remove != nullptr || g_unlink != nullptr) capability |= 1u << 5;
    if (g_rename != nullptr) capability |= 1u << 6;
    if (g_realpath != nullptr) capability |= 1u << 7;
    if (g_chmod != nullptr && g_chown != nullptr) capability |= 1u << 8;
    if (binder_identity_hooks) capability |= 1u << 9;
    if (g_inotify_add_watch != nullptr) capability |= 1u << 10;
    if (g_android_dlopen_ext != nullptr || g_dlopen != nullptr) capability |= 1u << 11;
    LOGI("provider virtual hooks: images=%u registrations=%u attempted=%d committed=%d active=%d capability=%x identity_mode=%u "
         "open=%p openat=%p stat=%p stat64=%p access=%p opendir=%p mkdir=%p "
         "remove=%p rename=%p realpath=%p",
         context.image_count, context.registration_count,
         result.hook_registration_attempted ? 1 : 0,
         result.hooks_committed ? 1 : 0,
         result.virtualization_active ? 1 : 0, capability,
         binder_identity_hooks ? 1u : 0u,
         g_open, g_openat, g_stat, g_stat64, g_access, g_opendir, g_mkdir,
         g_remove, g_rename, g_realpath);
    return result;
}

void MaybeInstallFuseHooks(const char* path, void* handle) {
    if (!IsFuseHookTarget(path) || g_api == nullptr) return;
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&g_fuse_install_state, &expected, 1, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return;
    }
    if (handle != nullptr && g_fuse_req_context == nullptr) {
        g_fuse_req_context = reinterpret_cast<FuseReqContextFn>(
            dlsym(handle, "fuse_req_ctx"));
    }
    const RegistrationContext context = RegisterMappedHooks(g_api, IsFuseHookTarget);
    const bool committed = context.registration_count != 0
        && g_api->pltHookCommit();
    const bool request_scope = g_fuse_req_userdata != nullptr
        && g_fuse_req_context != nullptr && g_fuse_reply_err != nullptr
        && g_fuse_reply_create != nullptr;
    const bool active = committed && request_scope;
    __atomic_store_n(&g_fuse_install_state, active ? 2u : 3u, __ATOMIC_RELEASE);
    LOGI("provider FUSE hooks: images=%u registrations=%u committed=%d active=%d "
         "request=%p context=%p reply_err=%p reply_create=%p",
         context.image_count, context.registration_count, committed ? 1 : 0,
         active ? 1 : 0, g_fuse_req_userdata, g_fuse_req_context,
         g_fuse_reply_err, g_fuse_reply_create);
}

}  // namespace

int32_t CurrentCallingUid() noexcept {
    const int32_t uid = EffectiveCallingUid();
    return uid >= 10000 && uid != static_cast<int32_t>(getuid()) ? uid : -1;
}

void SetProvenanceTransactionsAvailable(bool available) noexcept {
    __atomic_store_n(
        &g_provenance_transactions_available,
        available ? 1u : 0u,
        __ATOMIC_RELEASE);
}

bool BuildProvenanceRecordForRoute(
        const storage_path_adapter::RewriteResult& rewrite,
        int32_t caller_uid, const char* logical_path, const char* target_path,
        provenance_protocol::Record* output) noexcept {
    return BuildProvenanceRecord(
        rewrite, caller_uid, logical_path, target_path, output);
}

bool CaptureProvenanceIdentity(
        int fd, provenance_protocol::Identity* output) noexcept {
    return CaptureIdentity(fd, output);
}

InstallResult Install(zygisk::Api* api, JNIEnv* env,
                      const PolicyConfig& config) {
    if (api == nullptr || env == nullptr || config.policy_data == nullptr
        || config.policy_size == 0 || config.scopes == nullptr
        || config.scope_count == 0 || config.scope_count > kMaxScopes
        || (config.domain != AdmissionDomain::kAppPath
            && config.domain != AdmissionDomain::kProvider)) {
        return {};
    }
    policy_v6_view::PolicyV6View policy;
    if (!policy.Initialize(config.policy_data, config.policy_size)) return {};
    if (!InitializeThreadStateKey()) {
        LOGE("provider virtual thread state initialization failed");
        return {};
    }
    if (!InitializePolicySnapshotAtFork()) {
        LOGE("policy snapshot atfork registration failed");
        return {};
    }
    if (!InitializeTransactionAtFork()) {
        LOGE("provenance transaction atfork registration failed");
        return {};
    }
    // Android app and Provider seccomp policies may trap openat2 with SIGSYS
    // instead of returning EPERM. The component FD walk provides the same
    // no-symlink boundary without probing a process-fatal syscall.
    g_resolver_probe.ObserveOpenAt2(EPERM, true);
    __atomic_store_n(&g_hooks_enabled, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_fuse_install_state, 0, __ATOMIC_RELEASE);
    SetProvenanceTransactionsAvailable(false);
    g_api = api;
    auto* runtime = static_cast<RuntimePolicySnapshot*>(
        calloc(1, sizeof(RuntimePolicySnapshot)));
    if (runtime == nullptr) return {};
    runtime->bytes = static_cast<uint8_t*>(malloc(config.policy_size));
    if (runtime->bytes == nullptr) { free(runtime); return {}; }
    memcpy(runtime->bytes, config.policy_data, config.policy_size);
    runtime->size = config.policy_size;
    if (!runtime->policy.Initialize(runtime->bytes, runtime->size)) {
        ReleaseRuntimePolicy(runtime);
        return {};
    }
    runtime->scope_count = config.scope_count;
    runtime->domain = config.domain;
    g_domain = config.domain;
    g_identity_mode = config.identity_mode;
    for (uint32_t index = 0; index < config.scope_count; ++index) {
        const auto& scope = config.scopes[index];
        policy_v6_view::PackageRef package;
        if (scope.caller_uid < 10000
            || !policy.PackageAt(scope.package_index, &package)) {
            ReleaseRuntimePolicy(runtime);
            return {};
        }
        runtime->scopes[index] = scope;
    }
    bool binder_identity_attempted = false;
    const bool needs_binder = config.identity_mode == IdentityMode::kBinderCallerUid;
    const bool identity_hooks = !needs_binder || InstallBinderIdentityHooks(
        api, env, &binder_identity_attempted);
    if (!identity_hooks) {
        LOGE("provider virtual Binder JNI identity hooks unavailable");
        InstallResult result;
        result.identity_hook_attempted = binder_identity_attempted;
        return result;
    }
    InstallResult result = RegisterHooks(api, identity_hooks);
    result.identity_hook_attempted = needs_binder && binder_identity_attempted;
    result.identity_hooks = identity_hooks;
    if (config.domain == AdmissionDomain::kAppPath) {
        result.observed_capabilities |= kCapabilityAppPathAdapter;
    } else if (identity_hooks) {
        ProviderCompositeProbe probe;
        probe.caller_uid = true;
        probe.path_io = result.hooks_committed;
        probe.path_operations = result.observed_operations;
        // A libc path hook cannot prove cursor/document-id/MediaStore view
        // consistency. Bit 17 stays clear until a real Provider ABI adapter
        // probes query, insert, FD, rename/delete and reverse mapping.
        const ProviderCompositeObservation observed =
            ObserveProviderComposite(probe);
        result.observed_capabilities |= observed.capabilities;
        result.observed_operations |= observed.operations;
    }
    g_capabilities = {};
    g_capabilities.capability_generation = 1;
    g_capabilities.observed_capabilities = result.observed_capabilities;
    auto& domain = g_capabilities.domains[static_cast<uint8_t>(config.domain)];
    domain.state = result.virtualization_active
        ? AdapterState::kActive : AdapterState::kInactive;
    domain.observed_operations = result.observed_operations;
    runtime->capabilities = g_capabilities;
    const bool published = config.domain == AdmissionDomain::kProvider
        ? g_provider_policy_domain.Publish(
            runtime, sizeof(*runtime) + config.policy_size)
        : g_app_policy_domain.Publish(
            runtime, sizeof(*runtime) + config.policy_size);
    if (!published) {
        ReleaseRuntimePolicy(runtime);
        result.virtualization_active = false;
    } else {
        result.snapshot_generation = AtomicAddFetch(
            &g_snapshot_generation, uint64_t{1});
        result.capability_generation = g_capabilities.capability_generation;
        g_policy_domain = config.domain == AdmissionDomain::kProvider
            ? static_cast<PolicySnapshotDomainBase<RuntimePolicySnapshot>*>(
                &g_provider_policy_domain)
            : static_cast<PolicySnapshotDomainBase<RuntimePolicySnapshot>*>(
                &g_app_policy_domain);
    }
    const PolicySnapshotMetrics snapshot_metrics = config.domain
            == AdmissionDomain::kProvider
        ? g_provider_policy_domain.metrics() : g_app_policy_domain.metrics();
    result.hazard_slot_acquire_fail_total =
        snapshot_metrics.hazard_slot_acquire_fail_total;
    result.hazard_slots_in_use_high_watermark =
        snapshot_metrics.hazard_slots_in_use_high_watermark;
    result.snapshot_reload_rejected_retire_limit_total =
        snapshot_metrics.snapshot_reload_rejected_retire_limit_total;
    result.retired_snapshot_count_high_watermark =
        snapshot_metrics.retired_snapshot_count_high_watermark;
    result.retired_snapshot_bytes_high_watermark =
        snapshot_metrics.retired_snapshot_bytes_high_watermark;
    if (ShouldEnableHooks(result)) {
        __atomic_store_n(&g_hooks_enabled, 1, __ATOMIC_RELEASE);
    }
    LOGI("path policy installed: scopes=%u domain=%u caller_scope=%s "
         "binder_jni=%d late_loader=%d attempted=%d committed=%d active=%d",
         config.scope_count, static_cast<unsigned>(config.domain),
         needs_binder ? "binder_uid" : "process_uid",
         result.identity_hooks ? 1 : 0,
         g_android_dlopen_ext != nullptr || g_dlopen != nullptr ? 1 : 0,
         result.hook_registration_attempted ? 1 : 0,
         result.hooks_committed ? 1 : 0,
         result.virtualization_active ? 1 : 0);
    return result;
}

}  // namespace pathguard::provider_redirect
