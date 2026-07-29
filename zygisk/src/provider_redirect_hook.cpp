#include "pathguard/provider_redirect_hook.hpp"
#include "pathguard/provider_caller_uid.hpp"
#include "pathguard/provider_path_mapper.h"

#include <android/dlext.h>
#include <android/log.h>

#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/inotify.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace pathguard::provider_redirect {
namespace {

constexpr char kLogTag[] = "PathGuard";
constexpr uint32_t kMaxRules = 64;

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

PathRule g_rules[kMaxRules]{};
uint32_t g_rule_count = 0;
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
zygisk::Api* g_api = nullptr;

void DestroyThreadState(void* value) {
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

bool ValidLogicalRulePath(const char* path) {
    if (path == nullptr || path[0] == '\0' || path[0] == '/'
        || strlen(path) >= PATH_MAX || strchr(path, '{') != nullptr
        || strchr(path, '}') != nullptr) {
        return false;
    }
    const char* component = path;
    while (*component != '\0') {
        const char* separator = strchr(component, '/');
        const size_t length = separator == nullptr
            ? strlen(component) : static_cast<size_t>(separator - component);
        if (length == 0 || length > NAME_MAX
            || (length == 1 && component[0] == '.')
            || (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (separator == nullptr) break;
        component = separator + 1;
    }
    return true;
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
    return EffectiveCallerUid(GetThreadState(), RawCallingUid(),
                              static_cast<int32_t>(getuid()));
}

int64_t HookedClearCallingIdentity() {
    BeginBinderIdentityClear(GetThreadState(), RawCallingUid(),
                             static_cast<int32_t>(getuid()));
    return g_binder_clear_identity();
}

void HookedRestoreCallingIdentity(int64_t token) {
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
    g_binder_get_calling_uid = reinterpret_cast<BinderGetCallingUidFn>(
        methods[0].fnPtr);
    g_binder_clear_identity = reinterpret_cast<BinderClearIdentityFn>(
        methods[1].fnPtr);
    g_binder_restore_identity = reinterpret_cast<BinderRestoreIdentityFn>(
        methods[2].fnPtr);
    *attempted = g_binder_get_calling_uid != nullptr
        || g_binder_clear_identity != nullptr
        || g_binder_restore_identity != nullptr;
    return g_binder_get_calling_uid != nullptr
        && g_binder_clear_identity != nullptr
        && g_binder_restore_identity != nullptr;
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

bool RewriteAtPath(int dirfd, const char* path, char* absolute, char* output,
                   int32_t* caller_uid) {
    if (!ResolveAtPath(dirfd, path, absolute, PATH_MAX)
        || !MatchesVisiblePath(g_rules, g_rule_count, absolute)) {
        return false;
    }
    const int32_t uid = EffectiveCallingUid();
    if (!RewriteAbsolutePath(g_rules, g_rule_count, uid, absolute, output, PATH_MAX)) {
        return false;
    }
    *caller_uid = uid;
    return true;
}

void LogRewrite(const char* operation, int32_t uid,
                const char* from, const char* to) {
    if (__atomic_fetch_add(&g_rewrite_log_count, 1, __ATOMIC_RELAXED) >= 64) return;
    LOGI("provider virtual path: op=%s caller_uid=%d from=%s to=%s",
         operation, uid, from, to);
}

template <typename Function>
auto WithPath(const char* operation, int dirfd, const char* path,
              Function function) -> decltype(function(dirfd, path)) {
    if (__atomic_load_n(&g_hooks_enabled, __ATOMIC_ACQUIRE) == 0) {
        return function(dirfd, path);
    }
    HookGuard guard;
    if (!guard.outer() || path == nullptr) return function(dirfd, path);
    char absolute[PATH_MAX]{};
    char rewritten[PATH_MAX]{};
    int32_t uid = -1;
    if (!RewriteAtPath(dirfd, path, absolute, rewritten, &uid)) {
        return function(dirfd, path);
    }
    LogRewrite(operation, uid, absolute, rewritten);
    return function(AT_FDCWD, rewritten);
}

int HookedOpenCommon(OpenFn original, const char* operation,
                     const char* path, int flags, mode_t mode) {
    if (original == nullptr) { errno = ENOSYS; return -1; }
    return WithPath(operation, AT_FDCWD, path,
        [&](int, const char* final_path) { return original(final_path, flags, mode); });
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
    return WithPath(operation, dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return original(final_dirfd, final_path, flags, mode);
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
    return WithPath("stat", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_stat(final_path, value); });
}

int HookedLstat(const char* path, struct stat* value) {
    return WithPath("lstat", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_lstat(final_path, value); });
}

int HookedFstatAt(int dirfd, const char* path, struct stat* value, int flags) {
    return WithPath("fstatat", dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_fstatat(final_dirfd, final_path, value, flags);
        });
}

int HookedStat64(const char* path, Stat64* value) {
    return WithPath("stat64", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_stat64(final_path, value); });
}

int HookedLstat64(const char* path, Stat64* value) {
    return WithPath("lstat64", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_lstat64(final_path, value); });
}

int HookedFstatAt64(int dirfd, const char* path, Stat64* value, int flags) {
    return WithPath("fstatat64", dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_fstatat64(final_dirfd, final_path, value, flags);
        });
}

int HookedAccess(const char* path, int mode) {
    return WithPath("access", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_access(final_path, mode); });
}

int HookedFaccessAt(int dirfd, const char* path, int mode, int flags) {
    return WithPath("faccessat", dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_faccessat(final_dirfd, final_path, mode, flags);
        });
}

DIR* HookedOpenDir(const char* path) {
    return WithPath("opendir", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_opendir(final_path); });
}

int HookedMkdir(const char* path, mode_t mode) {
    return WithPath("mkdir", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_mkdir(final_path, mode); });
}

int HookedMkdirAt(int dirfd, const char* path, mode_t mode) {
    return WithPath("mkdirat", dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_mkdirat(final_dirfd, final_path, mode);
        });
}

int HookedUnlink(const char* path) {
    return WithPath("unlink", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_unlink(final_path); });
}

int HookedUnlinkAt(int dirfd, const char* path, int flags) {
    return WithPath("unlinkat", dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_unlinkat(final_dirfd, final_path, flags);
        });
}

int HookedRemove(const char* path) {
    return WithPath("remove", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_remove(final_path); });
}

int HookedRmdir(const char* path) {
    return WithPath("rmdir", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_rmdir(final_path); });
}

template <typename Function>
int WithTwoPaths(const char* operation, int old_dirfd, const char* old_path,
                 int new_dirfd, const char* new_path, Function function) {
    if (__atomic_load_n(&g_hooks_enabled, __ATOMIC_ACQUIRE) == 0) {
        return function(old_dirfd, old_path, new_dirfd, new_path);
    }
    HookGuard guard;
    if (!guard.outer() || old_path == nullptr || new_path == nullptr) {
        return function(old_dirfd, old_path, new_dirfd, new_path);
    }
    char old_absolute[PATH_MAX]{}, old_rewritten[PATH_MAX]{};
    char new_absolute[PATH_MAX]{}, new_rewritten[PATH_MAX]{};
    int32_t old_uid = -1;
    int32_t new_uid = -1;
    const bool old_changed = RewriteAtPath(
        old_dirfd, old_path, old_absolute, old_rewritten, &old_uid);
    const bool new_changed = RewriteAtPath(
        new_dirfd, new_path, new_absolute, new_rewritten, &new_uid);
    const int32_t uid = old_changed ? old_uid : new_uid;
    if (old_changed) LogRewrite(operation, uid, old_absolute, old_rewritten);
    if (new_changed) LogRewrite(operation, uid, new_absolute, new_rewritten);
    return function(old_changed ? AT_FDCWD : old_dirfd,
                    old_changed ? old_rewritten : old_path,
                    new_changed ? AT_FDCWD : new_dirfd,
                    new_changed ? new_rewritten : new_path);
}

int HookedRename(const char* old_path, const char* new_path) {
    return WithTwoPaths("rename", AT_FDCWD, old_path, AT_FDCWD, new_path,
        [&](int, const char* final_old, int, const char* final_new) {
            return g_rename(final_old, final_new);
        });
}

int HookedRenameAt(int old_dirfd, const char* old_path,
                   int new_dirfd, const char* new_path) {
    return WithTwoPaths("renameat", old_dirfd, old_path, new_dirfd, new_path,
        [&](int final_old_dirfd, const char* final_old,
            int final_new_dirfd, const char* final_new) {
            return g_renameat(final_old_dirfd, final_old,
                              final_new_dirfd, final_new);
        });
}

int HookedRenameAt2(int old_dirfd, const char* old_path,
                    int new_dirfd, const char* new_path, unsigned flags) {
    return WithTwoPaths("renameat2", old_dirfd, old_path, new_dirfd, new_path,
        [&](int final_old_dirfd, const char* final_old,
            int final_new_dirfd, const char* final_new) {
            using RenameAt2Fn = int (*)(int, const char*, int, const char*, unsigned);
            return reinterpret_cast<RenameAt2Fn>(g_renameat2)(
                final_old_dirfd, final_old, final_new_dirfd, final_new, flags);
        });
}

ssize_t HookedReadlink(const char* path, char* buffer, size_t size) {
    return WithPath("readlink", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_readlink(final_path, buffer, size); });
}

char* HookedRealpath(const char* path, char* resolved) {
    if (__atomic_load_n(&g_hooks_enabled, __ATOMIC_ACQUIRE) == 0) {
        return g_realpath(path, resolved);
    }
    HookGuard guard;
    if (!guard.outer() || path == nullptr) return g_realpath(path, resolved);
    char absolute[PATH_MAX]{}, rewritten[PATH_MAX]{};
    int32_t uid = -1;
    if (!RewriteAtPath(AT_FDCWD, path, absolute, rewritten, &uid)) {
        return g_realpath(path, resolved);
    }
    char canonical[PATH_MAX]{};
    if (g_realpath(rewritten, canonical) == nullptr) return nullptr;
    char restored[PATH_MAX]{};
    if (!RestoreAbsolutePath(g_rules, g_rule_count, uid, canonical,
                             restored, sizeof(restored))) {
        errno = EXDEV;
        return nullptr;
    }
    char* target = resolved;
    if (target == nullptr) {
        target = static_cast<char*>(malloc(strlen(restored) + 1));
        if (target == nullptr) return nullptr;
    }
    strcpy(target, restored);
    LogRewrite("realpath", uid, absolute, rewritten);
    return target;
}

int HookedChmod(const char* path, mode_t mode) {
    return WithPath("chmod", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_chmod(final_path, mode); });
}

int HookedFchmodAt(int dirfd, const char* path, mode_t mode, int flags) {
    return WithPath("fchmodat", dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_fchmodat(final_dirfd, final_path, mode, flags);
        });
}

int HookedChown(const char* path, uid_t owner, gid_t group) {
    return WithPath("chown", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_chown(final_path, owner, group); });
}

int HookedLchown(const char* path, uid_t owner, gid_t group) {
    return WithPath("lchown", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_lchown(final_path, owner, group); });
}

int HookedFchownAt(int dirfd, const char* path, uid_t owner, gid_t group, int flags) {
    return WithPath("fchownat", dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_fchownat(final_dirfd, final_path, owner, group, flags);
        });
}

int HookedTruncate(const char* path, off_t length) {
    return WithPath("truncate", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_truncate(final_path, length); });
}

int HookedTruncate64(const char* path, off64_t length) {
    return WithPath("truncate64", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_truncate64(final_path, length); });
}

int HookedUtimensAt(int dirfd, const char* path,
                    const struct timespec times[2], int flags) {
    return WithPath("utimensat", dirfd, path,
        [&](int final_dirfd, const char* final_path) {
            return g_utimensat(final_dirfd, final_path, times, flags);
        });
}

int HookedStatVfs(const char* path, struct statvfs* value) {
    return WithPath("statvfs", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_statvfs(final_path, value); });
}

int HookedStatVfs64(const char* path, StatVfs64* value) {
    return WithPath("statvfs64", AT_FDCWD, path,
        [&](int, const char* final_path) { return g_statvfs64(final_path, value); });
}

int HookedInotifyAddWatch(int fd, const char* path, uint32_t mask) {
    return WithPath("inotify_add_watch", AT_FDCWD, path,
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
    const bool filesystem_complete = has_open && has_stat
        && g_access != nullptr && g_opendir != nullptr
        && g_mkdir != nullptr && g_remove != nullptr && g_rename != nullptr
        && g_realpath != nullptr && g_readlink != nullptr
        && g_chmod != nullptr && g_chown != nullptr
        && g_statvfs != nullptr && g_inotify_add_watch != nullptr;
    result.virtualization_active = result.hooks_committed
        && filesystem_complete && binder_identity_hooks;
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

InstallResult Install(zygisk::Api* api, JNIEnv* env, const Rule* rules,
                      uint32_t rule_count) {
    if (api == nullptr || env == nullptr || rules == nullptr
        || rule_count == 0 || rule_count > kMaxRules) {
        return {};
    }
    if (!InitializeThreadStateKey()) {
        LOGE("provider virtual thread state initialization failed");
        return {};
    }
    __atomic_store_n(&g_hooks_enabled, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_fuse_install_state, 0, __ATOMIC_RELEASE);
    g_api = api;
    g_rule_count = 0;
    for (uint32_t index = 0; index < rule_count; ++index) {
        const Rule& input = rules[index];
        if (input.caller_uid < 10000
            || !ValidLogicalRulePath(input.visible_path)
            || !ValidLogicalRulePath(input.backing_path)) return {};
        for (uint32_t existing = 0; existing < g_rule_count; ++existing) {
            const PathRule& current = g_rules[existing];
            const bool same_scope = current.caller_uid == input.caller_uid
                && current.user_id == input.user_id;
            if (same_scope && strcmp(current.visible_path, input.visible_path) == 0
                && strcmp(current.backing_path, input.backing_path) != 0) {
                LOGE("provider virtual ambiguous rule: uid=%d user=%u visible=%s",
                     input.caller_uid, input.user_id, input.visible_path);
                return {};
            }
        }
        PathRule& output = g_rules[g_rule_count++];
        output.caller_uid = input.caller_uid;
        output.user_id = input.user_id;
        strcpy(output.visible_path, input.visible_path);
        strcpy(output.backing_path, input.backing_path);
    }
    bool binder_identity_attempted = false;
    const bool binder_identity_hooks = InstallBinderIdentityHooks(
        api, env, &binder_identity_attempted);
    if (!binder_identity_hooks) {
        LOGE("provider virtual Binder JNI identity hooks unavailable");
        InstallResult result;
        result.identity_hook_attempted = binder_identity_attempted;
        return result;
    }
    const InstallResult result = RegisterHooks(api, binder_identity_hooks);
    if (ShouldEnableHooks(result)) {
        __atomic_store_n(&g_hooks_enabled, 1, __ATOMIC_RELEASE);
    }
    LOGI("provider virtual installed: rules=%u caller_scope=request_uid "
         "binder_jni=%d late_loader=%d attempted=%d committed=%d active=%d",
         g_rule_count,
         result.identity_hooks ? 1 : 0,
         g_android_dlopen_ext != nullptr || g_dlopen != nullptr ? 1 : 0,
         result.hook_registration_attempted ? 1 : 0,
         result.hooks_committed ? 1 : 0,
         result.virtualization_active ? 1 : 0);
    if (!result.virtualization_active) g_rule_count = 0;
    return result;
}

}  // namespace pathguard::provider_redirect
