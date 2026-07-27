#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pathguard/mount_backend.h"
#include "pathguard/mount_executor.h"
#include "pathguard/mount_info_snapshot.h"

namespace {

int Fail(int code, const char* stage) {
    fprintf(stderr, "deny-anchor probe failed: stage=%s errno=%d\n",
                 stage, errno);
    return code;
}

bool DeniedForUnprivilegedUser(const char* target) {
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        setgroups(0, nullptr);
        if (setgid(12345) != 0 || setuid(12345) != 0) _exit(10);
        char hidden[PATH_MAX]{};
        char created[PATH_MAX]{};
        snprintf(hidden, sizeof(hidden), "%s/hidden", target);
        snprintf(created, sizeof(created), "%s/created", target);
        const int hidden_fd = open(hidden, O_RDONLY | O_CLOEXEC);
        if (hidden_fd >= 0) {
            close(hidden_fd);
            _exit(11);
        }
        const int created_fd = open(
            created, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
        if (created_fd >= 0) {
            close(created_fd);
            _exit(12);
        }
        DIR* directory = opendir(target);
        if (directory != nullptr) {
            closedir(directory);
            _exit(13);
        }
        _exit(0);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

int main() {
    using namespace pathguard;
    if (geteuid() != 0) return Fail(2, "root");
    char root[] = "/data/local/tmp/pathguard-deny-anchor-XXXXXX";
    if (mkdtemp(root) == nullptr) return Fail(3, "mkdtemp");
    char anchor[PATH_MAX]{};
    char target[PATH_MAX]{};
    char hidden[PATH_MAX]{};
    snprintf(anchor, sizeof(anchor), "%s/anchor", root);
    snprintf(target, sizeof(target), "%s/target", root);
    snprintf(hidden, sizeof(hidden), "%s/hidden", target);
    if (mkdir(anchor, 0000) != 0 || mkdir(target, 0700) != 0) {
        return Fail(4, "mkdir");
    }
    const int hidden_fd = open(
        hidden, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (hidden_fd < 0) return Fail(5, "fixture");
    close(hidden_fd);
    if (unshare(CLONE_NEWNS) != 0
        || mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
        return Fail(6, "namespace");
    }

    const MountBackendProbe probe = ProbeDirectoryMountBackends(anchor, target);
    const MountBackendSelection selection = SelectMountBackend(
        kMountActionDenyAnchor, probe.capabilities, true);
    if (selection.backend == MountBackendKind::kUnsupported) {
        return Fail(7, "probe");
    }
    MountInfoSnapshot before;
    PinnedIdentity source;
    PinnedIdentity destination;
    if (CaptureMountInfoSnapshot(&before) != 0
        || PinDirectory(before, anchor, &source) != 0
        || PinDirectory(before, target, &destination) != 0) {
        return Fail(8, "pin");
    }
    CanonicalLocator source_locator;
    CanonicalLocator target_locator;
    snprintf(source_locator.path, sizeof(source_locator.path), "%s", anchor);
    snprintf(target_locator.path, sizeof(target_locator.path), "%s", target);
    MountApplyResult applied = ApplyDirectoryMountRaw(
        selection.backend, 1, source, destination,
        source_locator, target_locator);
    MountInfoSnapshot after;
    if (!applied.ok() || !applied.mutation_happened()
        || CaptureMountInfoSnapshot(&after) != 0) {
        return Fail(9, "apply");
    }
    applied.failure = VerifyDirectoryMount(
        before, after, source, destination, target_locator, &applied.mount);
    if (!applied.ok() || !DeniedForUnprivilegedUser(target)) {
        return Fail(10, "deny-semantics");
    }
    MountRollbackResult rollback = ValidateRollbackDirectoryMount(
        applied.mount, after);
    if (rollback.ok()) rollback = UnmountValidatedDirectoryMount(applied.mount);
    MountInfoSnapshot restored;
    if (!rollback.ok() || CaptureMountInfoSnapshot(&restored) != 0
        || !VerifyRollbackDirectoryMount(applied.mount, restored).ok()
        || access(hidden, F_OK) != 0) {
        return Fail(11, "rollback");
    }
    printf("deny-anchor passed: backend=%u\n",
                static_cast<unsigned>(selection.backend));
    DestroyMountInfoSnapshot(&restored);
    DestroyMountInfoSnapshot(&after);
    DestroyMountInfoSnapshot(&before);
    ClosePinnedIdentity(&destination);
    ClosePinnedIdentity(&source);
    unlink(hidden);
    chmod(anchor, 0700);
    rmdir(target);
    rmdir(anchor);
    rmdir(root);
    return 0;
}
