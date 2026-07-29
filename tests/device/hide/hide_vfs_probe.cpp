#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <string>
#include <string_view>
#include <vector>

#include "hide_probe_contract.h"
#include "hide_vfs_probe_runner.h"

namespace {

using pathguard::hide_probe::Observation;
using pathguard::hide_probe::ProbeStatus;
using pathguard::hide_probe::RenderObservationJson;

thread_local std::string* g_captured_output = nullptr;

struct LinuxDirent64 {
    uint64_t inode;
    int64_t offset;
    unsigned short record_length;
    unsigned char type;
    char name[];
};

void Emit(std::string_view test, std::string_view surface,
          std::string_view path, int64_t return_value, int error_number,
          bool side_effect = false,
          ProbeStatus status = ProbeStatus::kObserved) {
    const std::string json = RenderObservationJson(Observation{
        .test = test,
        .surface = surface,
        .path = path,
        .return_value = return_value,
        .error_number = error_number,
        .side_effect = side_effect,
        .status = status,
    });
    if (g_captured_output != nullptr) {
        g_captured_output->append(json);
        g_captured_output->push_back('\n');
    } else {
        puts(json.c_str());
    }
}

int NormalizedFdResult(int fd) {
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

bool SplitPath(std::string_view path, std::string* parent, std::string* name) {
    while (path.size() > 1 && path.back() == '/') path.remove_suffix(1);
    const size_t separator = path.find_last_of('/');
    if (separator == std::string_view::npos || separator + 1 == path.size()) {
        return false;
    }
    *parent = separator == 0 ? "/" : std::string(path.substr(0, separator));
    *name = std::string(path.substr(separator + 1));
    return true;
}

int DirectoryContainsLibc(const char* parent, const char* name, int* error) {
    errno = 0;
    DIR* directory = opendir(parent);
    if (directory == nullptr) {
        *error = errno;
        return -1;
    }
    int found = 0;
    while (dirent* entry = readdir(directory)) {
        if (strcmp(entry->d_name, name) == 0) {
            found = 1;
            break;
        }
    }
    const int read_error = errno;
    if (closedir(directory) != 0 && read_error == 0) {
        *error = errno;
        return -1;
    }
    if (read_error != 0) {
        *error = read_error;
        return -1;
    }
    *error = 0;
    return found;
}

int DirectoryContainsRaw(int parent_fd, const char* name, int* error) {
    if (lseek(parent_fd, 0, SEEK_SET) < 0) {
        *error = errno;
        return -1;
    }
    alignas(LinuxDirent64) char buffer[4096];
    for (;;) {
        errno = 0;
        const long bytes = syscall(
            __NR_getdents64, parent_fd, buffer, sizeof(buffer));
        if (bytes < 0) {
            *error = errno;
            return -1;
        }
        if (bytes == 0) {
            *error = 0;
            return 0;
        }
        size_t position = 0;
        while (position < static_cast<size_t>(bytes)) {
            const auto* entry = reinterpret_cast<const LinuxDirent64*>(
                buffer + position);
            if (entry->record_length < sizeof(LinuxDirent64)
                || position + entry->record_length
                    > static_cast<size_t>(bytes)) {
                *error = EIO;
                return -1;
            }
            if (strcmp(entry->name, name) == 0) {
                *error = 0;
                return 1;
            }
            position += entry->record_length;
        }
    }
}

void ObservePath(std::string_view label, const std::string& path) {
    std::string parent;
    std::string name;
    if (!SplitPath(path, &parent, &name)) {
        Emit(label, "direct_vfs", path, -1, EINVAL, false,
             ProbeStatus::kSetupError);
        return;
    }

    struct stat metadata {};
    errno = 0;
    const int stat_result = stat(path.c_str(), &metadata);
    Emit(std::string(label) + ".stat", "direct_vfs", path, stat_result,
         stat_result == 0 ? 0 : errno);

    errno = 0;
    const int lstat_result = lstat(path.c_str(), &metadata);
    Emit(std::string(label) + ".lstat", "direct_vfs", path, lstat_result,
         lstat_result == 0 ? 0 : errno);

    errno = 0;
    const int access_result = access(path.c_str(), F_OK);
    Emit(std::string(label) + ".access", "direct_vfs", path, access_result,
         access_result == 0 ? 0 : errno);

    errno = 0;
    const int absolute_fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    const int absolute_error = absolute_fd < 0 ? errno : 0;
    Emit(std::string(label) + ".open", "direct_vfs", path,
         NormalizedFdResult(absolute_fd), absolute_error);

    errno = 0;
    DIR* directory = opendir(path.c_str());
    const int opendir_error = directory == nullptr ? errno : 0;
    const int opendir_result = directory == nullptr ? -1 : 0;
    if (directory != nullptr) closedir(directory);
    Emit(std::string(label) + ".opendir", "direct_vfs", path,
         opendir_result, opendir_error);

    int list_error = 0;
    const int listed = DirectoryContainsLibc(
        parent.c_str(), name.c_str(), &list_error);
    Emit(std::string(label) + ".readdir", "direct_vfs", path,
         listed, list_error);

    errno = 0;
    const int parent_fd = open(
        parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd < 0) {
        Emit(std::string(label) + ".parent_open", "direct_vfs", parent,
             -1, errno);
        return;
    }

    errno = 0;
    const int fstatat_result = fstatat(
        parent_fd, name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW);
    Emit(std::string(label) + ".fstatat", "direct_vfs", path,
         fstatat_result, fstatat_result == 0 ? 0 : errno);

    struct statx extended_metadata {};
    errno = 0;
    const int statx_result = static_cast<int>(syscall(
        __NR_statx, parent_fd, name.c_str(), AT_SYMLINK_NOFOLLOW,
        STATX_BASIC_STATS, &extended_metadata));
    Emit(std::string(label) + ".statx", "direct_vfs", path,
         statx_result, statx_result == 0 ? 0 : errno);

    errno = 0;
    const int faccessat_result = faccessat(parent_fd, name.c_str(), F_OK, 0);
    Emit(std::string(label) + ".faccessat", "direct_vfs", path,
         faccessat_result, faccessat_result == 0 ? 0 : errno);

    errno = 0;
    const int relative_fd = openat(
        parent_fd, name.c_str(), O_RDONLY | O_CLOEXEC);
    const int relative_error = relative_fd < 0 ? errno : 0;
    Emit(std::string(label) + ".openat", "direct_vfs", path,
         NormalizedFdResult(relative_fd), relative_error);

    open_how how{};
    how.flags = O_RDONLY | O_CLOEXEC;
    errno = 0;
    const int openat2_fd = static_cast<int>(syscall(
        __NR_openat2, parent_fd, name.c_str(), &how, sizeof(how)));
    const int openat2_error = openat2_fd < 0 ? errno : 0;
    const ProbeStatus openat2_status =
        openat2_fd < 0 && openat2_error == ENOSYS
            ? ProbeStatus::kUnsupported
            : ProbeStatus::kObserved;
    Emit(std::string(label) + ".openat2", "direct_vfs", path,
         NormalizedFdResult(openat2_fd), openat2_error, false,
         openat2_status);

#if defined(__NR_faccessat2)
    errno = 0;
    const int faccessat2_result = static_cast<int>(syscall(
        __NR_faccessat2, parent_fd, name.c_str(), F_OK, 0));
    const int faccessat2_error = faccessat2_result == 0 ? 0 : errno;
    const ProbeStatus faccessat2_status =
        faccessat2_result < 0 && faccessat2_error == ENOSYS
            ? ProbeStatus::kUnsupported
            : ProbeStatus::kObserved;
    Emit(std::string(label) + ".faccessat2", "direct_vfs", path,
         faccessat2_result, faccessat2_error, false, faccessat2_status);
#else
    Emit(std::string(label) + ".faccessat2", "direct_vfs", path,
         -1, ENOSYS, false, ProbeStatus::kUnsupported);
#endif

    int raw_error = 0;
    const int raw_listed = DirectoryContainsRaw(
        parent_fd, name.c_str(), &raw_error);
    Emit(std::string(label) + ".getdents64", "direct_vfs", path,
         raw_listed, raw_error);
    close(parent_fd);
}

void ObserveRelativeVariants(int sandbox_fd, const std::string& hidden_path) {
    errno = 0;
    const int dot_fd = openat(
        sandbox_fd, "./hidden", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int dot_error = dot_fd < 0 ? errno : 0;
    Emit("sandbox.hidden.openat_dot", "direct_vfs", hidden_path,
         NormalizedFdResult(dot_fd), dot_error);

    errno = 0;
    const bool sub_created = mkdirat(sandbox_fd, "sub", 0700) == 0;
    const int sub_error = sub_created ? 0 : errno;
    errno = 0;
    const int parent_fd = sub_created
        ? openat(sandbox_fd, "sub/../hidden",
                 O_RDONLY | O_DIRECTORY | O_CLOEXEC)
        : -1;
    const int parent_error = sub_created
        ? (parent_fd < 0 ? errno : 0)
        : sub_error;
    Emit("sandbox.hidden.openat_parent", "direct_vfs", hidden_path,
         NormalizedFdResult(parent_fd), parent_error, false,
         sub_created ? ProbeStatus::kObserved : ProbeStatus::kSetupError);
    if (sub_created) unlinkat(sandbox_fd, "sub", AT_REMOVEDIR);

    open_how how{};
    how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS;
    errno = 0;
    const int beneath_fd = static_cast<int>(syscall(
        __NR_openat2, sandbox_fd, "hidden", &how, sizeof(how)));
    const int beneath_error = beneath_fd < 0 ? errno : 0;
    const ProbeStatus beneath_status =
        beneath_fd < 0 && beneath_error == ENOSYS
            ? ProbeStatus::kUnsupported
            : ProbeStatus::kObserved;
    Emit("sandbox.hidden.openat2_beneath", "direct_vfs", hidden_path,
         NormalizedFdResult(beneath_fd), beneath_error, false,
         beneath_status);

    const int original_cwd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (original_cwd < 0) {
        const int cwd_error = errno;
        Emit("sandbox.hidden.chdir_relative", "direct_vfs", hidden_path,
             -1, cwd_error, false, ProbeStatus::kUnsupported);
        return;
    }
    if (fchdir(sandbox_fd) != 0) {
        const int cwd_error = errno;
        close(original_cwd);
        Emit("sandbox.hidden.chdir_relative", "direct_vfs", hidden_path,
             -1, cwd_error);
        return;
    }
    errno = 0;
    const int chdir_fd = open(
        "hidden", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int chdir_error = chdir_fd < 0 ? errno : 0;
    Emit("sandbox.hidden.chdir_relative", "direct_vfs", hidden_path,
         NormalizedFdResult(chdir_fd), chdir_error);
    const int restore_result = fchdir(original_cwd);
    const int restore_error = restore_result == 0 ? 0 : errno;
    close(original_cwd);
    if (restore_result != 0) {
        Emit("process.cwd_restore", "probe", hidden_path, -1,
             restore_error, false, ProbeStatus::kSetupError);
    }
}

bool DirectoryIsEmpty(const char* path) {
    DIR* directory = opendir(path);
    if (directory == nullptr) return false;
    bool empty = true;
    while (dirent* entry = readdir(directory)) {
        if (strcmp(entry->d_name, ".") != 0
            && strcmp(entry->d_name, "..") != 0) {
            empty = false;
            break;
        }
    }
    closedir(directory);
    return empty;
}

bool WriteFileAt(int parent_fd, const char* name, std::string_view content) {
    const int fd = openat(parent_fd, name,
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    size_t written = 0;
    while (written < content.size()) {
        const ssize_t result = write(
            fd, content.data() + written, content.size() - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            close(fd);
            return false;
        }
        written += static_cast<size_t>(result);
    }
    return close(fd) == 0;
}

bool ExistsAt(int parent_fd, const char* name) {
    struct stat metadata {};
    return fstatat(parent_fd, name, &metadata, AT_SYMLINK_NOFOLLOW) == 0;
}

bool FileSizeAt(int parent_fd, const char* name, off_t* size) {
    struct stat metadata {};
    if (fstatat(parent_fd, name, &metadata, 0) != 0) return false;
    *size = metadata.st_size;
    return true;
}

void ObserveMutations(int sandbox_fd, int hidden_fd,
                      const std::string& hidden_path) {
    errno = 0;
    const int create_fd = openat(hidden_fd, "created",
                                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                                 0600);
    const int create_error = create_fd < 0 ? errno : 0;
    if (create_fd >= 0) close(create_fd);
    const bool created = ExistsAt(hidden_fd, "created");
    Emit("sandbox.openat_create", "mutation", hidden_path + "/created",
         create_fd < 0 ? -1 : 0, create_error, created);
    if (created) unlinkat(hidden_fd, "created", 0);

    constexpr std::string_view kCanary = "pathguard-hide-canary";
    if (!WriteFileAt(hidden_fd, "canary", kCanary)) {
        Emit("sandbox.openat_truncate", "mutation",
             hidden_path + "/canary", -1, errno, false,
             ProbeStatus::kSetupError);
        return;
    }
    errno = 0;
    const int truncate_fd = openat(
        hidden_fd, "canary", O_WRONLY | O_TRUNC | O_CLOEXEC);
    const int truncate_error = truncate_fd < 0 ? errno : 0;
    if (truncate_fd >= 0) close(truncate_fd);
    off_t truncated_size = -1;
    const bool truncated = FileSizeAt(hidden_fd, "canary", &truncated_size)
        && truncated_size == 0;
    Emit("sandbox.openat_truncate", "mutation",
         hidden_path + "/canary", truncate_fd < 0 ? -1 : 0,
         truncate_error, truncated);
    WriteFileAt(hidden_fd, "canary", kCanary);

    errno = 0;
    const int mkdir_result = mkdirat(hidden_fd, "created-dir", 0700);
    const int mkdir_error = mkdir_result == 0 ? 0 : errno;
    const bool directory_created = ExistsAt(hidden_fd, "created-dir");
    Emit("sandbox.mkdirat", "mutation", hidden_path + "/created-dir",
         mkdir_result, mkdir_error, directory_created);
    if (directory_created) unlinkat(hidden_fd, "created-dir", AT_REMOVEDIR);

    errno = 0;
    const int unlink_result = unlinkat(hidden_fd, "canary", 0);
    const int unlink_error = unlink_result == 0 ? 0 : errno;
    const bool unlinked = !ExistsAt(hidden_fd, "canary");
    Emit("sandbox.unlinkat", "mutation", hidden_path + "/canary",
         unlink_result, unlink_error, unlinked);
    WriteFileAt(hidden_fd, "canary", kCanary);

    errno = 0;
    const int rename_result = renameat(
        hidden_fd, "canary", sandbox_fd, "moved-canary");
    const int rename_error = rename_result == 0 ? 0 : errno;
    const bool renamed = !ExistsAt(hidden_fd, "canary")
        && ExistsAt(sandbox_fd, "moved-canary");
    Emit("sandbox.renameat_source", "mutation", hidden_path + "/canary",
         rename_result, rename_error, renamed);
    if (renamed) {
        renameat(sandbox_fd, "moved-canary", hidden_fd, "canary");
    } else {
        unlinkat(sandbox_fd, "moved-canary", 0);
        WriteFileAt(hidden_fd, "canary", kCanary);
    }

    errno = 0;
    const int link_result = linkat(
        hidden_fd, "canary", sandbox_fd, "linked-canary", 0);
    const int link_error = link_result == 0 ? 0 : errno;
    const bool linked = ExistsAt(sandbox_fd, "linked-canary");
    Emit("sandbox.linkat_source", "mutation", hidden_path + "/canary",
         link_result, link_error, linked);
    if (linked) unlinkat(sandbox_fd, "linked-canary", 0);

    errno = 0;
    const int remove_directory_setup = mkdirat(hidden_fd, "remove-dir", 0700);
    const int remove_directory_result = remove_directory_setup == 0
        ? unlinkat(hidden_fd, "remove-dir", AT_REMOVEDIR)
        : -1;
    const int remove_directory_error = remove_directory_result == 0 ? 0 : errno;
    const bool directory_removed = remove_directory_setup == 0
        && remove_directory_result == 0
        && !ExistsAt(hidden_fd, "remove-dir");
    Emit("sandbox.rmdir", "mutation", hidden_path + "/remove-dir",
         remove_directory_result, remove_directory_error, directory_removed,
         remove_directory_setup == 0
             ? ProbeStatus::kObserved
             : ProbeStatus::kSetupError);
    unlinkat(hidden_fd, "remove-dir", AT_REMOVEDIR);

#if defined(__NR_renameat2)
    errno = 0;
    const int renameat2_result = static_cast<int>(syscall(
        __NR_renameat2, hidden_fd, "canary", sandbox_fd,
        "moved-canary-2", 1U));  // RENAME_NOREPLACE
    const int renameat2_error = renameat2_result == 0 ? 0 : errno;
    const bool renamed_at2 = renameat2_result == 0
        && !ExistsAt(hidden_fd, "canary")
        && ExistsAt(sandbox_fd, "moved-canary-2");
    const ProbeStatus renameat2_status = renameat2_result < 0
            && renameat2_error == ENOSYS
        ? ProbeStatus::kUnsupported
        : ProbeStatus::kObserved;
    Emit("sandbox.renameat2_source", "mutation", hidden_path + "/canary",
         renameat2_result, renameat2_error, renamed_at2, renameat2_status);
    if (renamed_at2) {
        renameat(sandbox_fd, "moved-canary-2", hidden_fd, "canary");
    } else {
        unlinkat(sandbox_fd, "moved-canary-2", 0);
        WriteFileAt(hidden_fd, "canary", kCanary);
    }
#else
    Emit("sandbox.renameat2_source", "mutation", hidden_path + "/canary",
         -1, ENOSYS, false, ProbeStatus::kUnsupported);
#endif

    errno = 0;
    const int symlink_result = symlinkat(
        "hidden/canary", sandbox_fd, "canary-link");
    const int symlink_error = symlink_result == 0 ? 0 : errno;
    const bool symlink_created = symlink_result == 0
        && ExistsAt(sandbox_fd, "canary-link");
    Emit("sandbox.symlinkat", "mutation", hidden_path + "/canary",
         symlink_result, symlink_error, symlink_created);

    char link_target[64]{};
    errno = 0;
    const ssize_t readlink_result = readlinkat(
        sandbox_fd, "canary-link", link_target, sizeof(link_target) - 1);
    const int readlink_error = readlink_result < 0 ? errno : 0;
    const bool link_target_matches = readlink_result >= 0
        && std::string_view(link_target, static_cast<size_t>(readlink_result))
            == "hidden/canary";
    Emit("sandbox.readlinkat", "direct_vfs", hidden_path + "/canary",
         readlink_result < 0 ? -1 : 0, readlink_error,
         false, link_target_matches
             ? ProbeStatus::kObserved
             : ProbeStatus::kSetupError);
    unlinkat(sandbox_fd, "canary-link", 0);

    errno = 0;
    const int existing_mkdir_result = mkdirat(sandbox_fd, "hidden", 0700);
    const int existing_mkdir_error =
        existing_mkdir_result == 0 ? 0 : errno;
    Emit("sandbox.mkdirat_existing", "mutation", hidden_path,
         existing_mkdir_result, existing_mkdir_error,
         existing_mkdir_result == 0);
}

std::string ReadSmallFile(const char* path) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string output;
    char buffer[4096];
    for (;;) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        output.append(buffer, static_cast<size_t>(count));
    }
    close(fd);
    return output;
}

void CleanupSandbox(int sandbox_fd, int hidden_fd) {
    unlinkat(hidden_fd, "created", 0);
    unlinkat(hidden_fd, "canary", 0);
    unlinkat(hidden_fd, "created-dir", AT_REMOVEDIR);
    unlinkat(sandbox_fd, "moved-canary", 0);
    unlinkat(sandbox_fd, "linked-canary", 0);
    unlinkat(sandbox_fd, "moved-canary-2", 0);
    unlinkat(sandbox_fd, "canary-link", 0);
    unlinkat(hidden_fd, "remove-dir", AT_REMOVEDIR);
    unlinkat(sandbox_fd, "alias", 0);
    unlinkat(sandbox_fd, "hidden", AT_REMOVEDIR);
}

int Fail(std::string_view stage, const std::string& path, int error_number) {
    Emit(stage, "probe", path, -1, error_number, false,
         ProbeStatus::kSetupError);
    return 2;
}

}  // namespace

int pathguard::hide_probe::RunHideVfsProbe(
    int argc, char** argv, std::string* output) {
    struct CaptureScope {
        explicit CaptureScope(std::string* target) : previous(g_captured_output) {
            g_captured_output = target;
        }
        ~CaptureScope() { g_captured_output = previous; }
        std::string* previous;
    } capture_scope(output);

    std::string sandbox;
    std::vector<std::string> observed_paths;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--sandbox") == 0 && index + 1 < argc) {
            sandbox = argv[++index];
        } else if (strcmp(argv[index], "--observe") == 0
                   && index + 1 < argc) {
            observed_paths.emplace_back(argv[++index]);
        } else {
            return Fail("arguments", index < argc ? argv[index] : "", EINVAL);
        }
    }
    if (!pathguard::hide_probe::IsAllowedSandboxPath(sandbox)) {
        return Fail("sandbox_path", sandbox, EINVAL);
    }

    struct stat sandbox_metadata {};
    if (lstat(sandbox.c_str(), &sandbox_metadata) != 0) {
        return Fail("sandbox_lstat", sandbox, errno);
    }
    if (!S_ISDIR(sandbox_metadata.st_mode)
        || sandbox_metadata.st_uid != geteuid()
        || !DirectoryIsEmpty(sandbox.c_str())) {
        return Fail("sandbox_identity", sandbox, EPERM);
    }

    const int sandbox_fd = open(
        sandbox.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (sandbox_fd < 0) return Fail("sandbox_open", sandbox, errno);
    if (mkdirat(sandbox_fd, "hidden", 0700) != 0) {
        const int error_number = errno;
        close(sandbox_fd);
        return Fail("fixture_hidden", sandbox, error_number);
    }
    const int hidden_fd = openat(
        sandbox_fd, "hidden",
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (hidden_fd < 0) {
        const int error_number = errno;
        unlinkat(sandbox_fd, "hidden", AT_REMOVEDIR);
        close(sandbox_fd);
        return Fail("fixture_open", sandbox, error_number);
    }
    if (!WriteFileAt(hidden_fd, "canary", "pathguard-hide-canary")
        || symlinkat("hidden", sandbox_fd, "alias") != 0) {
        const int error_number = errno;
        CleanupSandbox(sandbox_fd, hidden_fd);
        close(hidden_fd);
        close(sandbox_fd);
        return Fail("fixture_content", sandbox, error_number);
    }

    char namespace_identity[128]{};
    const ssize_t namespace_length = readlink(
        "/proc/self/ns/mnt", namespace_identity,
        sizeof(namespace_identity) - 1);
    Emit("process.mount_namespace", "process",
         namespace_length < 0 ? "" : namespace_identity,
         namespace_length < 0 ? -1 : 0,
         namespace_length < 0 ? errno : 0);
    Emit("process.uid", "process", "", geteuid(), 0);

    const std::string mountinfo_before = ReadSmallFile("/proc/self/mountinfo");
    const std::string mounts_before = ReadSmallFile("/proc/self/mounts");
    const std::string mountstats_before = ReadSmallFile("/proc/self/mountstats");

    const std::string hidden_path = sandbox + "/hidden";
    ObservePath("sandbox.hidden", hidden_path);
    ObservePath("sandbox.descendant", hidden_path + "/canary");
    ObservePath("sandbox.symlink_alias", sandbox + "/alias");
    ObserveRelativeVariants(sandbox_fd, hidden_path);
    ObserveMutations(sandbox_fd, hidden_fd, hidden_path);
    for (size_t index = 0; index < observed_paths.size(); ++index) {
        ObservePath("external." + std::to_string(index), observed_paths[index]);
    }

    const std::string mountinfo_after = ReadSmallFile("/proc/self/mountinfo");
    const std::string mounts_after = ReadSmallFile("/proc/self/mounts");
    const std::string mountstats_after = ReadSmallFile("/proc/self/mountstats");
    Emit("proc.mountinfo", "proc", "/proc/self/mountinfo",
         static_cast<int64_t>(mountinfo_after.size()), 0,
         mountinfo_before != mountinfo_after);
    Emit("proc.mounts", "proc", "/proc/self/mounts",
         static_cast<int64_t>(mounts_after.size()), 0,
         mounts_before != mounts_after);
    Emit("proc.mountstats", "proc", "/proc/self/mountstats",
         static_cast<int64_t>(mountstats_after.size()), 0,
         mountstats_before != mountstats_after);

    CleanupSandbox(sandbox_fd, hidden_fd);
    close(hidden_fd);
    const bool clean = DirectoryIsEmpty(sandbox.c_str());
    close(sandbox_fd);
    Emit("probe.complete", "probe", sandbox, clean ? 0 : -1,
         clean ? 0 : ENOTEMPTY, !clean,
         clean ? ProbeStatus::kObserved : ProbeStatus::kSetupError);
    return clean ? 0 : 3;
}

#if !defined(PATHGUARD_HIDE_PROBE_NO_MAIN)
int main(int argc, char** argv) {
    return pathguard::hide_probe::RunHideVfsProbe(argc, argv, nullptr);
}
#endif
