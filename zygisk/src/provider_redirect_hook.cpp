#include "pathguard/provider_redirect_hook.hpp"
#include "pathguard/provider_path_mapper.h"

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
using NdkCallingUidFn = uid_t (*)();
using BinderSelfFn = void* (*)();
using BinderCallingUidFn = uid_t (*)(void*);

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
NdkCallingUidFn g_ndk_calling_uid = nullptr;
BinderSelfFn g_binder_self = nullptr;
BinderCallingUidFn g_binder_calling_uid = nullptr;

struct ThreadState {
    bool in_hook = false;
};

pthread_key_t g_thread_state_key{};
uint32_t g_thread_state_key_ready = 0;
uint32_t g_rewrite_log_count = 0;
uint32_t g_hooks_enabled = 0;
CallerMode g_caller_mode = CallerMode::kBinderUid;

void DestroyThreadState(void* value) {
    free(value);
}

bool InitializeThreadStateKey() {
    if (__atomic_load_n(&g_thread_state_key_ready, __ATOMIC_ACQUIRE) != 0) return true;
    if (pthread_key_create(&g_thread_state_key, DestroyThreadState) != 0) return false;
    __atomic_store_n(&g_thread_state_key_ready, 1, __ATOMIC_RELEASE);
    return true;
}

ThreadState* GetThreadState() {
    if (__atomic_load_n(&g_thread_state_key_ready, __ATOMIC_ACQUIRE) == 0) return nullptr;
    auto* state = static_cast<ThreadState*>(pthread_getspecific(g_thread_state_key));
    if (state != nullptr) return state;
    state = static_cast<ThreadState*>(calloc(1, sizeof(ThreadState)));
    if (state == nullptr) return nullptr;
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
    HookGuard() : state_(GetThreadState()), outer_(state_ != nullptr && !state_->in_hook) {
        if (outer_) state_->in_hook = true;
    }
    ~HookGuard() { if (outer_) state_->in_hook = false; }
    bool outer() const { return outer_; }
private:
    ThreadState* state_;
    bool outer_;
};

int32_t RawCallingUid() {
    if (g_ndk_calling_uid != nullptr) {
        return static_cast<int32_t>(g_ndk_calling_uid());
    }
    if (g_binder_self != nullptr && g_binder_calling_uid != nullptr) {
        void* state = g_binder_self();
        if (state != nullptr) return static_cast<int32_t>(g_binder_calling_uid(state));
    }
    return -1;
}

int32_t EffectiveCallingUid() {
    if (g_caller_mode == CallerMode::kSystemMedia) return -1;
    const int32_t current = RawCallingUid();
    return current >= 10000 && current != static_cast<int32_t>(getuid())
        ? current : -1;
}

void ResolveBinderSymbols() {
    void* ndk = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
    if (ndk != nullptr) {
        g_ndk_calling_uid = reinterpret_cast<NdkCallingUidFn>(
            dlsym(ndk, "AIBinder_getCallingUid"));
    }
    void* binder = dlopen("libbinder.so", RTLD_NOW | RTLD_LOCAL);
    if (binder == nullptr) binder = RTLD_DEFAULT;
    g_binder_self = reinterpret_cast<BinderSelfFn>(
        dlsym(binder, "_ZN7android14IPCThreadState4selfEv"));
    g_binder_calling_uid = reinterpret_cast<BinderCallingUidFn>(
        dlsym(binder, "_ZNK7android14IPCThreadState13getCallingUidEv"));
    if (g_binder_calling_uid == nullptr) {
        g_binder_calling_uid = reinterpret_cast<BinderCallingUidFn>(
            dlsym(binder, "_ZN7android14IPCThreadState13getCallingUidEv"));
    }
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

struct HookSpec {
    const char* symbol;
    void* replacement;
    void** original;
};

struct RegistrationContext {
    zygisk::Api* api;
    const HookSpec* specs;
    size_t spec_count;
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
    if (info == nullptr || info->dlpi_name == nullptr
        || !IsPermanentHookTarget(info->dlpi_name)) return 0;

    struct stat identity{};
    if (stat(info->dlpi_name, &identity) != 0
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

    auto* context = static_cast<RegistrationContext*>(opaque);
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

InstallResult RegisterHooks(zygisk::Api* api) {
    const HookSpec specs[] = {
#define HOOK(symbol, hook, original) \
        {symbol, reinterpret_cast<void*>(hook), \
         reinterpret_cast<void**>(&(original))}
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
    };
#undef HOOK
    static_assert(sizeof(specs) / sizeof(specs[0]) <= 64);
    RegistrationContext context{api, specs, sizeof(specs) / sizeof(specs[0])};
    dl_iterate_phdr(RegisterImageImports, &context);
    InstallResult result;
    result.hook_registration_attempted = context.registration_count != 0;
    result.hooks_committed = api->pltHookCommit();
    const bool has_open = g_open != nullptr || g_openat != nullptr;
    const bool has_stat = g_stat != nullptr || g_stat64 != nullptr
        || g_fstatat != nullptr || g_fstatat64 != nullptr;
    result.identity_hooks = false;
    const bool filesystem_complete = has_open && has_stat
        && g_access != nullptr && g_opendir != nullptr
        && g_mkdir != nullptr && g_remove != nullptr && g_rename != nullptr
        && g_realpath != nullptr && g_readlink != nullptr
        && g_chmod != nullptr && g_chown != nullptr
        && g_statvfs != nullptr && g_inotify_add_watch != nullptr;
    const bool raw_caller_uid = g_ndk_calling_uid != nullptr
        || (g_binder_self != nullptr && g_binder_calling_uid != nullptr);
    const bool identity_safe = g_caller_mode == CallerMode::kSystemMedia
        || raw_caller_uid;
    result.virtualization_active = result.hooks_committed
        && filesystem_complete && identity_safe;
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
    if (g_inotify_add_watch != nullptr) capability |= 1u << 10;
    const unsigned identity_mode = g_caller_mode == CallerMode::kSystemMedia
        ? 0u : result.identity_hooks ? 1u : raw_caller_uid ? 2u : 3u;
    LOGI("provider virtual hooks: images=%u registrations=%u attempted=%d committed=%d active=%d capability=%x identity_mode=%u "
         "open=%p openat=%p stat=%p stat64=%p access=%p opendir=%p mkdir=%p "
         "remove=%p rename=%p realpath=%p",
         context.image_count, context.registration_count,
         result.hook_registration_attempted ? 1 : 0,
         result.hooks_committed ? 1 : 0,
         result.virtualization_active ? 1 : 0, capability, identity_mode,
         g_open, g_openat, g_stat, g_stat64, g_access, g_opendir, g_mkdir,
         g_remove, g_rename, g_realpath);
    return result;
}

}  // namespace

InstallResult Install(zygisk::Api* api, JNIEnv*, const Rule* rules,
                      uint32_t rule_count, CallerMode caller_mode) {
    if (api == nullptr || rules == nullptr || rule_count == 0 || rule_count > kMaxRules) {
        return {};
    }
    if (!InitializeThreadStateKey()) {
        LOGE("provider virtual thread state initialization failed");
        return {};
    }
    __atomic_store_n(&g_hooks_enabled, 0, __ATOMIC_RELEASE);
    g_rule_count = 0;
    g_caller_mode = caller_mode;
    for (uint32_t index = 0; index < rule_count; ++index) {
        const Rule& input = rules[index];
        if (input.caller_uid < 10000
            || !ValidLogicalRulePath(input.visible_path)
            || !ValidLogicalRulePath(input.backing_path)) return {};
        for (uint32_t existing = 0; existing < g_rule_count; ++existing) {
            const PathRule& current = g_rules[existing];
            const bool same_scope = caller_mode == CallerMode::kSystemMedia
                ? current.user_id == input.user_id
                : current.caller_uid == input.caller_uid
                    && current.user_id == input.user_id;
            if (same_scope && strcmp(current.visible_path, input.visible_path) == 0
                && strcmp(current.backing_path, input.backing_path) != 0) {
                LOGE("provider virtual ambiguous rule: uid=%d user=%u visible=%s",
                     input.caller_uid, input.user_id, input.visible_path);
                return {};
            }
        }
        PathRule& output = g_rules[g_rule_count++];
        output.caller_uid = caller_mode == CallerMode::kSystemMedia
            ? -1 : input.caller_uid;
        output.user_id = input.user_id;
        strcpy(output.visible_path, input.visible_path);
        strcpy(output.backing_path, input.backing_path);
    }
    ResolveBinderSymbols();
    if (caller_mode == CallerMode::kBinderUid
        && g_ndk_calling_uid == nullptr
        && (g_binder_self == nullptr || g_binder_calling_uid == nullptr)) {
        LOGE("provider virtual Binder calling UID unavailable");
        return {};
    }
    const InstallResult result = RegisterHooks(api);
    if (ShouldEnableHooks(result)) {
        __atomic_store_n(&g_hooks_enabled, 1, __ATOMIC_RELEASE);
    }
    LOGI("provider virtual installed: rules=%u caller_mode=%u binder_ndk=%d binder_cpp=%d "
         "identity_hooks=%d attempted=%d committed=%d active=%d",
         g_rule_count, static_cast<unsigned>(g_caller_mode),
         g_ndk_calling_uid != nullptr ? 1 : 0,
         g_binder_self != nullptr && g_binder_calling_uid != nullptr ? 1 : 0,
         result.identity_hooks ? 1 : 0,
         result.hook_registration_attempted ? 1 : 0,
         result.hooks_committed ? 1 : 0,
         result.virtualization_active ? 1 : 0);
    if (!result.virtualization_active) g_rule_count = 0;
    return result;
}

}  // namespace pathguard::provider_redirect
