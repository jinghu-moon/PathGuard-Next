#include "pathguard/provider_redirect_hook.hpp"
#include "pathguard/provider_path_mapper.h"

#include <android/log.h>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/inotify.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

namespace pathguard::provider_redirect {
namespace {

constexpr char kLogTag[] = "PathGuard";
constexpr uint32_t kMaxRules = 64;
constexpr uint32_t kMaxImages = 256;

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
using ClearIdentityFn = int64_t (*)(void*);
using RestoreIdentityFn = void (*)(void*, int64_t);

struct Image {
    dev_t device = 0;
    ino_t inode = 0;
};

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
ClearIdentityFn g_clear_identity = nullptr;
RestoreIdentityFn g_restore_identity = nullptr;
NdkCallingUidFn g_ndk_calling_uid = nullptr;
BinderSelfFn g_binder_self = nullptr;
BinderCallingUidFn g_binder_calling_uid = nullptr;
thread_local bool g_in_hook = false;
thread_local int32_t g_saved_caller_uid = -1;
thread_local uint32_t g_identity_clear_depth = 0;
uint32_t g_rewrite_log_count = 0;
CallerMode g_caller_mode = CallerMode::kBinderUid;

class HookGuard final {
public:
    HookGuard() : outer_(!g_in_hook) { if (outer_) g_in_hook = true; }
    ~HookGuard() { if (outer_) g_in_hook = false; }
    bool outer() const { return outer_; }
private:
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
    if (current >= 10000 && current != static_cast<int32_t>(getuid())) return current;
    return g_saved_caller_uid;
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

bool RewriteAtPath(int32_t uid, int dirfd, const char* path,
                   char* absolute, char* output) {
    return ResolveAtPath(dirfd, path, absolute, PATH_MAX)
        && RewriteAbsolutePath(g_rules, g_rule_count, uid, absolute, output, PATH_MAX);
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
    HookGuard guard;
    if (!guard.outer() || path == nullptr) return function(dirfd, path);
    const int32_t uid = EffectiveCallingUid();
    char absolute[PATH_MAX]{};
    char rewritten[PATH_MAX]{};
    if (!RewriteAtPath(uid, dirfd, path, absolute, rewritten)) {
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
    HookGuard guard;
    if (!guard.outer() || old_path == nullptr || new_path == nullptr) {
        return function(old_dirfd, old_path, new_dirfd, new_path);
    }
    const int32_t uid = EffectiveCallingUid();
    char old_absolute[PATH_MAX]{}, old_rewritten[PATH_MAX]{};
    char new_absolute[PATH_MAX]{}, new_rewritten[PATH_MAX]{};
    const bool old_changed = RewriteAtPath(
        uid, old_dirfd, old_path, old_absolute, old_rewritten);
    const bool new_changed = RewriteAtPath(
        uid, new_dirfd, new_path, new_absolute, new_rewritten);
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
    HookGuard guard;
    if (!guard.outer() || path == nullptr) return g_realpath(path, resolved);
    const int32_t uid = EffectiveCallingUid();
    char absolute[PATH_MAX]{}, rewritten[PATH_MAX]{};
    if (!RewriteAtPath(uid, AT_FDCWD, path, absolute, rewritten)) {
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

int64_t HookedClearCallingIdentity(void* state) {
    if (g_identity_clear_depth++ == 0) g_saved_caller_uid = RawCallingUid();
    return g_clear_identity(state);
}

void HookedRestoreCallingIdentity(void* state, int64_t token) {
    g_restore_identity(state, token);
    if (g_identity_clear_depth > 0 && --g_identity_clear_depth == 0) {
        g_saved_caller_uid = -1;
    }
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

uint32_t CollectImages(Image* images, uint32_t capacity) {
    FILE* maps = fopen("/proc/self/maps", "re");
    if (maps == nullptr) return 0;
    uint32_t count = 0;
    char line[PATH_MAX + 256]{};
    while (fgets(line, sizeof(line), maps) != nullptr && count < capacity) {
        unsigned major = 0, minor = 0;
        unsigned long long inode = 0;
        char path[PATH_MAX]{};
        if (sscanf(line, "%*s %*s %*s %x:%x %llu %4095s",
                   &major, &minor, &inode, path) != 4
            || inode == 0 || path[0] != '/'
            || !IsPermanentHookTarget(path)) continue;
        const Image image{static_cast<dev_t>(makedev(major, minor)),
                          static_cast<ino_t>(inode)};
        bool duplicate = false;
        for (uint32_t index = 0; index < count; ++index) {
            if (images[index].device == image.device
                && images[index].inode == image.inode) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) images[count++] = image;
    }
    fclose(maps);
    return count;
}

void Register(zygisk::Api* api, const Image* images, uint32_t count,
              const char* symbol, void* replacement, void** original) {
    for (uint32_t index = 0; index < count; ++index) {
        api->pltHookRegister(images[index].device, images[index].inode,
                             symbol, replacement, original);
    }
}

bool RegisterHooks(zygisk::Api* api) {
    Image images[kMaxImages]{};
    const uint32_t count = CollectImages(images, kMaxImages);
#define REGISTER(symbol, hook, original) \
    Register(api, images, count, symbol, reinterpret_cast<void*>(hook), \
             reinterpret_cast<void**>(&(original)))
    REGISTER("open", HookedOpen, g_open);
    REGISTER("open64", HookedOpen64, g_open64);
    REGISTER("openat", HookedOpenAt, g_openat);
    REGISTER("openat64", HookedOpenAt64, g_openat64);
    REGISTER("stat", HookedStat, g_stat);
    REGISTER("lstat", HookedLstat, g_lstat);
    REGISTER("fstatat", HookedFstatAt, g_fstatat);
    REGISTER("stat64", HookedStat64, g_stat64);
    REGISTER("lstat64", HookedLstat64, g_lstat64);
    REGISTER("fstatat64", HookedFstatAt64, g_fstatat64);
    REGISTER("access", HookedAccess, g_access);
    REGISTER("faccessat", HookedFaccessAt, g_faccessat);
    REGISTER("opendir", HookedOpenDir, g_opendir);
    REGISTER("mkdir", HookedMkdir, g_mkdir);
    REGISTER("mkdirat", HookedMkdirAt, g_mkdirat);
    REGISTER("unlink", HookedUnlink, g_unlink);
    REGISTER("unlinkat", HookedUnlinkAt, g_unlinkat);
    REGISTER("remove", HookedRemove, g_remove);
    REGISTER("rmdir", HookedRmdir, g_rmdir);
    REGISTER("rename", HookedRename, g_rename);
    REGISTER("renameat", HookedRenameAt, g_renameat);
    REGISTER("renameat2", HookedRenameAt2, g_renameat2);
    REGISTER("readlink", HookedReadlink, g_readlink);
    REGISTER("realpath", HookedRealpath, g_realpath);
    REGISTER("chmod", HookedChmod, g_chmod);
    REGISTER("fchmodat", HookedFchmodAt, g_fchmodat);
    REGISTER("chown", HookedChown, g_chown);
    REGISTER("lchown", HookedLchown, g_lchown);
    REGISTER("fchownat", HookedFchownAt, g_fchownat);
    REGISTER("truncate", HookedTruncate, g_truncate);
    REGISTER("truncate64", HookedTruncate64, g_truncate64);
    REGISTER("utimensat", HookedUtimensAt, g_utimensat);
    REGISTER("statvfs", HookedStatVfs, g_statvfs);
    REGISTER("statvfs64", HookedStatVfs64, g_statvfs64);
    REGISTER("inotify_add_watch", HookedInotifyAddWatch, g_inotify_add_watch);
    REGISTER("_ZN7android14IPCThreadState20clearCallingIdentityEv",
             HookedClearCallingIdentity, g_clear_identity);
    REGISTER("_ZN7android14IPCThreadState21restoreCallingIdentityEl",
             HookedRestoreCallingIdentity, g_restore_identity);
#undef REGISTER
    const bool committed = api->pltHookCommit();
    const bool has_open = g_open != nullptr || g_openat != nullptr;
    const bool has_stat = g_stat != nullptr || g_stat64 != nullptr
        || g_fstatat != nullptr || g_fstatat64 != nullptr;
    const bool required = has_open && has_stat
        && g_access != nullptr && g_opendir != nullptr
        && g_mkdir != nullptr && g_remove != nullptr && g_rename != nullptr
        && g_realpath != nullptr;
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
    if (g_clear_identity != nullptr && g_restore_identity != nullptr) capability |= 1u << 9;
    if (g_inotify_add_watch != nullptr) capability |= 1u << 10;
    LOGI("provider virtual hooks: images=%u committed=%d required=%d capability=%x "
         "open=%p openat=%p stat=%p stat64=%p access=%p opendir=%p mkdir=%p "
         "remove=%p rename=%p realpath=%p",
         count, committed ? 1 : 0, required ? 1 : 0, capability,
         g_open, g_openat, g_stat, g_stat64, g_access, g_opendir, g_mkdir,
         g_remove, g_rename, g_realpath);
    return committed && required;
}

}  // namespace

bool Install(zygisk::Api* api, JNIEnv*, const Rule* rules,
             uint32_t rule_count, CallerMode caller_mode) {
    if (api == nullptr || rules == nullptr || rule_count == 0 || rule_count > kMaxRules) {
        return false;
    }
    g_rule_count = 0;
    g_caller_mode = caller_mode;
    for (uint32_t index = 0; index < rule_count; ++index) {
        const Rule& input = rules[index];
        if (input.caller_uid < 10000
            || input.visible_path[0] == '\0' || input.backing_path[0] == '\0'
            || strlen(input.visible_path) >= PATH_MAX
            || strlen(input.backing_path) >= PATH_MAX) return false;
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
                return false;
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
        return false;
    }
    const bool installed = RegisterHooks(api);
    LOGI("provider virtual installed: rules=%u caller_mode=%u binder_ndk=%d binder_cpp=%d "
         "identity_hooks=%d ok=%d",
         g_rule_count, static_cast<unsigned>(g_caller_mode),
         g_ndk_calling_uid != nullptr ? 1 : 0,
         g_binder_self != nullptr && g_binder_calling_uid != nullptr ? 1 : 0,
         g_clear_identity != nullptr && g_restore_identity != nullptr ? 1 : 0,
         installed ? 1 : 0);
    return installed;
}

}  // namespace pathguard::provider_redirect
