#include <android/log.h>
#include <dlfcn.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/futex.h>
#include <linux/memfd.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pathguard/provider_redirect_hook.hpp"
#include "pathguard/provider_bridge_status.h"
#include "pathguard/provider_lsplant_bridge_api.h"
#include "pathguard/media_query_hook.hpp"
#include "pathguard/mount_backend.h"
#include "pathguard/mount_executor.h"
#include "pathguard/mount_plan_adapter.h"
#include "pathguard/mount_transaction.h"
#include "pathguard/mutation_journal.h"
#include "pathguard/perf_clock.hpp"
#include "pathguard/policy_format.h"
#include "pathguard/policy_pattern_runtime.h"
#include "pathguard/policy_v6_view.h"
#include "pathguard/provenance_protocol.h"
#include "pathguard/runtime_status.h"
#include "pathguard/runtime_status_builder.h"
#include "zygisk.hpp"

namespace {

constexpr char kLogTag[] = "PathGuard";
constexpr char kPolicyPath[] = "run/policy.bin";
constexpr uint32_t kPolicyMagic = pathguard::binary_format::kMagic;
constexpr uint16_t kPolicyFormatVersion = pathguard::binary_format::kFormatVersion;
constexpr size_t kPolicyHeaderSize = pathguard::binary_format::kHeaderSize;
constexpr uint32_t kBootstrapMagic = 0x50474250;
constexpr uint32_t kBootstrapVersion = 6;
constexpr uint32_t kBootstrapFeatureProviderEnabled = 1u;
constexpr uint32_t kStatusSubmissionMagic = 0x53544750;
constexpr uint32_t kStatusSubmissionVersion = 1;
constexpr uint32_t kSharedStatusMagic = 0x48534750;
constexpr uint32_t kSharedStatusVersion = 1;
constexpr size_t kMaxRuntimeStatusBytes = 32768;
constexpr uint32_t kCompanionResultMagic = 0x52534750;
constexpr uint32_t kCompanionResultVersion = 1;
constexpr uint32_t kSharedStateMagic = 0x53534750;
constexpr uint32_t kSharedStateVersion = 5;
constexpr size_t kMaxMountRules = 64;
constexpr size_t kMaxPlanPathBytes = 64 * 1024;
constexpr size_t kMaxProcessNameBytes = 256;
constexpr int kProcessReadyTimeoutMs = 5000;
constexpr int kCompanionIoTimeoutMs = 5000;
constexpr int kRuntimeStatusTimeoutMs = 5000;
constexpr uint32_t kProviderBridgeWaitTimeoutMs = 4500;
constexpr int kAppResultTimeoutMs = 300;
constexpr int kPreflightCompletionGraceMs = 500;
constexpr int kApplyingCompletionGraceMs = 500;
constexpr int kChildTerminateGraceMs = 1000;
constexpr int kApplyingOwnerDeathTimeoutMs = 10000;
constexpr uint8_t kDenyAction = 0;
constexpr uint8_t kRedirectAction = 1;
constexpr uint8_t kLiteralMatch = 0;
constexpr uint8_t kMountDomain = 0;
constexpr uint8_t kAppPathDomain = 1;
constexpr uint8_t kProviderDomain = 2;
constexpr char kDenyAnchorRelativePath[] = "run/deny-anchor";
constexpr char kDiagnosticPackage[] = "org.localsend.localsend_app";
constexpr char kExternalStorageProviderProcess[] = "com.android.externalstorage";
constexpr char kMainlineMediaProviderProcess[] = "com.android.providers.media.module";
constexpr char kExternalStorageProviderApk[] =
    "/system/priv-app/ExternalStorageProvider/ExternalStorageProvider.apk";
constexpr char kMediaProviderApk[] =
    "/apex/com.android.mediaprovider/priv-app/MediaProvider@TKQ1.220829.002/MediaProvider.apk";
constexpr uint64_t kAliothDeploymentProfileId = UINT64_C(0x616c696f74682d31);
constexpr uint64_t kAliothDocumentsProfileId = UINT64_C(0x616c696f74682d64);
constexpr uint64_t kAliothMediaProfileId = UINT64_C(0x616c696f74682d6d);
constexpr uint8_t kAliothDocumentsSha256[32] = {
    0x44,0xa4,0x2e,0xee,0xf3,0x64,0xa1,0xbd,0x53,0x8e,0x75,0xc3,0x55,0x3e,0x45,0xc9,
    0xad,0xfb,0xd0,0x4a,0x4b,0x7a,0xf2,0xdc,0xfa,0xa3,0xf7,0x6b,0xb4,0x48,0x85,0x6e};
constexpr uint8_t kAliothMediaSha256[32] = {
    0xf8,0xf7,0x1e,0xae,0xdd,0x78,0xa1,0xbb,0x0c,0x3b,0xb3,0xd8,0x14,0x05,0xf2,0x52,
    0x92,0x21,0xfe,0x63,0x58,0xca,0x5f,0xe4,0xce,0x74,0xa5,0xc3,0x85,0x3c,0xa9,0xed};
#ifndef PATHGUARD_TEST_MOUNT_DELAY_MS
#define PATHGUARD_TEST_MOUNT_DELAY_MS 0
#endif
#ifndef PATHGUARD_TEST_PRE_LEASE_DELAY_MS
#define PATHGUARD_TEST_PRE_LEASE_DELAY_MS 0
#endif
#ifndef PATHGUARD_TEST_CRASH_AFTER_MOUNT
#define PATHGUARD_TEST_CRASH_AFTER_MOUNT 0
#endif
#ifndef PATHGUARD_TEST_ROLLBACK_FAILURE
#define PATHGUARD_TEST_ROLLBACK_FAILURE 0
#endif

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

struct PlannedMount {
    uint8_t action = 0;
    uint32_t visible_path = 0;
    uint32_t backing_path = 0;
};

struct RuntimeStorageTopology {
    uint64_t generation = 0;
    uint32_t user_id = 0;
    char visible_root[PATH_MAX]{};
    char source_root[PATH_MAX]{};
    pathguard::MountPathIdentity visible_identity;
    pathguard::MountPathIdentity source_identity;
};

struct ProcessPlan {
    uint64_t snapshot_generation = 0;
    uint64_t plan_generation = 0;
    uint32_t policy_flags = 0;
    uint32_t count = 0;
    uint32_t path_bytes = 1;
    bool provider_enabled = false;
    bool app_path_status_available = false;
    pathguard::provider_redirect::InstallResult app_path_install;
    RuntimeStorageTopology topology;
    char process_name[kMaxProcessNameBytes]{};
    PlannedMount mounts[kMaxMountRules]{};
    char paths[kMaxPlanPathBytes]{};
};

struct PathInstallTelemetry {
    uint64_t observed_capabilities = 0;
    uint64_t observed_operations = 0;
    uint64_t snapshot_generation = 0;
    uint64_t capability_generation = 0;
    uint64_t hazard_slot_acquire_fail_total = 0;
    uint64_t snapshot_reload_rejected_retire_limit_total = 0;
    uint64_t retired_snapshot_bytes_high_watermark = 0;
    uint32_t hazard_slots_in_use_high_watermark = 0;
    uint32_t retired_snapshot_count_high_watermark = 0;
    uint32_t flags = 0;
    uint32_t reserved = 0;
};

struct Sha256State {
    uint32_t h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                     0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    uint64_t bits = 0;
    uint8_t block[64]{};
    size_t used = 0;
};

uint32_t Sha256Rotate(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32u - count));
}

void Sha256Compress(Sha256State* state, const uint8_t* block) {
    static constexpr uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64]{};
    for (size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i*4]) << 24)
            | (static_cast<uint32_t>(block[i*4+1]) << 16)
            | (static_cast<uint32_t>(block[i*4+2]) << 8)
            | static_cast<uint32_t>(block[i*4+3]);
    }
    for (size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = Sha256Rotate(w[i-15],7) ^ Sha256Rotate(w[i-15],18) ^ (w[i-15] >> 3);
        const uint32_t s1 = Sha256Rotate(w[i-2],17) ^ Sha256Rotate(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=state->h[0], b=state->h[1], c=state->h[2], d=state->h[3],
             e=state->h[4], f=state->h[5], g=state->h[6], h=state->h[7];
    for (size_t i = 0; i < 64; ++i) {
        const uint32_t s1 = Sha256Rotate(e,6) ^ Sha256Rotate(e,11) ^ Sha256Rotate(e,25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + k[i] + w[i];
        const uint32_t s0 = Sha256Rotate(a,2) ^ Sha256Rotate(a,13) ^ Sha256Rotate(a,22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state->h[0]+=a; state->h[1]+=b; state->h[2]+=c; state->h[3]+=d;
    state->h[4]+=e; state->h[5]+=f; state->h[6]+=g; state->h[7]+=h;
}

bool Sha256File(const char* path, uint8_t output[32]) {
    if (path == nullptr || output == nullptr) return false;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    Sha256State state;
    uint8_t input[8192]{};
    bool ok = true;
    for (;;) {
        const ssize_t count = read(fd, input, sizeof(input));
        if (count < 0) { ok = false; break; }
        if (count == 0) break;
        state.bits += static_cast<uint64_t>(count) * 8u;
        size_t offset = 0;
        while (offset < static_cast<size_t>(count)) {
            const size_t available = sizeof(state.block) - state.used;
            const size_t remaining = static_cast<size_t>(count) - offset;
            const size_t take = available < remaining ? available : remaining;
            memcpy(state.block + state.used, input + offset, take);
            state.used += take; offset += take;
            if (state.used == sizeof(state.block)) {
                Sha256Compress(&state, state.block); state.used = 0;
            }
        }
    }
    close(fd);
    if (!ok || state.used >= sizeof(state.block)) return false;
    state.block[state.used++] = 0x80;
    if (state.used > 56) { memset(state.block + state.used, 0, 64 - state.used); Sha256Compress(&state, state.block); state.used = 0; }
    memset(state.block + state.used, 0, 56 - state.used);
    for (size_t i = 0; i < 8; ++i) state.block[56 + i] = static_cast<uint8_t>(state.bits >> (56 - i*8));
    Sha256Compress(&state, state.block);
    for (size_t i = 0; i < 8; ++i) { output[i*4] = static_cast<uint8_t>(state.h[i] >> 24); output[i*4+1] = static_cast<uint8_t>(state.h[i] >> 16); output[i*4+2] = static_cast<uint8_t>(state.h[i] >> 8); output[i*4+3] = static_cast<uint8_t>(state.h[i]); }
    return true;
}

struct ModuleBytes { uint8_t* data = nullptr; size_t size = 0; };

void ReleaseModuleBytes(ModuleBytes* bytes) {
    if (bytes == nullptr) return;
    free(bytes->data); bytes->data = nullptr; bytes->size = 0;
}

bool ReadModuleFile(int module_dir, const char* relative, ModuleBytes* output) {
    if (module_dir < 0 || relative == nullptr || output == nullptr) return false;
    const int fd = openat(module_dir, relative, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ReleaseModuleBytes(output);
    output->data = static_cast<uint8_t*>(malloc(1024 * 1024));
    if (output->data == nullptr) { close(fd); return false; }
    bool ok = true;
    for (;;) {
        const ssize_t count = read(fd, output->data + output->size,
                                   1024 * 1024 - output->size);
        if (count < 0) { ok = false; break; }
        if (count == 0) break;
        output->size += static_cast<size_t>(count);
        if (output->size == 1024 * 1024) { ok = false; break; }
    }
    close(fd);
    if (!ok || output->size == 0) { ReleaseModuleBytes(output); return false; }
    return true;
}

bool SameDigest(const uint8_t* left, const uint8_t* right) {
    return left != nullptr && right != nullptr
        && memcmp(left, right, 32) == 0;
}

bool PrepareProviderLsplant(
        int module_dir, bool media, pathguard::ProviderJavaBridgeStatusV1* probe,
        void** handle, PathGuardLsplantInitializeV1* initialize,
        PathGuardLsplantInstallPassthroughV1* install,
        PathGuardLsplantWaitPassthroughV1* wait,
        PathGuardLsplantConfigureMappingV1* configure,
        ModuleBytes* dex) {
    if (module_dir < 0 || probe == nullptr || handle == nullptr
        || initialize == nullptr || install == nullptr || wait == nullptr
        || configure == nullptr
        || dex == nullptr) {
        return false;
    }
    *probe = {};
    probe->kind = media ? 2 : 1;
    probe->deployment_profile_id = kAliothDeploymentProfileId;
    probe->provider_profile_id = media ? kAliothMediaProfileId
                                       : kAliothDocumentsProfileId;
    const char* apk = media ? kMediaProviderApk : kExternalStorageProviderApk;
    uint8_t digest[32]{};
    const bool hashed = Sha256File(apk, digest);
    const bool hash_match = hashed && SameDigest(
        digest, media ? kAliothMediaSha256 : kAliothDocumentsSha256);
    if (!hash_match) return false;
    probe->build_matched = true;
    char fd_path[128]{};
#if defined(__LP64__)
    constexpr const char* kProviderAbi = "arm64-v8a";
#else
    constexpr const char* kProviderAbi = "armeabi-v7a";
#endif
    if (snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d/provider/%s/libpathguard_lsplant.so",
                 module_dir, kProviderAbi) <= 0) return false;
    void* library = dlopen(fd_path, RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) return false;
    auto init = reinterpret_cast<PathGuardLsplantInitializeV1>(
        dlsym(library, "pathguard_lsplant_initialize_v1"));
    auto hook = reinterpret_cast<PathGuardLsplantInstallPassthroughV1>(
        dlsym(library, "pathguard_lsplant_install_passthrough_v1"));
    auto wait_for_hook = reinterpret_cast<PathGuardLsplantWaitPassthroughV1>(
        dlsym(library, "pathguard_lsplant_wait_passthrough_v1"));
    auto configure_mapping = reinterpret_cast<PathGuardLsplantConfigureMappingV1>(
        dlsym(library, "pathguard_lsplant_configure_mapping_v1"));
    if (init == nullptr || hook == nullptr || wait_for_hook == nullptr
        || configure_mapping == nullptr
        || !ReadModuleFile(module_dir, "provider/provider-hooker.dex", dex)) {
        dlclose(library); return false;
    }
    *handle = library; *initialize = init; *install = hook; *wait = wait_for_hook;
    *configure = configure_mapping;
    probe->library_loaded = true;
    return true;
}

constexpr uint32_t kPathInstallAvailable = 1u << 0;
constexpr uint32_t kPathInstallAttempted = 1u << 1;
constexpr uint32_t kPathInstallCommitted = 1u << 2;
constexpr uint32_t kPathInstallActive = 1u << 3;
constexpr uint32_t kPathIdentityAttempted = 1u << 4;
constexpr uint32_t kPathIdentityHooks = 1u << 5;

struct BootstrapHeader {
    uint32_t magic;
    uint32_t version;
    int32_t pid;
    int32_t uid;
    uint32_t rule_count;
    uint32_t policy_flags;
    uint64_t snapshot_generation;
    uint64_t plan_generation;
    uint32_t process_name_length;
    uint32_t feature_flags;
    PathInstallTelemetry app_path_install;
};

struct StatusSubmissionHeader {
    uint32_t magic = kStatusSubmissionMagic;
    uint32_t version = kStatusSubmissionVersion;
    int32_t pid = -1;
    uint32_t uid = 0;
    uint64_t process_start_time = 0;
};

struct SharedRuntimeStatus {
    uint32_t magic = kSharedStatusMagic;
    uint32_t version = kSharedStatusVersion;
    alignas(4) uint32_t state = 0;
    uint32_t process_name_length = 0;
    uint32_t status_length = 0;
    char process_name[kMaxProcessNameBytes]{};
    char text[kMaxRuntimeStatusBytes]{};
};

struct PolicyLoadPerf {
    uint64_t open_map_ns = 0;
    uint64_t lookup_ns = 0;
    uint64_t unmap_ns = 0;
};

struct MountPerfResult {
    int32_t result = EIO;
    uint32_t rule_count = 0;
    uint32_t backend = 0;
    uint32_t backend_reason = 0;
    uint32_t failure_stage = 0;
    uint32_t rollback_failed = 0;
    uint32_t runtime_reason = 0;
    uint32_t mountinfo_snapshot_count = 0;
    uint64_t topology_generation = 0;
    uint64_t setns_ns = 0;
    uint64_t topology_ns = 0;
    uint64_t source_pin_ns = 0;
    uint64_t policy_revalidate_ns = 0;
    uint64_t propagation_check_ns = 0;
    uint64_t propagation_ns = 0;
    uint64_t mount_total_ns = 0;
    uint64_t mount_max_ns = 0;
    uint64_t rollback_ns = 0;
    uint64_t mountinfo_read_ns = 0;
    uint64_t mountinfo_parse_ns = 0;
};

struct CompanionResult {
    uint32_t magic = kCompanionResultMagic;
    uint32_t version = kCompanionResultVersion;
    uint64_t ready_ns = 0;
    MountPerfResult mount;
};

struct SharedMountState {
    uint32_t magic = kSharedStateMagic;
    uint32_t version = kSharedStateVersion;
    alignas(4) uint32_t status = static_cast<uint32_t>(
        pathguard::MountTransactionState::kPending);
    CompanionResult result;
};

using MountMutationJournal =
    pathguard::MutationJournal<pathguard::AppliedMount, kMaxMountRules>;

struct MountTransactionWorkspace {
    pathguard::PinnedIdentity sources[kMaxMountRules]{};
    pathguard::PinnedIdentity targets[kMaxMountRules]{};
    pathguard::CanonicalLocator source_locators[kMaxMountRules]{};
    pathguard::CanonicalLocator target_locators[kMaxMountRules]{};
    pathguard::MountApplyTiming timings[kMaxMountRules]{};
    uint64_t target_pin_ns[kMaxMountRules]{};
    uint8_t source_indexes[kMaxMountRules]{};
    MountMutationJournal journal;
};

MountTransactionWorkspace* CreateMountTransactionWorkspace() {
    void* mapping = mmap(nullptr, sizeof(MountTransactionWorkspace),
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return nullptr;
    memset(mapping, 0, sizeof(MountTransactionWorkspace));
    return static_cast<MountTransactionWorkspace*>(mapping);
}

void DestroyMountTransactionWorkspace(MountTransactionWorkspace* workspace) {
    if (workspace == nullptr) return;
    munmap(workspace, sizeof(*workspace));
}

bool IsAllowedTarget(const char* path);
bool ExpandRuntimePath(const char* input, uint32_t user_id,
                       char* output, size_t capacity);

bool ReadFully(int fd, void* buffer, size_t size) {
    auto* output = static_cast<uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t received = read(fd, output, size);
        if (received == 0) return false;
        if (received < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        output += received;
        size -= static_cast<size_t>(received);
    }
    return true;
}

bool WriteFully(int fd, const void* buffer, size_t size) {
    const auto* input = static_cast<const uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t written = send(fd, input, size, MSG_NOSIGNAL);
        if (written == 0) return false;
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        input += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

PathInstallTelemetry EncodePathInstallTelemetry(
        const pathguard::provider_redirect::InstallResult& result,
        bool available) {
    PathInstallTelemetry telemetry;
    telemetry.observed_capabilities = result.observed_capabilities;
    telemetry.observed_operations = result.observed_operations;
    telemetry.snapshot_generation = result.snapshot_generation;
    telemetry.capability_generation = result.capability_generation;
    telemetry.hazard_slot_acquire_fail_total =
        result.hazard_slot_acquire_fail_total;
    telemetry.snapshot_reload_rejected_retire_limit_total =
        result.snapshot_reload_rejected_retire_limit_total;
    telemetry.retired_snapshot_bytes_high_watermark =
        result.retired_snapshot_bytes_high_watermark;
    telemetry.hazard_slots_in_use_high_watermark =
        result.hazard_slots_in_use_high_watermark;
    telemetry.retired_snapshot_count_high_watermark =
        result.retired_snapshot_count_high_watermark;
    telemetry.flags = (available ? kPathInstallAvailable : 0u)
        | (result.hook_registration_attempted ? kPathInstallAttempted : 0u)
        | (result.hooks_committed ? kPathInstallCommitted : 0u)
        | (result.virtualization_active ? kPathInstallActive : 0u)
        | (result.identity_hook_attempted ? kPathIdentityAttempted : 0u)
        | (result.identity_hooks ? kPathIdentityHooks : 0u);
    return telemetry;
}

pathguard::provider_redirect::InstallResult DecodePathInstallTelemetry(
        const PathInstallTelemetry& telemetry) {
    pathguard::provider_redirect::InstallResult result;
    result.hook_registration_attempted =
        (telemetry.flags & kPathInstallAttempted) != 0;
    result.hooks_committed = (telemetry.flags & kPathInstallCommitted) != 0;
    result.virtualization_active = (telemetry.flags & kPathInstallActive) != 0;
    result.identity_hook_attempted =
        (telemetry.flags & kPathIdentityAttempted) != 0;
    result.identity_hooks = (telemetry.flags & kPathIdentityHooks) != 0;
    result.observed_capabilities = telemetry.observed_capabilities;
    result.observed_operations = telemetry.observed_operations;
    result.snapshot_generation = telemetry.snapshot_generation;
    result.capability_generation = telemetry.capability_generation;
    result.hazard_slot_acquire_fail_total =
        telemetry.hazard_slot_acquire_fail_total;
    result.snapshot_reload_rejected_retire_limit_total =
        telemetry.snapshot_reload_rejected_retire_limit_total;
    result.retired_snapshot_bytes_high_watermark =
        telemetry.retired_snapshot_bytes_high_watermark;
    result.hazard_slots_in_use_high_watermark =
        telemetry.hazard_slots_in_use_high_watermark;
    result.retired_snapshot_count_high_watermark =
        telemetry.retired_snapshot_count_high_watermark;
    return result;
}

bool WriteRegularFileFully(int fd, const void* buffer, size_t size) {
    const auto* input = static_cast<const uint8_t*>(buffer);
    while (size > 0) {
        const ssize_t written = write(fd, input, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        input += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

void SetSocketTimeout(int fd, int timeout_ms) {
    const timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

uint16_t ReadLe16(const uint8_t* value) {
    return static_cast<uint16_t>(value[0])
        | static_cast<uint16_t>(value[1]) << 8;
}

uint32_t ReadLe32(const uint8_t* value) {
    uint32_t result = 0;
    for (int index = 0; index < 4; ++index) {
        result |= static_cast<uint32_t>(value[index]) << (index * 8);
    }
    return result;
}

uint64_t ReadLe64(const uint8_t* value) {
    uint64_t result = 0;
    for (int index = 0; index < 8; ++index) {
        result |= static_cast<uint64_t>(value[index]) << (index * 8);
    }
    return result;
}

const char* ReadPolicyString(const uint8_t* data, size_t size,
                             uint32_t string_offset, uint32_t relative_offset) {
    if (string_offset > size || relative_offset >= size - string_offset) return nullptr;
    const char* value = reinterpret_cast<const char*>(data + string_offset + relative_offset);
    const size_t remaining = size - string_offset - relative_offset;
    return memchr(value, '\0', remaining) == nullptr ? nullptr : value;
}

bool ListContains(const char* list, const char* expected) {
    if (list == nullptr || expected == nullptr) return false;
    if (strcmp(list, "*") == 0) return true;
    const size_t expected_length = strlen(expected);
    const char* current = list;
    while (*current != '\0') {
        const char* separator = strchr(current, ',');
        const size_t length = separator == nullptr
            ? strlen(current)
            : static_cast<size_t>(separator - current);
        if (length == expected_length && memcmp(current, expected, length) == 0) return true;
        if (separator == nullptr) break;
        current = separator + 1;
    }
    return false;
}

bool ProcessMatches(const char* package_name, const char* process_list, const char* process_name) {
    if (strcmp(process_list, "*") != 0) return ListContains(process_list, process_name);
    const size_t package_length = strlen(package_name);
    return strcmp(package_name, process_name) == 0
        || (strncmp(package_name, process_name, package_length) == 0
            && process_name[package_length] == ':');
}

const char* PlanPath(const ProcessPlan& plan, uint32_t offset) {
    return offset < plan.path_bytes ? plan.paths + offset : nullptr;
}

bool StorePlanPath(ProcessPlan* plan, const char* path, uint32_t* offset) {
    if (plan == nullptr || path == nullptr || offset == nullptr) return false;
    const size_t length = strlen(path) + 1;
    if (length > kMaxPlanPathBytes - plan->path_bytes) return false;
    *offset = plan->path_bytes;
    memcpy(plan->paths + plan->path_bytes, path, length);
    plan->path_bytes += static_cast<uint32_t>(length);
    return true;
}

bool IsExecutableMountAction(uint8_t action) {
    return action == kDenyAction || action == kRedirectAction;
}

pathguard::MountActionMask RequiredMountActions(const ProcessPlan& plan) {
    pathguard::MountActionMask required = 0;
    for (uint32_t index = 0; index < plan.count; ++index) {
        if (plan.mounts[index].action == kDenyAction) {
            required |= pathguard::kMountActionDenyAnchor;
        } else if (plan.mounts[index].action == kRedirectAction) {
            required |= pathguard::kMountActionRedirect;
        } else {
            return 0;
        }
    }
    return required;
}

bool BuildDenyAnchorPath(int module_dir_fd, char* output, size_t capacity) {
    if (module_dir_fd < 0 || output == nullptr || capacity == 0) return false;
    const int run_fd = openat(module_dir_fd, "run",
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (run_fd < 0) return false;
    const int anchor_fd = openat(run_fd, "deny-anchor",
                                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(run_fd);
    if (anchor_fd < 0) return false;
    struct stat anchor_stat {};
    bool valid = fstat(anchor_fd, &anchor_stat) == 0
        && S_ISDIR(anchor_stat.st_mode) && anchor_stat.st_uid == 0
        && (anchor_stat.st_mode & 0777) == 0;
    const int scan_fd = valid ? dup(anchor_fd) : -1;
    DIR* directory = scan_fd >= 0 ? fdopendir(scan_fd) : nullptr;
    if (directory == nullptr) valid = false;
    while (valid) {
        errno = 0;
        dirent* entry = readdir(directory);
        if (entry == nullptr) {
            valid = errno == 0;
            break;
        }
        if (strcmp(entry->d_name, ".") != 0
            && strcmp(entry->d_name, "..") != 0) {
            valid = false;
        }
    }
    if (directory != nullptr) closedir(directory);

    char descriptor_path[64]{};
    char module_path[PATH_MAX]{};
    const int descriptor_written = snprintf(
        descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d",
        module_dir_fd);
    const ssize_t module_length = descriptor_written > 0
            && static_cast<size_t>(descriptor_written) < sizeof(descriptor_path)
        ? readlink(descriptor_path, module_path, sizeof(module_path) - 1)
        : -1;
    if (module_length <= 0) valid = false;
    if (valid) {
        module_path[module_length] = '\0';
        const int written = snprintf(output, capacity, "%s/%s", module_path,
                                     kDenyAnchorRelativePath);
        struct stat path_stat {};
        valid = module_path[0] == '/'
            && strstr(module_path, " (deleted)") == nullptr
            && written > 0 && static_cast<size_t>(written) < capacity
            && lstat(output, &path_stat) == 0
            && path_stat.st_dev == anchor_stat.st_dev
            && path_stat.st_ino == anchor_stat.st_ino;
    }
    close(anchor_fd);
    return valid;
}

bool BuildPathUnderRoot(const char* root, const char* logical_path,
                        char* output, size_t output_size);

bool BuildMountSourcePath(const ProcessPlan& plan,
                          const PlannedMount& mount,
                          const char* deny_anchor_path,
                          char* output, size_t capacity) {
    if (mount.action == kDenyAction) {
        if (deny_anchor_path == nullptr || deny_anchor_path[0] != '/') {
            return false;
        }
        const int written = snprintf(output, capacity, "%s", deny_anchor_path);
        return written > 0 && static_cast<size_t>(written) < capacity;
    }
    const char* backing = PlanPath(plan, mount.backing_path);
    return mount.action == kRedirectAction && backing != nullptr
        && backing[0] != '\0'
        && BuildPathUnderRoot(plan.topology.source_root, backing,
                              output, capacity);
}

bool LoadProcessPlan(int module_dir, const char* process_name, jint uid,
                     ProcessPlan* plan, PolicyLoadPerf* perf) {
    if (plan == nullptr || process_name == nullptr
        || strlen(process_name) >= kMaxProcessNameBytes) return false;
    const bool diagnostic = process_name != nullptr
        && strcmp(process_name, kDiagnosticPackage) == 0;
    *plan = {};
    plan->path_bytes = 1;
    snprintf(plan->process_name, sizeof(plan->process_name), "%s", process_name);
    const uint64_t open_map_started = pathguard::perf::NowNs();
    const int policy_fd = openat(module_dir, kPolicyPath, O_RDONLY | O_CLOEXEC);
    if (policy_fd < 0) {
        if (diagnostic) LOGE("policy_open_failed errno=%d", errno);
        if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
        return false;
    }

    struct stat file_stat {};
    if (fstat(policy_fd, &file_stat) != 0 || file_stat.st_size < static_cast<off_t>(kPolicyHeaderSize)) {
        if (diagnostic) LOGE("policy_stat_invalid errno=%d size=%lld", errno,
                             static_cast<long long>(file_stat.st_size));
        close(policy_fd);
        if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
        return false;
    }
    const size_t size = static_cast<size_t>(file_stat.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, policy_fd, 0);
    close(policy_fd);
    if (perf != nullptr) perf->open_map_ns = pathguard::perf::ElapsedNs(open_map_started);
    if (mapping == MAP_FAILED) {
        if (diagnostic) LOGE("policy_mmap_failed errno=%d", errno);
        return false;
    }

    const uint64_t lookup_started = pathguard::perf::NowNs();
    const auto finish = [&](bool result) {
        if (perf != nullptr) perf->lookup_ns = pathguard::perf::ElapsedNs(lookup_started);
        const uint64_t unmap_started = pathguard::perf::NowNs();
        munmap(mapping, size);
        if (perf != nullptr) perf->unmap_ns = pathguard::perf::ElapsedNs(unmap_started);
        return result;
    };

    const auto* data = static_cast<const uint8_t*>(mapping);
    pathguard::policy_v6_view::PolicyV6View policy;
    pathguard::policy_v6_view::Error policy_error{};
    if (!policy.Initialize(data, size, &policy_error)) {
        if (diagnostic) LOGE("policy_v6_decode_failed size=%zu", size);
        return finish(false);
    }
    const char* process_separator = strchr(process_name, ':');
    const size_t package_length = process_separator == nullptr
        ? strlen(process_name)
        : static_cast<size_t>(process_separator - process_name);
    if (package_length == 0) return finish(false);
    pathguard::policy_v6_view::PackageRef package;
    if (!policy.FindPackage(process_name, package_length, &package)) {
        if (diagnostic) LOGE("policy_package_miss uid=%d", uid);
        return finish(false);
    }
    const uint32_t user_id = static_cast<uint32_t>(uid) / 100000u;
    if (!policy.PackageMatchesScope(package, process_name, strlen(process_name),
                                    user_id)) {
        if (diagnostic) LOGE("policy_scope_miss user=%u", user_id);
        return finish(false);
    }
    pathguard::LiteralMountPlan mount_plan;
    if (!pathguard::BuildLiteralMountPlan(policy, package, &mount_plan)) {
        if (diagnostic) LOGE("policy_mount_plan_empty_or_invalid");
        return finish(false);
    }
    plan->snapshot_generation = mount_plan.content_generation;
    plan->policy_flags = mount_plan.policy_flags;
    plan->plan_generation = mount_plan.plan_generation;
    plan->provider_enabled =
        (package.flags & pathguard::binary_format::kPackageFlagProviderEnabled) != 0;
    for (uint32_t index = 0; index < mount_plan.count; ++index) {
        const pathguard::LiteralMountPlanEntry& source = mount_plan.entries[index];
        const char* visible = mount_plan.Path(source.visible_path);
        const char* backing = mount_plan.Path(source.target_path);
        if (visible == nullptr || backing == nullptr || plan->count >= kMaxMountRules) {
            plan->count = 0;
            return finish(false);
        }
        const bool deny = source.action == pathguard::LiteralMountAction::kDeny;
        const uint8_t mount_action = deny ? kDenyAction : kRedirectAction;
        char expanded_backing[PATH_MAX]{};
        if (!IsAllowedTarget(visible)
            || (deny ? backing[0] != '\0'
                     : !ExpandRuntimePath(
                           backing, user_id,
                           expanded_backing, sizeof(expanded_backing)))
            || strlen(visible) >= PATH_MAX) {
            plan->count = 0;
            if (diagnostic) LOGE("policy_mount_action_invalid");
            return finish(false);
        }
        PlannedMount& mount = plan->mounts[plan->count++];
        mount.action = mount_action;
        if (!StorePlanPath(plan, visible, &mount.visible_path)
            || !StorePlanPath(plan, deny ? "" : expanded_backing,
                              &mount.backing_path)) {
            plan->count = 0;
            if (diagnostic) LOGE("policy_plan_path_overflow");
            return finish(false);
        }
    }
    if (diagnostic) {
        LOGI("policy_plan_loaded uid=%d rules=%u flags=%u", uid, plan->count,
             plan->policy_flags);
    }
    return finish(plan->count > 0);
}

bool SameProcessPlan(const ProcessPlan& expected, const ProcessPlan& actual) {
    if (expected.snapshot_generation != actual.snapshot_generation
        || expected.plan_generation != actual.plan_generation
        || expected.policy_flags != actual.policy_flags
        || expected.count != actual.count
        || expected.provider_enabled != actual.provider_enabled
        || strcmp(expected.process_name, actual.process_name) != 0) {
        return false;
    }
    for (uint32_t index = 0; index < expected.count; ++index) {
        const PlannedMount& lhs = expected.mounts[index];
        const PlannedMount& rhs = actual.mounts[index];
        const char* lhs_visible = PlanPath(expected, lhs.visible_path);
        const char* lhs_backing = PlanPath(expected, lhs.backing_path);
        const char* rhs_visible = PlanPath(actual, rhs.visible_path);
        const char* rhs_backing = PlanPath(actual, rhs.backing_path);
        if (lhs.action != rhs.action || lhs_visible == nullptr || lhs_backing == nullptr
            || rhs_visible == nullptr || rhs_backing == nullptr
            || strcmp(lhs_visible, rhs_visible) != 0
            || strcmp(lhs_backing, rhs_backing) != 0) {
            return false;
        }
    }
    return true;
}

struct PathPolicyMapping {
    void* mapping = nullptr;
    size_t size = 0;
    pathguard::storage_path_adapter::PolicyScope scopes[kMaxMountRules]{};
    uint32_t scope_count = 0;
};

void ReleasePathPolicy(PathPolicyMapping* output) {
    if (output == nullptr) return;
    if (output->mapping != nullptr && output->mapping != MAP_FAILED) {
        munmap(output->mapping, output->size);
    }
    *output = {};
}

bool MapPolicy(int module_dir, PathPolicyMapping* output,
               pathguard::policy_v6_view::PolicyV6View* policy) {
    if (output == nullptr || policy == nullptr) return false;
    *output = {};
    const int policy_fd = openat(module_dir, kPolicyPath, O_RDONLY | O_CLOEXEC);
    if (policy_fd < 0) return false;
    struct stat file_stat {};
    if (fstat(policy_fd, &file_stat) != 0
        || file_stat.st_size < static_cast<off_t>(kPolicyHeaderSize)) {
        close(policy_fd);
        return false;
    }
    const size_t size = static_cast<size_t>(file_stat.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, policy_fd, 0);
    close(policy_fd);
    if (mapping == MAP_FAILED) return false;
    if (!policy->Initialize(static_cast<const uint8_t*>(mapping), size)) {
        munmap(mapping, size);
        return false;
    }
    output->mapping = mapping;
    output->size = size;
    return true;
}

bool PackageHasDomainActions(
        const pathguard::policy_v6_view::PolicyV6View& policy,
        const pathguard::policy_v6_view::PackageRef& package,
        uint8_t domain) {
    for (uint32_t index = 0; index < package.action_count; ++index) {
        pathguard::policy_v6_view::ActionRef action;
        if (policy.ActionAt(package.first_action + index, &action)
            && action.domain == domain && action.kind <= kRedirectAction) {
            return true;
        }
    }
    return false;
}

bool LoadProviderPolicy(int module_dir, PathPolicyMapping* output) {
    pathguard::policy_v6_view::PolicyV6View policy;
    if (!MapPolicy(module_dir, output, &policy)) return false;

    for (uint32_t package_index = 0;
         package_index < policy.package_count(); ++package_index) {
        pathguard::policy_v6_view::PackageRef package;
        pathguard::policy_v6_view::StringRef package_name_ref;
        char package_name[PATH_MAX]{};
        if (!policy.PackageAt(package_index, &package)
            || (package.flags & pathguard::binary_format::kPackageFlagProviderEnabled) == 0
            || (package.flags & pathguard::binary_format::kPackageFlagAllUsers) != 0
            || !PackageHasDomainActions(policy, package, kProviderDomain)
            || !policy.StringAt(package.name_id, &package_name_ref)
            || !package_name_ref.CopyTo(package_name, sizeof(package_name))) {
            continue;
        }
        for (uint32_t user_index = 0; user_index < package.user_count; ++user_index) {
            uint32_t user_id = 0;
            if (!policy.PackageUserAt(package, user_index, &user_id)) continue;
            char package_data_path[PATH_MAX]{};
            struct stat package_stat {};
            const int written = snprintf(
                package_data_path, sizeof(package_data_path), "/data/user/%u/%s",
                user_id, package_name);
            if (written <= 0 || static_cast<size_t>(written) >= sizeof(package_data_path)
                || stat(package_data_path, &package_stat) != 0
                || package_stat.st_uid < 10000) {
                LOGE("provider redirect package uid resolution failed: package=%s user=%u errno=%d",
                     package_name, user_id, errno);
                continue;
            }
            if (output->scope_count >= kMaxMountRules) return true;
            output->scopes[output->scope_count++] = {
                static_cast<int32_t>(package_stat.st_uid), user_id, package_index};
        }
    }
    if (output->scope_count == 0) ReleasePathPolicy(output);
    return output->scope_count > 0;
}

bool ReadProcessStartTime(pid_t pid, uint64_t* output);
void WriteRuntimeStatusRecord(
    int module_dir_fd, const char* process_name,
    const pathguard::RuntimeStatusRecord& status);

bool LoadAppPathPolicy(int module_dir, const char* process_name, jint uid,
                       PathPolicyMapping* output) {
    if (process_name == nullptr || uid < 10000) return false;
    pathguard::policy_v6_view::PolicyV6View policy;
    if (!MapPolicy(module_dir, output, &policy)) return false;
    const char* separator = strchr(process_name, ':');
    const size_t package_size = separator == nullptr
        ? strlen(process_name) : static_cast<size_t>(separator - process_name);
    pathguard::policy_v6_view::PackageRef package;
    const uint32_t user_id = static_cast<uint32_t>(uid) / 100000u;
    if (package_size == 0
        || !policy.FindPackage(process_name, package_size, &package)
        || !policy.PackageMatchesScope(package, process_name,
                                       strlen(process_name), user_id)
        || !PackageHasDomainActions(policy, package, kAppPathDomain)) {
        ReleasePathPolicy(output);
        return false;
    }
    output->scopes[0] = {uid, user_id, package.index};
    output->scope_count = 1;
    return true;
}

bool BuildPathRuntimeStatus(
        pid_t pid, uid_t uid,
        const PathPolicyMapping& mapping, pathguard::AdmissionDomain domain,
        const pathguard::provider_redirect::InstallResult& installed,
        pathguard::RuntimeStatusRecord* output) {
    if (pid <= 0 || mapping.mapping == nullptr || output == nullptr) return false;
    pathguard::policy_v6_view::PolicyV6View policy;
    if (!policy.Initialize(static_cast<const uint8_t*>(mapping.mapping),
                           mapping.size)) return false;
    pathguard::RuntimeStatusRecord status;
    status.pid = pid;
    status.uid = uid;
    ReadProcessStartTime(status.pid, &status.process_start_time);
    status.content_generation = policy.content_generation();
    status.snapshot_generation = installed.snapshot_generation;
    status.capability_generation = installed.capability_generation;
    status.observed_capabilities = installed.observed_capabilities;
    status.counters.hazard_slot_acquire_fail_total =
        installed.hazard_slot_acquire_fail_total;
    status.counters.hazard_slots_in_use_high_watermark =
        installed.hazard_slots_in_use_high_watermark;
    status.counters.snapshot_reload_rejected_retire_limit_total =
        installed.snapshot_reload_rejected_retire_limit_total;
    status.counters.retired_snapshot_count_high_watermark =
        installed.retired_snapshot_count_high_watermark;
    status.counters.retired_snapshot_bytes_high_watermark =
        installed.retired_snapshot_bytes_high_watermark;
    pathguard::CapabilitySnapshot capabilities;
    capabilities.capability_generation = installed.capability_generation;
    capabilities.observed_capabilities = installed.observed_capabilities;
    capabilities.domains[static_cast<uint8_t>(domain)] = {
        installed.virtualization_active
            ? pathguard::AdapterState::kActive
            : pathguard::AdapterState::kInactive,
        installed.observed_operations,
        0,
    };
    uint64_t aggregate_plan_generation = 0;
    bool have_plan_generation = false;
    bool mixed_plan_generations = false;
    for (uint32_t scope_index = 0; scope_index < mapping.scope_count; ++scope_index) {
        const uint32_t package_index = mapping.scopes[scope_index].package_index;
        bool duplicate = false;
        for (uint32_t previous = 0; previous < scope_index; ++previous) {
            duplicate = duplicate
                || mapping.scopes[previous].package_index == package_index;
        }
        if (duplicate) continue;
        pathguard::policy_v6_view::PackageRef package;
        if (!policy.PackageAt(package_index, &package)
            || !pathguard::AppendPackageRuntimeActions(
                policy, package, domain, capabilities, &status)) continue;
        if (!have_plan_generation) {
            aggregate_plan_generation = package.plan_generation;
            have_plan_generation = true;
        } else if (aggregate_plan_generation != package.plan_generation) {
            mixed_plan_generations = true;
        }
    }
    status.plan_generation = mixed_plan_generations ? 0
                                                    : aggregate_plan_generation;
    bool any_active = false;
    bool any_unsupported = false;
    for (uint32_t index = 0; index < status.action_count; ++index) {
        any_active = any_active || status.actions[index].admission.active();
        any_unsupported = any_unsupported
            || status.actions[index].admission.state
                == pathguard::ActionAdmissionState::kUnsupported;
    }
    status.enforcement = any_active ? pathguard::EnforcementState::kActive
                                    : pathguard::EnforcementState::kInactive;
    if (!any_active && status.action_total != 0) {
        status.reason = any_unsupported
            ? pathguard::RuntimeReason::kCapabilityMissing
            : pathguard::RuntimeReason::kUnsupportedAction;
    }
    *output = status;
    return true;
}

void WritePathRuntimeStatus(
        int module_dir, const char* process_name, uid_t uid,
        const PathPolicyMapping& mapping, pathguard::AdmissionDomain domain,
        const pathguard::provider_redirect::InstallResult& installed) {
    if (module_dir < 0 || process_name == nullptr) return;
    pathguard::RuntimeStatusRecord status;
    if (!BuildPathRuntimeStatus(
            getpid(), uid, mapping, domain, installed, &status)) return;
    WriteRuntimeStatusRecord(module_dir, process_name, status);
}

bool HasSafePathComponents(const char* path) {
    if (path == nullptr || path[0] == '\0' || path[0] == '/') return false;
    const char* component = path;
    while (*component != '\0') {
        const char* separator = strchr(component, '/');
        const size_t length = separator == nullptr
            ? strlen(component)
            : static_cast<size_t>(separator - component);
        if (length == 0 || (length == 1 && component[0] == '.')
            || (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (separator == nullptr) break;
        component = separator + 1;
    }
    return true;
}

bool IsAllowedTarget(const char* path) {
    return HasSafePathComponents(path)
        && strchr(path, '{') == nullptr && strchr(path, '}') == nullptr;
}

bool ExpandRuntimePath(const char* input, uint32_t user_id,
                       char* output, size_t capacity) {
    if (input == nullptr || output == nullptr || capacity == 0) return false;
    char user[16]{};
    const int user_length = snprintf(user, sizeof(user), "%u", user_id);
    if (user_length <= 0 || static_cast<size_t>(user_length) >= sizeof(user)) {
        return false;
    }
    size_t written = 0;
    const char* current = input;
    while (*current != '\0') {
        if (strncmp(current, "{user}", 6) == 0) {
            if (written + static_cast<size_t>(user_length) >= capacity) return false;
            memcpy(output + written, user, static_cast<size_t>(user_length));
            written += static_cast<size_t>(user_length);
            current += 6;
            continue;
        }
        if (*current == '{' || *current == '}' || written + 1 >= capacity) return false;
        output[written++] = *current++;
    }
    output[written] = '\0';
    return IsAllowedTarget(output);
}

bool BuildPathUnderRoot(const char* root, const char* logical_path,
                        char* output, size_t output_size) {
    if (root == nullptr || root[0] != '/' || !IsAllowedTarget(logical_path)
        || output == nullptr || output_size == 0) {
        return false;
    }
    const int written = snprintf(output, output_size, "%s/%s", root, logical_path);
    return written >= 0 && static_cast<size_t>(written) < output_size;
}

uint64_t TopologyGeneration(const RuntimeStorageTopology& topology) {
    char canonical[PATH_MAX * 2 + 512]{};
    const int written = snprintf(
        canonical, sizeof(canonical),
        "PGTOPO1|%u|%s|%s|%llu|%llu|%llu|%s|%s|%llu|%llu|%llu|%s|%s",
        topology.user_id, topology.visible_root, topology.source_root,
        static_cast<unsigned long long>(topology.visible_identity.mount_id),
        static_cast<unsigned long long>(topology.visible_identity.parent_mount_id),
        static_cast<unsigned long long>(topology.visible_identity.device),
        topology.visible_identity.root, topology.visible_identity.filesystem,
        static_cast<unsigned long long>(topology.source_identity.mount_id),
        static_cast<unsigned long long>(topology.source_identity.parent_mount_id),
        static_cast<unsigned long long>(topology.source_identity.device),
        topology.source_identity.root, topology.source_identity.filesystem);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(canonical)) return 0;
    return pathguard::binary_format::Fnv1a64(
        reinterpret_cast<const uint8_t*>(canonical), static_cast<size_t>(written));
}

bool CaptureStorageTopologyFromSnapshot(
    uid_t uid, const char* required_source_root,
    const pathguard::MountInfoSnapshot& snapshot,
    RuntimeStorageTopology* topology) {
    if (topology == nullptr) return false;
    *topology = {};
    topology->user_id = static_cast<uint32_t>(uid) / 100000u;
    if (snprintf(topology->visible_root, sizeof(topology->visible_root),
                 "/storage/emulated/%u", topology->user_id) <= 0) {
        return false;
    }
    pathguard::PinnedIdentity visible;
    if (pathguard::PinDirectory(
            snapshot, topology->visible_root, &visible) != 0) {
        return false;
    }
    topology->visible_identity = visible.mount;
    pathguard::ClosePinnedIdentity(&visible);
    char candidates[4][PATH_MAX]{};
    size_t candidate_count = 0;
    if (required_source_root != nullptr && required_source_root[0] != '\0') {
        snprintf(candidates[candidate_count++], PATH_MAX, "%s", required_source_root);
    } else {
        snprintf(candidates[candidate_count++], PATH_MAX,
                 "/mnt/user/%u/emulated/%u", topology->user_id, topology->user_id);
        snprintf(candidates[candidate_count++], PATH_MAX,
                 "/mnt/runtime/default/emulated/%u", topology->user_id);
        snprintf(candidates[candidate_count++], PATH_MAX,
                 "/mnt/pass_through/%u/emulated/%u", topology->user_id,
                 topology->user_id);
    }
    for (size_t index = 0; index < candidate_count; ++index) {
        pathguard::PinnedIdentity pinned;
        if (pathguard::PinDirectory(
                snapshot, candidates[index], &pinned) != 0) {
            continue;
        }
        const bool same_plane = pinned.device == topology->visible_identity.device
            && strcmp(pinned.mount.filesystem,
                      topology->visible_identity.filesystem) == 0;
        if (same_plane) {
            snprintf(topology->source_root, sizeof(topology->source_root), "%s",
                     candidates[index]);
            topology->source_identity = pinned.mount;
        }
        pathguard::ClosePinnedIdentity(&pinned);
        if (same_plane) break;
    }
    if (topology->source_root[0] == '\0') return false;
    topology->generation = TopologyGeneration(*topology);
    return topology->generation != 0;
}

bool SameStoragePlane(const RuntimeStorageTopology& expected,
                      const RuntimeStorageTopology& actual) {
    return expected.generation != 0
        && actual.generation != 0
        && expected.user_id == actual.user_id
        && strcmp(expected.visible_root, actual.visible_root) == 0
        && strcmp(expected.source_root, actual.source_root) == 0
        && expected.visible_identity.device == actual.visible_identity.device
        && expected.source_identity.device == actual.source_identity.device
        && strcmp(expected.visible_identity.root, actual.visible_identity.root) == 0
        && strcmp(expected.source_identity.root, actual.source_identity.root) == 0
        && strcmp(expected.visible_identity.filesystem,
                  actual.visible_identity.filesystem) == 0
        && strcmp(expected.source_identity.filesystem,
                  actual.source_identity.filesystem) == 0;
}

bool SameStorageTopology(const RuntimeStorageTopology& expected,
                         const RuntimeStorageTopology& actual) {
    return expected.generation == actual.generation
        && SameStoragePlane(expected, actual);
}

bool ReadProcessUid(pid_t pid, uid_t expected_uid) {
    char status_path[64]{};
    snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
    FILE* status = fopen(status_path, "re");
    if (status == nullptr) return false;
    char line[256]{};
    bool matched = false;
    while (fgets(line, sizeof(line), status) != nullptr) {
        unsigned real_uid = 0;
        if (sscanf(line, "Uid:\t%u", &real_uid) == 1) {
            matched = real_uid == expected_uid;
            break;
        }
    }
    fclose(status);
    return matched;
}

bool ReadProcessStartTime(pid_t pid, uint64_t* start_time) {
    if (start_time == nullptr) return false;
    char stat_path[64]{};
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
    FILE* input = fopen(stat_path, "re");
    if (input == nullptr) return false;
    char line[4096]{};
    const bool read = fgets(line, sizeof(line), input) != nullptr;
    fclose(input);
    if (!read) return false;
    char* current = strrchr(line, ')');
    if (current == nullptr || current[1] != ' ') return false;
    current += 2;
    for (int field = 3; field < 22; ++field) {
        current = strchr(current, ' ');
        if (current == nullptr) return false;
        while (*current == ' ') ++current;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = strtoull(current, &end, 10);
    if (errno != 0 || end == current) return false;
    *start_time = static_cast<uint64_t>(value);
    return true;
}

bool ReadProcessContext(pid_t pid, char* context, size_t context_size) {
    if (context == nullptr || context_size < 2) return false;
    char attr_path[64]{};
    snprintf(attr_path, sizeof(attr_path), "/proc/%d/attr/current", pid);
    FILE* attr = fopen(attr_path, "re");
    if (attr == nullptr) return false;
    const char* result = fgets(context, static_cast<int>(context_size), attr);
    fclose(attr);
    if (result == nullptr) return false;
    context[strcspn(context, "\r\n")] = '\0';
    return context[0] != '\0';
}

using MountState = pathguard::MountTransactionState;

const char* EnforcementName(pathguard::EnforcementState value) {
    switch (value) {
        case pathguard::EnforcementState::kInactive: return "inactive";
        case pathguard::EnforcementState::kActive: return "active";
        case pathguard::EnforcementState::kPendingRestart: return "pending_restart";
        case pathguard::EnforcementState::kFailed: return "failed";
    }
    return "failed";
}

const char* TransactionName(pathguard::TransactionOutcome value) {
    switch (value) {
        case pathguard::TransactionOutcome::kNone: return "none";
        case pathguard::TransactionOutcome::kComplete: return "complete";
        case pathguard::TransactionOutcome::kFailedPreflight: return "failed_preflight";
        case pathguard::TransactionOutcome::kRollbackComplete: return "rollback_complete";
        case pathguard::TransactionOutcome::kNamespaceTainted: return "namespace_tainted";
    }
    return "none";
}

const char* SecurityName(pathguard::SecurityLevel value) {
    switch (value) {
        case pathguard::SecurityLevel::kNone: return "none";
        case pathguard::SecurityLevel::kFdPinned: return "fd_pinned";
        case pathguard::SecurityLevel::kLegacyToctou: return "legacy_toctou";
    }
    return "none";
}

const char* ReasonName(pathguard::RuntimeReason value) {
    switch (value) {
        case pathguard::RuntimeReason::kNone: return "none";
        case pathguard::RuntimeReason::kCapabilityMissing: return "capability_missing";
        case pathguard::RuntimeReason::kLegacyNotAuthorized: return "legacy_not_authorized";
        case pathguard::RuntimeReason::kUnsupportedAction: return "unsupported_action";
        case pathguard::RuntimeReason::kTopologyChanged: return "topology_changed";
        case pathguard::RuntimeReason::kPolicyChanged: return "policy_changed";
        case pathguard::RuntimeReason::kPreflightFailed: return "preflight_failed";
        case pathguard::RuntimeReason::kApplyFailed: return "apply_failed";
        case pathguard::RuntimeReason::kRollbackFailed: return "rollback_failed";
        case pathguard::RuntimeReason::kOwnerDeath: return "owner_death";
    }
    return "preflight_failed";
}

const char* RuntimeActionKindName(pathguard::RuntimeActionKind value) {
    switch (value) {
        case pathguard::RuntimeActionKind::kUnknown: return "unknown";
        case pathguard::RuntimeActionKind::kDeny: return "deny";
        case pathguard::RuntimeActionKind::kRedirect: return "redirect";
        case pathguard::RuntimeActionKind::kObserve: return "observe";
        case pathguard::RuntimeActionKind::kExport: return "export";
    }
    return "unknown";
}

const char* AdmissionDomainName(pathguard::AdmissionDomain value) {
    switch (value) {
        case pathguard::AdmissionDomain::kMount: return "mount";
        case pathguard::AdmissionDomain::kAppPath: return "app_path";
        case pathguard::AdmissionDomain::kProvider: return "provider";
        case pathguard::AdmissionDomain::kCompleteVfs: return "complete_vfs";
        case pathguard::AdmissionDomain::kEvent: return "event";
    }
    return "unknown";
}

const char* AdmissionStateName(pathguard::ActionAdmissionState value) {
    switch (value) {
        case pathguard::ActionAdmissionState::kInactive: return "inactive";
        case pathguard::ActionAdmissionState::kActive: return "active";
        case pathguard::ActionAdmissionState::kUnsupported: return "unsupported";
    }
    return "inactive";
}

const char* AdmissionReasonName(pathguard::ActionAdmissionReason value) {
    switch (value) {
        case pathguard::ActionAdmissionReason::kNone: return "none";
        case pathguard::ActionAdmissionReason::kIntentDisabled: return "intent_disabled";
        case pathguard::ActionAdmissionReason::kGenerationStale: return "generation_stale";
        case pathguard::ActionAdmissionReason::kAdapterInactive: return "adapter_inactive";
        case pathguard::ActionAdmissionReason::kAdapterUnsupported: return "adapter_unsupported";
        case pathguard::ActionAdmissionReason::kCapabilityMissing: return "capability_missing";
        case pathguard::ActionAdmissionReason::kOperationMissing: return "operation_missing";
    }
    return "adapter_inactive";
}

bool FormatRuntimeStatusRecord(
        const char* process_name, const pathguard::RuntimeStatusRecord& status,
        char* text, size_t capacity, size_t* output_length) {
    if (status.pid <= 0 || process_name == nullptr || text == nullptr
        || capacity == 0 || output_length == nullptr) return false;
    int length = snprintf(
        text, capacity,
        "schema=pathguard.runtime_status.v2\nversion=%u\npid=%d\nuid=%u\n"
        "process_start_time=%llu\nprocess=%s\n"
        "enforcement=%s\nbackend=%u\ntransaction=%s\nsecurity=%s\nreason=%s\n"
        "error=%d\ncontent_generation=%llu\nsnapshot_generation=%llu\n"
        "plan_generation=%llu\ncapability_generation=%llu\n"
        "topology_generation=%llu\nobserved_capabilities=%llu\n"
        "action_count=%u\naction_total=%u\nactions_truncated=%s\n"
        "hazard_slot_acquire_fail_total=%llu\n"
        "hazard_slots_in_use_high_watermark=%u\n"
        "snapshot_reload_rejected_retire_limit_total=%llu\n"
        "retired_snapshot_count_high_watermark=%u\n"
        "retired_snapshot_bytes_high_watermark=%llu\n"
        "provider_bridge_kind=%u\nprovider_bridge_deployment_profile=%llu\n"
        "provider_bridge_profile=%llu\nprovider_bridge_build_matched=%s\n"
        "provider_bridge_library_loaded=%s\nprovider_bridge_lsplant_initialized=%s\n"
        "provider_bridge_hooker_dex_loaded=%s\nprovider_bridge_resolved_methods=%llu\n"
        "provider_bridge_installed_hooks=%llu\nprovider_bridge_backup_methods=%llu\n"
        "provider_bridge_self_tested_hooks=%llu\nprovider_bridge_errno=%d\n"
        "event_overflow_total=%llu\ndiagnostic_drop_total=%llu\n",
        status.version, status.pid, status.uid,
        static_cast<unsigned long long>(status.process_start_time), process_name,
        EnforcementName(status.enforcement), status.backend,
        TransactionName(status.transaction), SecurityName(status.security),
        ReasonName(status.reason), status.error,
        static_cast<unsigned long long>(status.content_generation),
        static_cast<unsigned long long>(status.snapshot_generation),
        static_cast<unsigned long long>(status.plan_generation),
        static_cast<unsigned long long>(status.capability_generation),
        static_cast<unsigned long long>(status.topology_generation),
        static_cast<unsigned long long>(status.observed_capabilities),
        status.action_count, status.action_total,
        status.actions_truncated ? "true" : "false",
        static_cast<unsigned long long>(
            status.counters.hazard_slot_acquire_fail_total),
        status.counters.hazard_slots_in_use_high_watermark,
        static_cast<unsigned long long>(
            status.counters.snapshot_reload_rejected_retire_limit_total),
        status.counters.retired_snapshot_count_high_watermark,
        static_cast<unsigned long long>(
            status.counters.retired_snapshot_bytes_high_watermark),
        static_cast<unsigned>(status.provider_bridge.kind),
        static_cast<unsigned long long>(status.provider_bridge.deployment_profile_id),
        static_cast<unsigned long long>(status.provider_bridge.provider_profile_id),
        status.provider_bridge.build_matched ? "true" : "false",
        status.provider_bridge.library_loaded ? "true" : "false",
        status.provider_bridge.lsplant_initialized ? "true" : "false",
        status.provider_bridge.hooker_dex_loaded ? "true" : "false",
        static_cast<unsigned long long>(status.provider_bridge.resolved_methods),
        static_cast<unsigned long long>(status.provider_bridge.installed_hooks),
        static_cast<unsigned long long>(status.provider_bridge.backup_methods),
        static_cast<unsigned long long>(status.provider_bridge.self_tested_hooks),
        status.provider_bridge.bridge_errno,
        static_cast<unsigned long long>(status.counters.event_overflow_total),
        static_cast<unsigned long long>(status.counters.diagnostic_drop_total));
    bool formatted = length > 0 && static_cast<size_t>(length) < capacity;
    size_t offset = formatted ? static_cast<size_t>(length) : 0;
    for (uint32_t index = 0; formatted && index < status.action_count; ++index) {
        const pathguard::RuntimeActionStatus& action = status.actions[index];
        const pathguard::ActionAdmission& admission = action.admission;
        length = snprintf(
            text + offset, capacity - offset,
            "action.%u.kind=%s\naction.%u.domain=%s\n"
            "action.%u.intent=%s\naction.%u.action_mask=%llu\n"
            "action.%u.rule_id=%llu\naction.%u.selector_id=%u\n"
            "action.%u.conflict_id=%llu\naction.%u.admission=%s\n"
            "action.%u.admission_reason=%s\n"
            "action.%u.required_capabilities=%llu\n"
            "action.%u.observed_capabilities=%llu\n"
            "action.%u.missing_capabilities=%llu\n"
            "action.%u.required_operations=%llu\n"
            "action.%u.observed_operations=%llu\n"
            "action.%u.missing_operations=%llu\n"
            "action.%u.capability_generation=%llu\n"
            "action.%u.plan_generation=%llu\naction.%u.probe_error=%d\n",
            index, RuntimeActionKindName(action.kind),
            index, AdmissionDomainName(action.domain),
            index, action.intent_enabled ? "enabled" : "disabled",
            index, static_cast<unsigned long long>(action.action_mask),
            index, static_cast<unsigned long long>(action.rule_id),
            index, action.selector_id,
            index, static_cast<unsigned long long>(action.conflict_id),
            index, AdmissionStateName(admission.state),
            index, AdmissionReasonName(admission.reason),
            index, static_cast<unsigned long long>(admission.required_capabilities),
            index, static_cast<unsigned long long>(admission.observed_capabilities),
            index, static_cast<unsigned long long>(admission.missing_capabilities),
            index, static_cast<unsigned long long>(admission.required_operations),
            index, static_cast<unsigned long long>(admission.observed_operations),
            index, static_cast<unsigned long long>(admission.missing_operations),
            index, static_cast<unsigned long long>(admission.capability_generation),
            index, static_cast<unsigned long long>(admission.plan_generation),
            index, admission.probe_error);
        formatted = length > 0 && static_cast<size_t>(length) < capacity - offset;
        if (formatted) offset += static_cast<size_t>(length);
    }
    if (formatted) *output_length = offset;
    return formatted;
}

bool WriteRuntimeStatusText(int module_dir_fd, pid_t pid,
                            const char* text, size_t length) {
    if (module_dir_fd < 0 || pid <= 0 || text == nullptr || length == 0
        || length > kMaxRuntimeStatusBytes) return false;
    if (mkdirat(module_dir_fd, "run/status", 0755) != 0 && errno != EEXIST) {
        return false;
    }
    char temporary[96]{};
    char final_path[64]{};
    snprintf(temporary, sizeof(temporary), "run/status/.%d.%d.tmp",
             pid, getpid());
    snprintf(final_path, sizeof(final_path), "run/status/%d.status", pid);
    const int output = openat(module_dir_fd, temporary,
                              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (output < 0) return false;
    const bool written = WriteRegularFileFully(output, text, length);
    close(output);
    const bool installed = written && renameat(
        module_dir_fd, temporary, module_dir_fd, final_path) == 0;
    if (!installed) {
        unlinkat(module_dir_fd, temporary, 0);
        return false;
    }

    const int status_dir = openat(module_dir_fd, "run/status",
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC
                                      | O_NOFOLLOW);
    if (status_dir < 0) return true;
    DIR* directory = fdopendir(status_dir);
    if (directory == nullptr) {
        close(status_dir);
        return true;
    }
    while (dirent* entry = readdir(directory)) {
        if (entry->d_name[0] < '1' || entry->d_name[0] > '9') continue;
        errno = 0;
        char* suffix = nullptr;
        const long candidate = strtol(entry->d_name, &suffix, 10);
        if (errno != 0 || candidate <= 0 || candidate > INT_MAX
            || suffix == entry->d_name || strcmp(suffix, ".status") != 0
            || candidate == pid) {
            continue;
        }
        uint64_t process_start_time = 0;
        if (!ReadProcessStartTime(
                static_cast<pid_t>(candidate), &process_start_time)) {
            unlinkat(dirfd(directory), entry->d_name, 0);
        }
    }
    closedir(directory);
    return true;
}

void WriteRuntimeStatusRecord(int module_dir_fd, const char* process_name,
                              const pathguard::RuntimeStatusRecord& status) {
    char text[kMaxRuntimeStatusBytes]{};
    size_t length = 0;
    if (FormatRuntimeStatusRecord(
            process_name, status, text, sizeof(text), &length)) {
        WriteRuntimeStatusText(module_dir_fd, status.pid, text, length);
    }
}

bool PublishRuntimeStatusRecord(SharedRuntimeStatus* shared,
                                const char* process_name,
                                const pathguard::RuntimeStatusRecord& status) {
    if (shared == nullptr || process_name == nullptr
        || shared->magic != kSharedStatusMagic
        || shared->version != kSharedStatusVersion) return false;
    const size_t process_name_length = strlen(process_name);
    if (process_name_length == 0 || process_name_length >= kMaxProcessNameBytes) {
        return false;
    }
    size_t status_length = 0;
    if (!FormatRuntimeStatusRecord(
            process_name, status, shared->text, sizeof(shared->text),
            &status_length)) {
        return false;
    }
    memcpy(shared->process_name, process_name, process_name_length);
    shared->process_name[process_name_length] = '\0';
    shared->process_name_length = static_cast<uint32_t>(process_name_length);
    shared->status_length = static_cast<uint32_t>(status_length);
    __atomic_store_n(&shared->state, 1u, __ATOMIC_RELEASE);
    syscall(SYS_futex, &shared->state, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
    return true;
}

void CancelRuntimeStatus(SharedRuntimeStatus* shared) {
    if (shared == nullptr) return;
    __atomic_store_n(&shared->state, 2u, __ATOMIC_RELEASE);
    syscall(SYS_futex, &shared->state, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

void WriteRuntimeStatus(int module_dir_fd, pid_t pid, uid_t uid,
                        const ProcessPlan& plan, const MountPerfResult& mount,
                        MountState state) {
    if (module_dir_fd < 0 || pid <= 0) return;
    pathguard::RuntimeStatusRecord status;
    bool app_path_policy_loaded = false;
    bool app_path_status_built = false;
    if (plan.app_path_status_available) {
        PathPolicyMapping mapping;
        app_path_policy_loaded = LoadAppPathPolicy(
                module_dir_fd, plan.process_name, static_cast<jint>(uid),
                &mapping);
        if (app_path_policy_loaded) {
            app_path_status_built = BuildPathRuntimeStatus(
                pid, uid, mapping, pathguard::AdmissionDomain::kAppPath,
                plan.app_path_install, &status);
            ReleasePathPolicy(&mapping);
        }
    }
    LOGI("runtime status merge: pid=%d app_path_available=%d policy_loaded=%d built=%d actions=%u/%u",
         pid, plan.app_path_status_available ? 1 : 0,
         app_path_policy_loaded ? 1 : 0, app_path_status_built ? 1 : 0,
         status.action_count, status.action_total);
    status.pid = pid;
    status.uid = uid;
    ReadProcessStartTime(pid, &status.process_start_time);
    status.snapshot_generation = plan.snapshot_generation;
    status.content_generation = plan.snapshot_generation;
    status.plan_generation = plan.plan_generation;
    status.topology_generation = plan.topology.generation;
    status.backend = static_cast<uint8_t>(mount.backend);
    if (state == MountState::kComplete) {
        status.enforcement = pathguard::EnforcementState::kActive;
        status.transaction = pathguard::TransactionOutcome::kComplete;
    } else {
        status.enforcement = pathguard::EnforcementState::kFailed;
        status.transaction = state == MountState::kNamespaceTainted
            ? pathguard::TransactionOutcome::kNamespaceTainted
            : state == MountState::kRollbackComplete
                ? pathguard::TransactionOutcome::kRollbackComplete
                : pathguard::TransactionOutcome::kFailedPreflight;
    }
    status.security = mount.backend
            == static_cast<uint32_t>(pathguard::MountBackendKind::kLegacyString)
        ? pathguard::SecurityLevel::kLegacyToctou
        : mount.backend == static_cast<uint32_t>(
              pathguard::MountBackendKind::kStrictOpenTree)
              || mount.backend == static_cast<uint32_t>(
                  pathguard::MountBackendKind::kStrictProcFd)
            ? pathguard::SecurityLevel::kFdPinned
            : pathguard::SecurityLevel::kNone;
    if (mount.runtime_reason != 0) {
        status.reason = static_cast<pathguard::RuntimeReason>(mount.runtime_reason);
    } else if (mount.result == EOWNERDEAD) {
        status.reason = pathguard::RuntimeReason::kOwnerDeath;
    } else if (mount.rollback_failed != 0
               || state == MountState::kNamespaceTainted) {
        status.reason = pathguard::RuntimeReason::kRollbackFailed;
    } else if (mount.backend_reason == static_cast<uint32_t>(
                   pathguard::MountBackendReason::kLegacyNotAuthorized)) {
        status.reason = pathguard::RuntimeReason::kLegacyNotAuthorized;
    } else if (mount.backend_reason == static_cast<uint32_t>(
                   pathguard::MountBackendReason::kUnsupportedAction)) {
        status.reason = pathguard::RuntimeReason::kUnsupportedAction;
    } else if (mount.backend_reason == static_cast<uint32_t>(
                   pathguard::MountBackendReason::kCapabilityMissing)) {
        status.reason = pathguard::RuntimeReason::kCapabilityMissing;
    } else if (mount.result == ESTALE) {
        status.reason = pathguard::RuntimeReason::kPolicyChanged;
    } else if (mount.failure_stage != 0) {
        status.reason = pathguard::RuntimeReason::kApplyFailed;
    } else if (mount.result != 0) {
        status.reason = pathguard::RuntimeReason::kPreflightFailed;
    }
    status.error = mount.result;
    WriteRuntimeStatusRecord(module_dir_fd, plan.process_name, status);
}

uint32_t StateValue(MountState state) {
    return static_cast<uint32_t>(state);
}

MountState LoadSharedStatus(const SharedMountState* state) {
    return static_cast<MountState>(
        __atomic_load_n(&state->status, __ATOMIC_ACQUIRE));
}

bool IsCancelRequested(const SharedMountState* state) {
    return state != nullptr
        && LoadSharedStatus(state) == MountState::kCancelRequested;
}

void WakeSharedState(SharedMountState* state) {
    syscall(SYS_futex, &state->status, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

bool TransitionSharedState(SharedMountState* state, MountState from, MountState to) {
    if (state == nullptr || !pathguard::IsMountTransitionAllowed(from, to)) return false;
    uint32_t expected = StateValue(from);
    const bool transitioned = __atomic_compare_exchange_n(
        &state->status, &expected, StateValue(to), false,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (transitioned) WakeSharedState(state);
    return transitioned;
}

bool PublishSharedResult(SharedMountState* state, const CompanionResult& result,
                         MountState from, MountState terminal) {
    state->result = result;
    return TransitionSharedState(state, from, terminal);
}

bool MarkSharedCancelled(SharedMountState* state) {
    return TransitionSharedState(
        state, MountState::kCancelRequested, MountState::kCancelled);
}

bool PublishPreflightFailure(SharedMountState* state,
                             const CompanionResult& result) {
    if (PublishSharedResult(
            state, result, MountState::kPreflighting,
            MountState::kFailed)) {
        return true;
    }
    if (PublishSharedResult(
            state, result, MountState::kPending, MountState::kFailed)) {
        return true;
    }
    if (LoadSharedStatus(state) == MountState::kCancelRequested) {
        MarkSharedCancelled(state);
    }
    return false;
}

bool BeginPreflight(SharedMountState* state) {
    if (TransitionSharedState(
            state, MountState::kPending, MountState::kPreflighting)) {
        return true;
    }
    if (LoadSharedStatus(state) == MountState::kCancelRequested) {
        MarkSharedCancelled(state);
    }
    return false;
}

bool AcquireMutationLease(SharedMountState* state) {
    if (TransitionSharedState(
            state, MountState::kPreflighting, MountState::kApplying)) {
        return true;
    }
    if (LoadSharedStatus(state) == MountState::kCancelRequested) {
        MarkSharedCancelled(state);
    }
    return false;
}

enum class ProcessReadyResult {
    kReady,
    kTimedOut,
    kCancelled,
};

ProcessReadyResult WaitForProcessReady(pid_t pid, uid_t expected_uid,
                                       SharedMountState* state) {
    int elapsed = 0;
    int delay_ms = 1;
    while (elapsed < kProcessReadyTimeoutMs) {
        if (IsCancelRequested(state)) return ProcessReadyResult::kCancelled;
        char context[256]{};
        if (ReadProcessUid(pid, expected_uid)
            && ReadProcessContext(pid, context, sizeof(context))
            && strstr(context, "zygote") == nullptr) {
            return ProcessReadyResult::kReady;
        }
        usleep(static_cast<useconds_t>(delay_ms) * 1000u);
        elapsed += delay_ms;
        if (delay_ms < 10) delay_ms = delay_ms >= 5 ? 10 : delay_ms * 2;
    }
    return ProcessReadyResult::kTimedOut;
}

pathguard::MountBackendProbe ProbeMountBackendsIsolated(
        const char* source_path, const char* target_path) {
    pathguard::MountBackendProbe probe;
    int sockets[2]{};
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
        probe.error = errno;
        return probe;
    }
    const pid_t child = fork();
    if (child == 0) {
        close(sockets[0]);
        if (unshare(CLONE_NEWNS) != 0
            || mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
            probe.error = errno;
        } else {
            probe = pathguard::ProbeDirectoryMountBackends(
                source_path, target_path);
        }
        send(sockets[1], &probe, sizeof(probe), MSG_NOSIGNAL);
        close(sockets[1]);
        _exit(probe.error == 0 ? 0 : 1);
    }
    close(sockets[1]);
    if (child < 0 || !ReadFully(sockets[0], &probe, sizeof(probe))) {
        probe = {};
        probe.error = child < 0 ? errno : EIO;
    }
    close(sockets[0]);
    if (child > 0) {
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
    }
    return probe;
}

bool StoragePropagationRequiresPrivate(
    const pathguard::MountInfoSnapshot& snapshot) {
    bool requires_private = true;
    if (pathguard::MountInfoSnapshotRequiresPrivate(
            snapshot, "/storage", &requires_private) != 0) {
        return true;
    }
    return requires_private;
}

struct NamespaceTerminationResult {
    size_t matched = 0;
    size_t signaled = 0;
    size_t remaining = 0;
};

size_t ScanNamespaceMembers(dev_t namespace_device, ino_t namespace_inode,
                            bool terminate, size_t* signaled) {
    DIR* proc = opendir("/proc");
    if (proc == nullptr) return 0;
    size_t matched = 0;
    while (dirent* entry = readdir(proc)) {
        char* end = nullptr;
        const long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || value <= 0
            || value == getpid()) {
            continue;
        }
        char path[64]{};
        snprintf(path, sizeof(path), "/proc/%ld/ns/mnt", value);
        struct stat identity {};
        if (stat(path, &identity) == 0
            && identity.st_dev == namespace_device
            && identity.st_ino == namespace_inode) {
            ++matched;
            if (terminate && kill(static_cast<pid_t>(value), SIGKILL) == 0
                && signaled != nullptr) {
                ++*signaled;
            }
        }
    }
    closedir(proc);
    return matched;
}

NamespaceTerminationResult TerminateNamespaceMembers(dev_t namespace_device,
                                                       ino_t namespace_inode) {
    NamespaceTerminationResult result;
    result.matched = ScanNamespaceMembers(
        namespace_device, namespace_inode, true, &result.signaled);
    for (int attempt = 0; attempt < 20; ++attempt) {
        result.remaining = ScanNamespaceMembers(
            namespace_device, namespace_inode, false, nullptr);
        if (result.remaining == 0) break;
        usleep(50000);
        ScanNamespaceMembers(namespace_device, namespace_inode, true,
                             &result.signaled);
    }
    return result;
}

bool IsDisposableAppNamespace(pid_t target_pid, uid_t target_uid,
                              dev_t namespace_device,
                              ino_t namespace_inode) {
    DIR* proc = opendir("/proc");
    if (proc == nullptr) return false;
    bool target_found = false;
    bool valid = true;
    while (valid) {
        dirent* entry = readdir(proc);
        if (entry == nullptr) break;
        char* end = nullptr;
        const long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || value <= 0
            || value == getpid()) {
            continue;
        }
        char path[64]{};
        snprintf(path, sizeof(path), "/proc/%ld/ns/mnt", value);
        struct stat identity {};
        if (stat(path, &identity) != 0
            || identity.st_dev != namespace_device
            || identity.st_ino != namespace_inode) {
            continue;
        }
        const pid_t member = static_cast<pid_t>(value);
        if (member != target_pid || !ReadProcessUid(member, target_uid)) {
            valid = false;
            break;
        }
        target_found = true;
    }
    closedir(proc);
    return valid && target_found;
}

pathguard::MountRollbackResult UnmountTransactionMount(
    const pathguard::AppliedMount& applied) {
#if PATHGUARD_TEST_ROLLBACK_FAILURE
    pathguard::MountRollbackResult result;
    result.failure.backend = applied.backend;
    result.failure.operation_id = applied.operation_id;
    result.failure.stage = pathguard::MountOperationStage::kRollbackUnmount;
    result.failure.error = EIO;
    result.failure.expected_mount_id = applied.mount_id;
    result.failure.actual_mount_id = applied.mount_id;
    result.failure.mutation_happened = 1;
    result.failure.identity_confirmed = 1;
    return result;
#else
    return pathguard::UnmountValidatedDirectoryMount(applied);
#endif
}

MountPerfResult ApplyProcessPlan(pid_t pid, uid_t uid, int module_dir_fd,
                                 const ProcessPlan& plan,
                                 SharedMountState* state) {
    MountPerfResult perf;
    perf.rule_count = plan.count;
    perf.topology_generation = plan.topology.generation;
    if (IsCancelRequested(state)) {
        perf.result = ECANCELED;
        return perf;
    }
    uint64_t expected_start_time = 0;
    if (!ReadProcessUid(pid, uid)
        || !ReadProcessStartTime(pid, &expected_start_time)) {
        perf.result = ESRCH;
        return perf;
    }
    const uint64_t setns_started = pathguard::perf::NowNs();
    char namespace_path[64];
    snprintf(namespace_path, sizeof(namespace_path), "/proc/%d/ns/mnt", pid);
    const int namespace_fd = open(namespace_path, O_RDONLY | O_CLOEXEC);
    if (namespace_fd < 0) {
        const int error = errno;
        LOGE("mount namespace open failed: pid=%d errno=%d", pid, error);
        perf.result = error;
        perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);
        return perf;
    }
    if (setns(namespace_fd, CLONE_NEWNS) != 0) {
        const int error = errno;
        LOGE("mount namespace enter failed: pid=%d errno=%d", pid, error);
        close(namespace_fd);
        perf.result = error;
        perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);
        return perf;
    }
    struct stat namespace_identity {};
    if (fstat(namespace_fd, &namespace_identity) != 0) {
        const int error = errno;
        close(namespace_fd);
        perf.result = error;
        perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);
        return perf;
    }
    close(namespace_fd);
    perf.setns_ns = pathguard::perf::ElapsedNs(setns_started);

    char source_path[PATH_MAX]{};
    char target_path[PATH_MAX]{};
    char deny_anchor_path[PATH_MAX]{};
    const pathguard::MountActionMask required_actions =
        RequiredMountActions(plan);
    if (required_actions == 0
        || ((required_actions & pathguard::kMountActionDenyAnchor) != 0
            && !BuildDenyAnchorPath(module_dir_fd, deny_anchor_path,
                                    sizeof(deny_anchor_path)))) {
        perf.result = ENOTSUP;
        perf.runtime_reason = static_cast<uint32_t>(
            pathguard::RuntimeReason::kUnsupportedAction);
        return perf;
    }
    const bool allow_legacy = (plan.policy_flags
        & pathguard::binary_format::kPolicyFlagAllowLegacyStringBind) != 0;
    pathguard::MountBackendKind backend =
        pathguard::MountBackendKind::kStrictOpenTree;
    bool backend_locked = false;
    perf.backend = static_cast<uint32_t>(backend);
    perf.backend_reason = static_cast<uint32_t>(
        pathguard::MountBackendReason::kNone);
    MountTransactionWorkspace* workspace = CreateMountTransactionWorkspace();
    if (workspace == nullptr) {
        perf.result = errno != 0 ? errno : ENOMEM;
        return perf;
    }
    pathguard::MountInfoSnapshot current_mounts;
    const int snapshot_error = pathguard::CaptureMountInfoSnapshot(
        &current_mounts);
    if (snapshot_error != 0
        || !pathguard::MountInfoSnapshotMatchesCurrentNamespace(
            current_mounts)) {
        pathguard::DestroyMountInfoSnapshot(&current_mounts);
        DestroyMountTransactionWorkspace(workspace);
        if (LoadSharedStatus(state) == MountState::kCancelRequested) {
            MarkSharedCancelled(state);
        }
        perf.result = snapshot_error != 0 ? snapshot_error : ESTALE;
        return perf;
    }
    perf.mountinfo_snapshot_count = 1;
    perf.mountinfo_read_ns = current_mounts.read_ns;
    perf.mountinfo_parse_ns = current_mounts.parse_ns;
    size_t pinned_source_count = 0;
    size_t pinned_target_count = 0;
    const auto finish_preflight = [&](int error) {
        for (size_t index = 0; index < pinned_source_count; ++index) {
            pathguard::ClosePinnedIdentity(&workspace->sources[index]);
        }
        for (size_t index = 0; index < pinned_target_count; ++index) {
            pathguard::ClosePinnedIdentity(&workspace->targets[index]);
        }
        pathguard::DestroyMountInfoSnapshot(&current_mounts);
        DestroyMountTransactionWorkspace(workspace);
        if (error == ECANCELED
            && LoadSharedStatus(state) == MountState::kCancelRequested) {
            MarkSharedCancelled(state);
        }
        perf.result = error;
        return perf;
    };
    RuntimeStorageTopology transaction_topology;
    const uint64_t initial_topology_started = pathguard::perf::NowNs();
    const bool initial_topology_valid = CaptureStorageTopologyFromSnapshot(
        uid, plan.topology.source_root, current_mounts, &transaction_topology)
        && SameStoragePlane(plan.topology, transaction_topology);
    perf.topology_ns = pathguard::perf::ElapsedNs(initial_topology_started);
    if (!initial_topology_valid) {
        perf.runtime_reason = static_cast<uint32_t>(
            pathguard::RuntimeReason::kTopologyChanged);
        return finish_preflight(ESTALE);
    }
    const uint64_t src_pin_started = pathguard::perf::NowNs();
    for (size_t rule_index = 0; rule_index < plan.count; ++rule_index) {
        const char* visible_path = PlanPath(
            plan, plan.mounts[rule_index].visible_path);
        const char* backing_path = PlanPath(
            plan, plan.mounts[rule_index].backing_path);
        if (!IsExecutableMountAction(plan.mounts[rule_index].action)
            || visible_path == nullptr || backing_path == nullptr
            || !BuildPathUnderRoot(plan.topology.visible_root, visible_path,
                                   target_path, sizeof(target_path))
            || !BuildMountSourcePath(
                plan, plan.mounts[rule_index], deny_anchor_path,
                source_path, sizeof(source_path))) {
            return finish_preflight(EINVAL);
        }

        size_t source_index = 0;
        while (source_index < pinned_source_count
               && strcmp(workspace->source_locators[source_index].path,
                         source_path) != 0) {
            ++source_index;
        }
        if (source_index == pinned_source_count) {
            const int source_pin_error = pathguard::PinDirectory(
                current_mounts, source_path,
                &workspace->sources[source_index]);
            if (source_pin_error != 0) {
                return finish_preflight(source_pin_error);
            }
            snprintf(workspace->source_locators[source_index].path,
                     sizeof(workspace->source_locators[source_index].path),
                     "%s", source_path);
            ++pinned_source_count;
        }
        workspace->source_indexes[rule_index] =
            static_cast<uint8_t>(source_index);

        const uint64_t target_pin_started = pathguard::perf::NowNs();
        const int target_pin_error = pathguard::PinDirectory(
            current_mounts, target_path, &workspace->targets[rule_index]);
        workspace->target_pin_ns[rule_index] =
            pathguard::perf::ElapsedNs(target_pin_started);
        if (target_pin_error != 0) {
            return finish_preflight(target_pin_error);
        }
        ++pinned_target_count;
        snprintf(workspace->target_locators[rule_index].path,
                 sizeof(workspace->target_locators[rule_index].path),
                 "%s", target_path);
    }
    perf.source_pin_ns = pathguard::perf::ElapsedNs(src_pin_started);
    LOGI("perf_stage mount_pin_loop_us=%llu sources=%zu targets=%zu",
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             perf.source_pin_ns)),
         pinned_source_count, pinned_target_count);
#if PATHGUARD_TEST_PRE_LEASE_DELAY_MS > 0
    LOGI("transaction test pre-lease delay: pid=%d delay_ms=%d",
         pid, PATHGUARD_TEST_PRE_LEASE_DELAY_MS);
    usleep(static_cast<useconds_t>(PATHGUARD_TEST_PRE_LEASE_DELAY_MS) * 1000u);
#endif
    const uint64_t propagation_check_started = pathguard::perf::NowNs();
    const bool propagation_requires_private = StoragePropagationRequiresPrivate(
        current_mounts);
    perf.propagation_check_ns = pathguard::perf::ElapsedNs(
        propagation_check_started);
    ProcessPlan lease_plan;
    RuntimeStorageTopology lease_topology;
    uint64_t lease_start_time = 0;
    const bool lease_process_valid = ReadProcessUid(pid, uid)
        && ReadProcessStartTime(pid, &lease_start_time)
        && lease_start_time == expected_start_time;
    bool lease_policy_valid = false;
    if (lease_process_valid) {
        const uint64_t lease_policy_started = pathguard::perf::NowNs();
        lease_policy_valid = LoadProcessPlan(
            module_dir_fd, plan.process_name, static_cast<jint>(uid),
            &lease_plan, nullptr) && SameProcessPlan(plan, lease_plan);
        perf.policy_revalidate_ns = pathguard::perf::ElapsedNs(
            lease_policy_started);
    }
    bool lease_topology_valid = false;
    if (lease_policy_valid) {
        const uint64_t topology_started = pathguard::perf::NowNs();
        pathguard::MountInfoSnapshot lease_mounts;
        const int lease_snapshot_error = pathguard::CaptureMountInfoSnapshot(
            &lease_mounts);
        if (lease_snapshot_error == 0) {
            ++perf.mountinfo_snapshot_count;
            perf.mountinfo_read_ns += lease_mounts.read_ns;
            perf.mountinfo_parse_ns += lease_mounts.parse_ns;
        }
        lease_topology_valid = lease_snapshot_error == 0
            && pathguard::MountInfoSnapshotMatchesCurrentNamespace(lease_mounts)
            && CaptureStorageTopologyFromSnapshot(
                uid, plan.topology.source_root, lease_mounts, &lease_topology)
            && SameStorageTopology(transaction_topology, lease_topology);
        pathguard::DestroyMountInfoSnapshot(&lease_mounts);
        perf.topology_ns += pathguard::perf::ElapsedNs(topology_started);
    }
    if (!lease_process_valid || !lease_policy_valid || !lease_topology_valid) {
        perf.result = ESTALE;
        perf.runtime_reason = static_cast<uint32_t>(
            !lease_process_valid ? pathguard::RuntimeReason::kPreflightFailed
            : !lease_policy_valid ? pathguard::RuntimeReason::kPolicyChanged
            : pathguard::RuntimeReason::kTopologyChanged);
        return finish_preflight(ESTALE);
    }
    if (propagation_requires_private) {
        const size_t first_source_index = workspace->source_indexes[0];
        const pathguard::MountBackendProbe probe = ProbeMountBackendsIsolated(
            workspace->source_locators[first_source_index].path,
            workspace->target_locators[0].path);
        const pathguard::MountBackendSelection selection =
            pathguard::SelectMountBackend(
                required_actions, probe.capabilities, allow_legacy);
        perf.backend = static_cast<uint32_t>(selection.backend);
        perf.backend_reason = static_cast<uint32_t>(selection.reason);
        if (selection.backend == pathguard::MountBackendKind::kUnsupported) {
            perf.runtime_reason = static_cast<uint32_t>(
                pathguard::RuntimeReason::kPreflightFailed);
            return finish_preflight(
                probe.error != 0 ? probe.error : ENOTSUP);
        }
        if (selection.backend == pathguard::MountBackendKind::kLegacyString
            && !IsDisposableAppNamespace(
                pid, uid, namespace_identity.st_dev,
                namespace_identity.st_ino)) {
            perf.backend = static_cast<uint32_t>(
                pathguard::MountBackendKind::kUnsupported);
            perf.backend_reason = static_cast<uint32_t>(
                pathguard::MountBackendReason::kCapabilityMissing);
            return finish_preflight(ENOTSUP);
        }
        backend = selection.backend;
        backend_locked = true;
        LOGI("mount backend preflight required: pid=%d backend=%u",
             pid, static_cast<unsigned>(backend));
    }
    if (IsCancelRequested(state)) {
        return finish_preflight(ECANCELED);
    }
    if (!AcquireMutationLease(state)) {
        return finish_preflight(ECANCELED);
    }

    const uint64_t propagation_started = pathguard::perf::NowNs();
    bool propagation_changed = false;
    int error = 0;
    if (propagation_requires_private) {
        if (mount(nullptr, "/storage", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
            error = errno;
        } else {
            propagation_changed = true;
        }
    }
    perf.propagation_ns = pathguard::perf::ElapsedNs(propagation_started);

    const uint64_t mounts_started = pathguard::perf::NowNs();
    bool untracked_mutation = false;
    pathguard::MountInfoSnapshot mutation_snapshot;
    bool mutation_snapshot_ready = false;
    for (size_t rule_index = 0; rule_index < plan.count && error == 0; ++rule_index) {
        if (IsCancelRequested(state)) {
            error = ECANCELED;
            break;
        }
        const uint64_t mount_started = pathguard::perf::NowNs();
        const size_t source_index = workspace->source_indexes[rule_index];
        if (source_index >= pinned_source_count) {
            error = EINVAL;
            break;
        }
        pathguard::MountApplyResult apply_result;
        while (true) {
            apply_result = pathguard::ApplyDirectoryMountRaw(
                backend, static_cast<uint32_t>(rule_index),
                workspace->sources[source_index],
                workspace->targets[rule_index],
                workspace->source_locators[source_index],
                workspace->target_locators[rule_index],
                &workspace->timings[rule_index]);
            if (apply_result.ok() || apply_result.mutation_happened()
                || backend_locked || rule_index != 0) {
                break;
            }

            bool legacy_namespace_allowed = false;
            if (backend == pathguard::MountBackendKind::kStrictProcFd
                && allow_legacy
                && required_actions == pathguard::kMountActionRedirect) {
                legacy_namespace_allowed = IsDisposableAppNamespace(
                    pid, uid, namespace_identity.st_dev,
                    namespace_identity.st_ino);
            }
            const pathguard::MountBackendKind next =
                pathguard::NextMountBackend(
                    backend, required_actions, allow_legacy,
                    legacy_namespace_allowed);
            LOGI("mount backend unavailable: pid=%d backend=%u errno=%d next=%u",
                 pid, static_cast<unsigned>(backend),
                 apply_result.failure.error, static_cast<unsigned>(next));
            if (next == pathguard::MountBackendKind::kUnsupported) {
                perf.backend = static_cast<uint32_t>(next);
                perf.backend_reason = static_cast<uint32_t>(
                    required_actions == pathguard::kMountActionRedirect
                        && !allow_legacy
                    ? pathguard::MountBackendReason::kLegacyNotAuthorized
                    : pathguard::MountBackendReason::kCapabilityMissing);
                break;
            }
            backend = next;
            perf.backend = static_cast<uint32_t>(backend);
        }
        if (apply_result.ok() && apply_result.mutation_happened()) {
            backend_locked = true;
            perf.backend = static_cast<uint32_t>(backend);
            perf.backend_reason = static_cast<uint32_t>(
                pathguard::MountBackendReason::kNone);
        }
        // Record every successful namespace mutation before any later work.
        // Strict success does not need a mount ID; failures hydrate IDs from
        // one snapshot before rollback.
        if (apply_result.mutation_happened()) {
            if (!workspace->journal.Push(apply_result.mount)) {
                error = EOVERFLOW;
                untracked_mutation = true;
            }
        }
#if PATHGUARD_TEST_CRASH_AFTER_MOUNT
        if (workspace->journal.size() == 1) _exit(86);
#endif
        if (error == 0) error = apply_result.failure.error;
        perf.failure_stage = static_cast<uint32_t>(
            apply_result.failure.stage);
#if PATHGUARD_TEST_MOUNT_DELAY_MS > 0
        if (workspace->journal.size() == 1) {
            usleep(static_cast<useconds_t>(PATHGUARD_TEST_MOUNT_DELAY_MS) * 1000u);
        }
#endif
        const uint64_t mount_ns = pathguard::perf::ElapsedNs(mount_started);
        if (mount_ns > perf.mount_max_ns) perf.mount_max_ns = mount_ns;
    }
    if (error == 0 && IsCancelRequested(state)) error = ECANCELED;

    const auto log_mount_step = [&](size_t rule_index,
                                    const pathguard::MountError& outcome,
                                    uint64_t mount_id,
                                    const char* verification) {
        const pathguard::MountApplyTiming& timing =
            workspace->timings[rule_index];
        LOGI("perf mount_step rule=%zu backend=%u target_pin_us=%llu "
             "verify_pinned_us=%llu apply_raw_us=%llu verify_us=%llu "
             "verify_stat_us=%llu mutation=1 stage=%u errno=%d mount_id=%llu "
             "verification=%s",
             rule_index, static_cast<uint32_t>(backend),
             static_cast<unsigned long long>(
                 workspace->target_pin_ns[rule_index] / 1000u),
             static_cast<unsigned long long>(
                 timing.verify_pinned_ns / 1000u),
             static_cast<unsigned long long>(timing.apply_raw_ns / 1000u),
             static_cast<unsigned long long>(timing.verify_ns / 1000u),
             static_cast<unsigned long long>(timing.verify_stat_ns / 1000u),
             static_cast<unsigned>(outcome.stage), outcome.error,
             static_cast<unsigned long long>(mount_id), verification);
    };

    bool result_published = false;
    if (error == 0 && !pathguard::NeedsPostMountSnapshot(
            backend, !workspace->journal.empty(), true)) {
        perf.mount_total_ns = pathguard::perf::ElapsedNs(mounts_started);
        perf.result = 0;
        CompanionResult result = state->result;
        result.mount = perf;
        if (PublishSharedResult(state, result, MountState::kApplying,
                                MountState::kComplete)) {
            result_published = true;
            pathguard::MountError committed;
            for (size_t journal_index = 0;
                 journal_index < workspace->journal.size(); ++journal_index) {
                const pathguard::AppliedMount* recorded =
                    workspace->journal.At(journal_index);
                if (recorded != nullptr && recorded->operation_id < plan.count) {
                    log_mount_step(recorded->operation_id, committed, 0, "syscall");
                }
            }
        } else {
            error = ECANCELED;
        }
    }

    if (pathguard::NeedsPostMountSnapshot(
            backend, !workspace->journal.empty(), error == 0)) {
        const int final_snapshot_error = pathguard::CaptureMountInfoSnapshot(
            &mutation_snapshot);
        if (final_snapshot_error != 0) {
            if (error == 0) error = final_snapshot_error;
            perf.failure_stage = static_cast<uint32_t>(
                pathguard::MountOperationStage::kVerify);
        } else {
            mutation_snapshot_ready = true;
            ++perf.mountinfo_snapshot_count;
            perf.mountinfo_read_ns += mutation_snapshot.read_ns;
            perf.mountinfo_parse_ns += mutation_snapshot.parse_ns;
            const size_t expected_mount_count = workspace->journal.size();
            for (size_t journal_index = 0;
                 journal_index < workspace->journal.size(); ++journal_index) {
                const pathguard::AppliedMount* recorded =
                    workspace->journal.At(journal_index);
                if (recorded == nullptr
                    || recorded->operation_id >= plan.count) {
                    if (error == 0) error = EOVERFLOW;
                    untracked_mutation = true;
                    continue;
                }
                const size_t rule_index = recorded->operation_id;
                const size_t source_index =
                    workspace->source_indexes[rule_index];
                if (source_index >= pinned_source_count) {
                    if (error == 0) error = EOVERFLOW;
                    untracked_mutation = true;
                    continue;
                }
                pathguard::AppliedMount verified = *recorded;
                pathguard::MountError verify = pathguard::VerifyDirectoryMount(
                    current_mounts, mutation_snapshot,
                    workspace->sources[source_index],
                    workspace->targets[rule_index],
                    workspace->target_locators[rule_index], &verified,
                    expected_mount_count, &workspace->timings[rule_index]);
                if (verify.identity_confirmed != 0
                    && !workspace->journal.UpdateAt(journal_index, verified)) {
                    verify.stage = pathguard::MountOperationStage::kVerify;
                    verify.error = EOVERFLOW;
                    verify.mutation_happened = 1;
                    untracked_mutation = true;
                }
                if (error == 0 && verify.error != 0) error = verify.error;
                if (verify.error != 0) {
                    perf.failure_stage = static_cast<uint32_t>(verify.stage);
                }
                log_mount_step(
                    rule_index, verify, verified.mount_id, "mountinfo");
            }
        }
    }
    perf.mount_total_ns = pathguard::perf::ElapsedNs(mounts_started);
    if (error == 0 && !result_published) {
        perf.result = 0;
        CompanionResult result = state->result;
        result.mount = perf;
        if (!PublishSharedResult(state, result, MountState::kApplying,
                                 MountState::kComplete)) {
            error = ECANCELED;
        }
    }
    bool rollback_failed = false;
    if (error != 0) {
        const uint64_t rollback_started = pathguard::perf::NowNs();
        const auto record_rollback_failure = [
            &rollback_failed, &perf, &error](
                const pathguard::MountRollbackResult& rollback) {
            if (!rollback.ok()) {
                rollback_failed = true;
                perf.rollback_failed = 1;
                error = rollback.failure.error;
                LOGE("mount rollback failed: op=%u stage=%u errno=%d expected=%llu actual=%llu identity=%u",
                     rollback.failure.operation_id,
                     static_cast<unsigned>(rollback.failure.stage),
                     rollback.failure.error,
                     static_cast<unsigned long long>(rollback.failure.expected_mount_id),
                     static_cast<unsigned long long>(rollback.failure.actual_mount_id),
                     rollback.failure.identity_confirmed);
            }
        };
        pathguard::MountInfoSnapshot rollback_before;
        pathguard::MountInfoSnapshot rollback_after;
        const int rollback_snapshot_error = workspace->journal.empty()
            || mutation_snapshot_ready
            ? 0 : pathguard::CaptureMountInfoSnapshot(&rollback_before);
        if (rollback_snapshot_error == 0
            && rollback_before.mapping != nullptr) {
            ++perf.mountinfo_snapshot_count;
            perf.mountinfo_read_ns += rollback_before.read_ns;
            perf.mountinfo_parse_ns += rollback_before.parse_ns;
        }
        if (rollback_snapshot_error != 0) {
            pathguard::MountRollbackResult rollback;
            rollback.failure.stage =
                pathguard::MountOperationStage::kRollbackIdentity;
            rollback.failure.error = rollback_snapshot_error;
            rollback.failure.mutation_happened = 1;
            record_rollback_failure(rollback);
        }
        const pathguard::MountInfoSnapshot& rollback_identity_snapshot =
            mutation_snapshot_ready ? mutation_snapshot : rollback_before;
        for (size_t index = workspace->journal.size();
             !rollback_failed && index > 0; --index) {
            const pathguard::AppliedMount* applied =
                workspace->journal.At(index - 1);
            if (applied == nullptr) {
                pathguard::MountRollbackResult rollback;
                rollback.failure.stage =
                    pathguard::MountOperationStage::kRollbackIdentity;
                rollback.failure.error = EOVERFLOW;
                rollback.failure.mutation_happened = 1;
                record_rollback_failure(rollback);
                break;
            }
            record_rollback_failure(
                pathguard::ValidateRollbackDirectoryMount(
                    *applied, rollback_identity_snapshot));
        }
        for (size_t index = workspace->journal.size();
             !rollback_failed && index > 0; --index) {
            const pathguard::AppliedMount* applied =
                workspace->journal.At(index - 1);
            record_rollback_failure(
                UnmountTransactionMount(*applied));
        }
        if (!rollback_failed && !workspace->journal.empty()) {
            const int after_error = pathguard::CaptureMountInfoSnapshot(
                &rollback_after);
            if (after_error != 0) {
                pathguard::MountRollbackResult rollback;
                rollback.failure.stage =
                    pathguard::MountOperationStage::kRollbackVerify;
                rollback.failure.error = after_error;
                rollback.failure.mutation_happened = 1;
                rollback.failure.identity_confirmed = 1;
                record_rollback_failure(rollback);
            } else {
                ++perf.mountinfo_snapshot_count;
                perf.mountinfo_read_ns += rollback_after.read_ns;
                perf.mountinfo_parse_ns += rollback_after.parse_ns;
            }
        }
        for (size_t index = workspace->journal.size();
             !rollback_failed && index > 0; --index) {
            const pathguard::AppliedMount* applied =
                workspace->journal.At(index - 1);
            record_rollback_failure(
                pathguard::VerifyRollbackDirectoryMount(
                    *applied, rollback_after));
        }
        if (!rollback_failed) workspace->journal.Clear();
        pathguard::DestroyMountInfoSnapshot(&rollback_after);
        pathguard::DestroyMountInfoSnapshot(&rollback_before);
        perf.rollback_ns = pathguard::perf::ElapsedNs(rollback_started);
    }
    for (size_t index = 0; index < pinned_source_count; ++index) {
        pathguard::ClosePinnedIdentity(&workspace->sources[index]);
    }
    for (size_t index = 0; index < pinned_target_count; ++index) {
        pathguard::ClosePinnedIdentity(&workspace->targets[index]);
    }
    pathguard::DestroyMountInfoSnapshot(&mutation_snapshot);
    pathguard::DestroyMountInfoSnapshot(&current_mounts);
    DestroyMountTransactionWorkspace(workspace);
    perf.result = error;
    if (error != 0) {
        CompanionResult result = state->result;
        result.mount = perf;
        const MountState current = LoadSharedStatus(state);
        if (propagation_changed || rollback_failed || untracked_mutation) {
            PublishSharedResult(state, result, current,
                                MountState::kNamespaceTainted);
            const NamespaceTerminationResult termination = TerminateNamespaceMembers(
                namespace_identity.st_dev, namespace_identity.st_ino);
            LOGE("namespace tainted: pid=%d matched=%zu signaled=%zu remaining=%zu",
                 pid, termination.matched, termination.signaled,
                 termination.remaining);
        } else if (current == MountState::kCancelRequested) {
            PublishSharedResult(state, result, MountState::kCancelRequested,
                                MountState::kRollbackComplete);
        } else if (current == MountState::kApplying) {
            PublishSharedResult(state, result, MountState::kApplying,
                                MountState::kRollbackComplete);
        }
    }
    return perf;
}

void LogZygiskPrePerf(pid_t pid, jint uid, uint32_t rule_count,
                      const PolicyLoadPerf& policy, uint64_t hook_ns,
                      uint64_t connect_ns, uint64_t send_ns,
                      uint64_t total_ns, bool hook_required,
                      bool hook_deferred, bool request_sent) {
    LOGI("perf zygisk_pre pid=%d uid=%d rules=%u hook_required=%d hook_deferred=%d request_sent=%d "
         "policy_open_us=%llu policy_lookup_us=%llu policy_unmap_us=%llu "
         "hook_us=%llu companion_connect_us=%llu companion_send_us=%llu total_us=%llu",
          pid, uid, rule_count, hook_required ? 1 : 0, hook_deferred ? 1 : 0,
         request_sent ? 1 : 0,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(policy.open_map_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(policy.lookup_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(policy.unmap_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(hook_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(connect_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(send_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(total_ns)));
}

SharedMountState* CreateSharedState(int* shared_fd) {
    if (shared_fd == nullptr) return nullptr;
    *shared_fd = static_cast<int>(syscall(
        SYS_memfd_create, "pathguard-result", MFD_CLOEXEC));
    if (*shared_fd < 0 || ftruncate(*shared_fd, sizeof(SharedMountState)) != 0) {
        if (*shared_fd >= 0) close(*shared_fd);
        *shared_fd = -1;
        return nullptr;
    }
    void* mapping = mmap(nullptr, sizeof(SharedMountState), PROT_READ | PROT_WRITE,
                         MAP_SHARED, *shared_fd, 0);
    if (mapping == MAP_FAILED) {
        close(*shared_fd);
        *shared_fd = -1;
        return nullptr;
    }
    auto* state = static_cast<SharedMountState*>(mapping);
    memset(state, 0, sizeof(*state));
    state->magic = kSharedStateMagic;
    state->version = kSharedStateVersion;
    state->result.magic = kCompanionResultMagic;
    state->result.version = kCompanionResultVersion;
    return state;
}

SharedMountState* MapSharedState(int shared_fd) {
    struct stat file_stat {};
    if (shared_fd < 0 || fstat(shared_fd, &file_stat) != 0
        || file_stat.st_size != static_cast<off_t>(sizeof(SharedMountState))) {
        return nullptr;
    }
    void* mapping = mmap(nullptr, sizeof(SharedMountState), PROT_READ | PROT_WRITE,
                         MAP_SHARED, shared_fd, 0);
    if (mapping == MAP_FAILED) return nullptr;
    auto* state = static_cast<SharedMountState*>(mapping);
    if (state->magic != kSharedStateMagic || state->version != kSharedStateVersion) {
        munmap(state, sizeof(*state));
        return nullptr;
    }
    return state;
}

SharedRuntimeStatus* CreateSharedRuntimeStatus(int* shared_fd) {
    if (shared_fd == nullptr) return nullptr;
    *shared_fd = static_cast<int>(syscall(
        SYS_memfd_create, "pathguard-status", MFD_CLOEXEC));
    if (*shared_fd < 0
        || ftruncate(*shared_fd, sizeof(SharedRuntimeStatus)) != 0) {
        if (*shared_fd >= 0) close(*shared_fd);
        *shared_fd = -1;
        return nullptr;
    }
    void* mapping = mmap(nullptr, sizeof(SharedRuntimeStatus),
                         PROT_READ | PROT_WRITE, MAP_SHARED, *shared_fd, 0);
    if (mapping == MAP_FAILED) {
        close(*shared_fd);
        *shared_fd = -1;
        return nullptr;
    }
    auto* state = static_cast<SharedRuntimeStatus*>(mapping);
    memset(state, 0, sizeof(*state));
    state->magic = kSharedStatusMagic;
    state->version = kSharedStatusVersion;
    return state;
}

SharedRuntimeStatus* MapSharedRuntimeStatus(int shared_fd) {
    struct stat file_stat {};
    if (shared_fd < 0 || fstat(shared_fd, &file_stat) != 0
        || file_stat.st_size
            != static_cast<off_t>(sizeof(SharedRuntimeStatus))) {
        return nullptr;
    }
    void* mapping = mmap(nullptr, sizeof(SharedRuntimeStatus),
                         PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0);
    if (mapping == MAP_FAILED) return nullptr;
    auto* state = static_cast<SharedRuntimeStatus*>(mapping);
    if (state->magic != kSharedStatusMagic
        || state->version != kSharedStatusVersion) {
        munmap(state, sizeof(*state));
        return nullptr;
    }
    return state;
}

bool WaitForRuntimeStatus(SharedRuntimeStatus* state, int timeout_ms) {
    if (state == nullptr || timeout_ms <= 0) return false;
    const uint64_t deadline = pathguard::perf::NowNs()
        + static_cast<uint64_t>(timeout_ms) * 1000000u;
    while (true) {
        const uint32_t current = __atomic_load_n(&state->state, __ATOMIC_ACQUIRE);
        if (current == 1u) return true;
        if (current != 0u) return false;
        const uint64_t now = pathguard::perf::NowNs();
        if (now >= deadline) return false;
        const uint64_t remaining = deadline - now;
        const timespec timeout{
            static_cast<time_t>(remaining / 1000000000u),
            static_cast<long>(remaining % 1000000000u),
        };
        syscall(SYS_futex, &state->state, FUTEX_WAIT, 0u,
                &timeout, nullptr, 0);
    }
}

bool WaitForSharedResult(SharedMountState* state, int timeout_ms) {
    uint64_t deadline = pathguard::perf::NowNs()
        + static_cast<uint64_t>(timeout_ms) * 1000000u;
    bool preflight_grace_used = false;
    bool applying_grace_used = false;
    while (true) {
        const MountState current = LoadSharedStatus(state);
        if (pathguard::MountTransactionHasResult(current)) return true;
        if (current == MountState::kCancelled) return false;
        const uint64_t now = pathguard::perf::NowNs();
        if (now >= deadline) {
            if (current == MountState::kPending) {
                if (TransitionSharedState(state, MountState::kPending,
                                          MountState::kCancelRequested)) {
                    return false;
                }
                continue;
            }
            if (current == MountState::kPreflighting
                && !preflight_grace_used) {
                preflight_grace_used = true;
                deadline = now
                    + static_cast<uint64_t>(kPreflightCompletionGraceMs)
                        * 1000000u;
                continue;
            }
            if (current == MountState::kPreflighting) {
                if (TransitionSharedState(
                        state, MountState::kPreflighting,
                        MountState::kCancelRequested)) {
                    return false;
                }
                continue;
            }
            if (current == MountState::kApplying && !applying_grace_used) {
                applying_grace_used = true;
                deadline = now
                    + static_cast<uint64_t>(kApplyingCompletionGraceMs) * 1000000u;
                continue;
            }
            if (current == MountState::kApplying
                && !TransitionSharedState(state, MountState::kApplying,
                                          MountState::kCancelRequested)) {
                continue;
            }
            if (pathguard::IsMountTransactionTerminal(current)) {
                return pathguard::MountTransactionHasResult(current);
            }
            const uint64_t owner_deadline = pathguard::perf::NowNs()
                + static_cast<uint64_t>(kApplyingOwnerDeathTimeoutMs) * 1000000u;
            while (true) {
                const MountState applying_state = LoadSharedStatus(state);
                if (pathguard::MountTransactionHasResult(applying_state)) return true;
                if (applying_state == MountState::kCancelled) return false;
                const uint64_t applying_now = pathguard::perf::NowNs();
                if (applying_now >= owner_deadline) {
                    LOGE("mount transaction owner unavailable; terminating target pid=%d state=%u",
                         getpid(), StateValue(applying_state));
                    kill(getpid(), SIGKILL);
                    return false;
                }
                const uint64_t owner_remaining = owner_deadline - applying_now;
                const timespec owner_timeout{
                    static_cast<time_t>(owner_remaining / 1000000000u),
                    static_cast<long>(owner_remaining % 1000000000u),
                };
                syscall(SYS_futex, &state->status, FUTEX_WAIT,
                        StateValue(applying_state), &owner_timeout, nullptr, 0);
            }
        }
        const uint64_t remaining = deadline - now;
        const timespec timeout{
            static_cast<time_t>(remaining / 1000000000u),
            static_cast<long>(remaining % 1000000000u),
        };
        syscall(SYS_futex, &state->status, FUTEX_WAIT, StateValue(current),
                &timeout, nullptr, 0);
    }
}

bool SendPlanWithSharedFd(int fd, jint uid, const ProcessPlan& plan,
                          int shared_fd, int module_dir_fd) {
    if (module_dir_fd < 0 || plan.process_name[0] == '\0') return false;
    BootstrapHeader header{};
    header.magic = kBootstrapMagic;
    header.version = kBootstrapVersion;
    header.pid = getpid();
    header.uid = uid;
    header.rule_count = plan.count;
    header.policy_flags = plan.policy_flags;
    header.snapshot_generation = plan.snapshot_generation;
    header.plan_generation = plan.plan_generation;
    header.process_name_length = static_cast<uint32_t>(strlen(plan.process_name));
    header.feature_flags = plan.provider_enabled
        ? kBootstrapFeatureProviderEnabled : 0u;
    header.app_path_install = EncodePathInstallTelemetry(
        plan.app_path_install, plan.app_path_status_available);
    int descriptors[2] = {shared_fd, module_dir_fd};
    char control[CMSG_SPACE(sizeof(descriptors))]{};
    iovec header_iov{&header, sizeof(header)};
    msghdr message{};
    message.msg_iov = &header_iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr* descriptor = CMSG_FIRSTHDR(&message);
    descriptor->cmsg_level = SOL_SOCKET;
    descriptor->cmsg_type = SCM_RIGHTS;
    descriptor->cmsg_len = CMSG_LEN(sizeof(descriptors));
    memcpy(CMSG_DATA(descriptor), descriptors, sizeof(descriptors));
    const ssize_t sent = sendmsg(fd, &message, MSG_NOSIGNAL);
    if (sent != static_cast<ssize_t>(sizeof(header))) return false;
    if (!WriteFully(fd, plan.process_name, header.process_name_length)) return false;
    for (uint32_t index = 0; index < plan.count; ++index) {
        const PlannedMount& mount = plan.mounts[index];
        const char* visible = PlanPath(plan, mount.visible_path);
        const char* backing = PlanPath(plan, mount.backing_path);
        if (visible == nullptr || backing == nullptr) return false;
        const uint32_t visible_length = static_cast<uint32_t>(strlen(visible));
        const uint32_t backing_length = static_cast<uint32_t>(strlen(backing));
        if (!WriteFully(fd, &mount.action, sizeof(mount.action))
            || !WriteFully(fd, &visible_length, sizeof(visible_length))
            || !WriteFully(fd, visible, visible_length)
            || !WriteFully(fd, &backing_length, sizeof(backing_length))
            || !WriteFully(fd, backing, backing_length)) {
            return false;
        }
    }
    return true;
}

bool SendRuntimeStatusBootstrap(int fd, uid_t uid, int shared_fd,
                                int module_dir_fd) {
    if (fd < 0 || shared_fd < 0 || module_dir_fd < 0) return false;
    StatusSubmissionHeader header;
    header.pid = getpid();
    header.uid = uid;
    if (!ReadProcessStartTime(header.pid, &header.process_start_time)) {
        return false;
    }
    int descriptors[2] = {shared_fd, module_dir_fd};
    char control[CMSG_SPACE(sizeof(descriptors))]{};
    iovec header_iov{&header, sizeof(header)};
    msghdr message{};
    message.msg_iov = &header_iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr* descriptor = CMSG_FIRSTHDR(&message);
    descriptor->cmsg_level = SOL_SOCKET;
    descriptor->cmsg_type = SCM_RIGHTS;
    descriptor->cmsg_len = CMSG_LEN(sizeof(descriptors));
    memcpy(CMSG_DATA(descriptor), descriptors, sizeof(descriptors));
    return sendmsg(fd, &message, MSG_NOSIGNAL)
        == static_cast<ssize_t>(sizeof(header));
}

bool ReceiveBootstrap(int fd, BootstrapHeader* header, int* shared_fd,
                      int* module_dir_fd) {
    if (header == nullptr || shared_fd == nullptr || module_dir_fd == nullptr) {
        return false;
    }
    *shared_fd = -1;
    *module_dir_fd = -1;
    int descriptors[2] = {-1, -1};
    char control[CMSG_SPACE(sizeof(descriptors))]{};
    iovec header_iov{header, sizeof(*header)};
    msghdr message{};
    message.msg_iov = &header_iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    const ssize_t received = recvmsg(fd, &message, MSG_WAITALL);
    if (received != static_cast<ssize_t>(sizeof(*header))) return false;
    for (cmsghdr* descriptor = CMSG_FIRSTHDR(&message); descriptor != nullptr;
         descriptor = CMSG_NXTHDR(&message, descriptor)) {
        if (descriptor->cmsg_level == SOL_SOCKET
            && descriptor->cmsg_type == SCM_RIGHTS
            && descriptor->cmsg_len >= CMSG_LEN(sizeof(descriptors))) {
            memcpy(descriptors, CMSG_DATA(descriptor), sizeof(descriptors));
            *shared_fd = descriptors[0];
            *module_dir_fd = descriptors[1];
            break;
        }
    }
    return *shared_fd >= 0 && *module_dir_fd >= 0;
}

bool ReceiveRuntimeStatusBootstrap(
        int fd, StatusSubmissionHeader* header,
        int* shared_fd, int* module_dir_fd) {
    if (header == nullptr || shared_fd == nullptr || module_dir_fd == nullptr) {
        return false;
    }
    *shared_fd = -1;
    *module_dir_fd = -1;
    int descriptors[2] = {-1, -1};
    char control[CMSG_SPACE(sizeof(descriptors))]{};
    iovec header_iov{header, sizeof(*header)};
    msghdr message{};
    message.msg_iov = &header_iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    if (recvmsg(fd, &message, MSG_WAITALL)
        != static_cast<ssize_t>(sizeof(*header))) return false;
    for (cmsghdr* descriptor = CMSG_FIRSTHDR(&message); descriptor != nullptr;
         descriptor = CMSG_NXTHDR(&message, descriptor)) {
        if (descriptor->cmsg_level == SOL_SOCKET
            && descriptor->cmsg_type == SCM_RIGHTS
            && descriptor->cmsg_len >= CMSG_LEN(sizeof(descriptors))) {
            memcpy(descriptors, CMSG_DATA(descriptor), sizeof(descriptors));
            *shared_fd = descriptors[0];
            *module_dir_fd = descriptors[1];
            break;
        }
    }
    return *shared_fd >= 0 && *module_dir_fd >= 0;
}

class PathGuardModule final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
        LOGI("module_onload pid=%d", getpid());
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        const uint64_t pre_started = pathguard::perf::NowNs();
        const char* process_name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (process_name == nullptr) {
            Unload();
            return;
        }

        ProcessPlan plan;
        PolicyLoadPerf policy_perf;
        const int module_dir = api_->getModuleDir();
        const bool external_storage_provider =
            strcmp(process_name, kExternalStorageProviderProcess) == 0;
        const bool media_provider =
            strcmp(process_name, kMainlineMediaProviderProcess) == 0;
        if (external_storage_provider || media_provider) {
            PathPolicyMapping provider_policy;
            const bool loaded = module_dir >= 0
                && LoadProviderPolicy(module_dir, &provider_policy);
            env_->ReleaseStringUTFChars(args->nice_name, process_name);
            provider_redirect_required_ = loaded;
            provider_process_ = true;
            provider_media_process_ = media_provider;
            provider_uid_ = static_cast<uid_t>(args->uid);
            if (!loaded) {
                if (module_dir >= 0) close(module_dir);
                LOGI("provider redirect has no active rules");
                Unload();
                return;
            }
            path_policy_ = provider_policy;
            provider_lsplant_ready_ = PrepareProviderLsplant(
                module_dir, media_provider, &provider_bridge_probe_,
                &provider_lsplant_handle_, &provider_lsplant_initialize_,
                &provider_lsplant_install_, &provider_lsplant_wait_,
                &provider_lsplant_configure_,
                &provider_hooker_dex_);
            if (provider_lsplant_ready_) {
                const int configure_result = provider_lsplant_configure_(nullptr);
                if (configure_result != 0) {
                    provider_bridge_probe_.bridge_errno = configure_result;
                    provider_lsplant_ready_ = false;
                }
                PathGuardLsplantBridgeResultV1 bridge_result{};
                const int init_result = provider_lsplant_ready_
                    ? provider_lsplant_initialize_(env_, &bridge_result)
                    : provider_bridge_probe_.bridge_errno;
                provider_bridge_probe_.lsplant_initialized =
                    init_result == 0 && bridge_result.lsplant_initialized != 0;
                provider_bridge_probe_.bridge_errno = init_result;
                provider_lsplant_ready_ =
                    provider_bridge_probe_.lsplant_initialized;
                if (!provider_lsplant_ready_) {
                    ReleaseModuleBytes(&provider_hooker_dex_);
                    dlclose(provider_lsplant_handle_);
                    provider_lsplant_handle_ = nullptr;
                }
            }
            PrepareProviderRuntimeStatus(module_dir, provider_uid_);
            if (module_dir >= 0) close(module_dir);
            LOGI("provider redirect prepared: process=%s scopes=%u caller_scope=binder_uid",
                 media_provider ? "media" : "external_storage",
                 provider_policy.scope_count);
            return;
        }
        const bool diagnostic = strcmp(process_name, kDiagnosticPackage) == 0;
        if (diagnostic) LOGI("pre_specialize_enter pid=%d uid=%d module_fd=%d",
                             getpid(), args->uid, module_dir);
        PathPolicyMapping app_path_policy;
        const bool app_path_loaded = module_dir >= 0
            && LoadAppPathPolicy(module_dir, process_name, args->uid,
                                 &app_path_policy);
        const bool matched = module_dir >= 0
            && LoadProcessPlan(module_dir, process_name, args->uid, &plan, &policy_perf);
        char process_name_copy[PATH_MAX]{};
        snprintf(process_name_copy, sizeof(process_name_copy), "%s", process_name);
        env_->ReleaseStringUTFChars(args->nice_name, process_name);
        if (!matched && !app_path_loaded) {
            if (module_dir >= 0) close(module_dir);
            if (diagnostic) LOGE("pre_specialize_no_plan pid=%d uid=%d",
                                 getpid(), args->uid);
            Unload();
            return;
        }

        if (app_path_loaded) {
            const pathguard::provider_redirect::PolicyConfig config{
                static_cast<const uint8_t*>(app_path_policy.mapping),
                app_path_policy.size,
                app_path_policy.scopes,
                app_path_policy.scope_count,
                pathguard::AdmissionDomain::kAppPath,
                pathguard::provider_redirect::IdentityMode::kProcessUid,
            };
            const auto install_result = pathguard::provider_redirect::Install(
                api_, env_, config);
            provider_redirect_required_ = true;
            provider_redirect_installed_ = install_result.virtualization_active;
            provider_redirect_module_retained_ =
                pathguard::provider_redirect::MustRetainModule(install_result);
            if (matched) {
                plan.app_path_status_available = true;
                plan.app_path_install = install_result;
            } else {
                WritePathRuntimeStatus(
                    module_dir, process_name_copy, static_cast<uid_t>(args->uid),
                    app_path_policy, pathguard::AdmissionDomain::kAppPath,
                    install_result);
            }
            if (provider_redirect_module_retained_) {
                path_policy_ = app_path_policy;
            } else {
                ReleasePathPolicy(&app_path_policy);
            }
            LOGI("app path specialize: uid=%d attempted=%d committed=%d active=%d capabilities=%llx operations=%llx",
                 args->uid,
                 install_result.hook_registration_attempted ? 1 : 0,
                 install_result.hooks_committed ? 1 : 0,
                 install_result.virtualization_active ? 1 : 0,
                 static_cast<unsigned long long>(install_result.observed_capabilities),
                 static_cast<unsigned long long>(install_result.observed_operations));
        }
        if (!matched) {
            if (module_dir >= 0) close(module_dir);
            if (!provider_redirect_module_retained_) Unload();
            return;
        }

        media_query_hook_required_ = plan.provider_enabled;
        if (media_query_hook_required_) {
            bool has_deny = false;
            for (uint32_t index = 0; index < plan.count; ++index) {
                if (plan.mounts[index].action == kDenyAction) {
                    has_deny = true;
                    break;
                }
            }
            media_query_hook_required_ = has_deny;
            if (has_deny) {
                media_plan_ = plan;
                media_uid_ = args->uid;
            }
        }

        const uint64_t hook_ns = 0;

        const uint64_t connect_started = pathguard::perf::NowNs();
        const int companion_fd = api_->connectCompanion();
        const uint64_t connect_ns = pathguard::perf::ElapsedNs(connect_started);
        if (companion_fd < 0) {
            close(module_dir);
            LOGE("cannot connect companion socket");
            LogZygiskPrePerf(getpid(), args->uid, plan.count, policy_perf,
                             hook_ns, connect_ns, 0, pathguard::perf::ElapsedNs(pre_started),
                             false, false, false);
            Unload();
            return;
        }
        SetSocketTimeout(companion_fd, kCompanionIoTimeoutMs);
        int shared_fd = -1;
        SharedMountState* shared_state = CreateSharedState(&shared_fd);
        if (shared_state == nullptr) {
            LOGE("cannot create shared result state");
            close(companion_fd);
            close(module_dir);
            Unload();
            return;
        }
        const uint64_t send_started = pathguard::perf::NowNs();
        if (!SendPlanWithSharedFd(
                companion_fd, args->uid, plan, shared_fd, module_dir)) {
            LOGE("cannot send process plan to companion");
            LogZygiskPrePerf(getpid(), args->uid, plan.count, policy_perf,
                             hook_ns, connect_ns, pathguard::perf::ElapsedNs(send_started),
                             pathguard::perf::ElapsedNs(pre_started),
                             false, false, false);
            munmap(shared_state, sizeof(*shared_state));
            close(shared_fd);
            Unload();
        } else {
            mount_request_sent_ = true;
            mount_shared_state_ = shared_state;
            close(shared_fd);
            LogZygiskPrePerf(getpid(), args->uid, plan.count, policy_perf,
                             hook_ns, connect_ns, pathguard::perf::ElapsedNs(send_started),
                             pathguard::perf::ElapsedNs(pre_started),
                             false, false, true);
        }
        close(module_dir);
        close(companion_fd);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if (provider_process_) {
            bool bridge_deferred = false;
            if (provider_lsplant_ready_) {
                PathGuardLsplantBridgeResultV1 bridge_result{};
                const int hook_result = provider_lsplant_install_(
                    env_, provider_media_process_
                        ? PATHGUARD_LSPLANT_PROVIDER_MEDIA
                        : PATHGUARD_LSPLANT_PROVIDER_DOCUMENTS,
                    provider_hooker_dex_.data, provider_hooker_dex_.size,
                    &bridge_result);
                bridge_deferred = hook_result == EINPROGRESS;
                ApplyProviderBridgeResult(bridge_result, hook_result);
                ReleaseModuleBytes(&provider_hooker_dex_);
            }
            InstallPreparedProviderHooks();
            if (bridge_deferred) {
                const int thread_result = StartProviderBridgeWait();
                if (thread_result == 0) return;
                provider_bridge_probe_.bridge_errno = thread_result;
            }
            PublishPreparedProviderStatus();
            if (!provider_redirect_installed_
                && !provider_redirect_module_retained_) Unload();
            return;
        }
        const uint64_t post_started = pathguard::perf::NowNs();
        if (mount_request_sent_ && mount_shared_state_ != nullptr) {
            CompanionResult result;
            const uint64_t result_started = pathguard::perf::NowNs();
            const bool received = WaitForSharedResult(mount_shared_state_, kAppResultTimeoutMs);
            const uint64_t result_ns = pathguard::perf::ElapsedNs(result_started);
            if (received) result = mount_shared_state_->result;
            const MountState final_state = LoadSharedStatus(mount_shared_state_);
            munmap(mount_shared_state_, sizeof(*mount_shared_state_));
            mount_shared_state_ = nullptr;
            const bool valid = received
                && result.magic == kCompanionResultMagic
                && result.version == kCompanionResultVersion;
            LOGI("perf zygisk_post pid=%d request_sent=1 result_received=%d "
                 "ready_us=%llu result_wait_us=%llu mount_us=%llu total_us=%llu result=%d",
                 getpid(), valid ? 1 : 0,
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(
                     valid ? result.ready_ns : 0)),
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(result_ns)),
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(
                     valid ? result.mount.mount_total_ns : 0)),
                 static_cast<unsigned long long>(pathguard::perf::NsToUs(
                     pathguard::perf::ElapsedNs(post_started))),
                  valid ? result.mount.result : ETIMEDOUT);
            if (valid && result.mount.result == 0
                && final_state == MountState::kComplete) {
                LOGI("redirect mount active: pid=%d backend=%u", getpid(),
                     result.mount.backend);
                if (media_query_hook_required_) {
                    char primary_paths[kMaxMountRules][PATH_MAX]{};
                    const char* deny_paths[kMaxMountRules]{};
                    char storage_root[64]{};
                    size_t deny_path_count = 0;
                    const int storage_root_length = snprintf(
                        storage_root, sizeof(storage_root), "/storage/emulated/%u",
                        static_cast<unsigned>(media_uid_) / 100000u);
                    if (storage_root_length <= 0
                        || static_cast<size_t>(storage_root_length)
                            >= sizeof(storage_root)) {
                        media_query_hook_required_ = false;
                    }
                    for (uint32_t index = 0;
                         media_query_hook_required_ && index < media_plan_.count;
                         ++index) {
                        if (media_plan_.mounts[index].action != kDenyAction) continue;
                        const char* visible = PlanPath(
                            media_plan_, media_plan_.mounts[index].visible_path);
                        if (visible == nullptr || !BuildPathUnderRoot(
                                storage_root, visible,
                                primary_paths[deny_path_count],
                                sizeof(primary_paths[deny_path_count]))) {
                            media_query_hook_required_ = false;
                            break;
                        }
                        deny_paths[deny_path_count] = primary_paths[deny_path_count];
                        ++deny_path_count;
                    }
                    const uint64_t hook_started = pathguard::perf::NowNs();
                    if (media_query_hook_required_ && deny_path_count > 0) {
                        media_query_hook_installed_ = pathguard::media_query::Install(
                            api_, env_, deny_paths, deny_path_count, media_uid_);
                    }
                    LOGI("perf media_hook_install installed=%d deny=%zu elapsed_us=%llu",
                         media_query_hook_installed_ ? 1 : 0, deny_path_count,
                         static_cast<unsigned long long>(pathguard::perf::NsToUs(
                             pathguard::perf::ElapsedNs(hook_started))));
                    if (!media_query_hook_installed_) {
                        LOGE("media query hook unavailable after successful mount");
                    }
                }
            } else if (final_state == MountState::kNamespaceTainted) {
                LOGE("redirect namespace tainted; process must terminate: pid=%d",
                     getpid());
            } else {
                LOGE("redirect mount failed or timed out; fail-open: pid=%d",
                     getpid());
            }
        }
        if (!media_query_hook_installed_
            && !provider_redirect_module_retained_) Unload();
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs*) override { Unload(); }

private:
    void ApplyProviderBridgeResult(
            const PathGuardLsplantBridgeResultV1& result,
            int operation_result) {
        provider_bridge_probe_.hooker_dex_loaded =
            result.hooker_dex_loaded != 0;
        provider_bridge_probe_.resolved_methods = result.resolved_methods;
        provider_bridge_probe_.installed_hooks = result.installed_hooks;
        provider_bridge_probe_.backup_methods = result.backup_methods;
        provider_bridge_probe_.self_tested_hooks = result.self_tested_hooks;
        provider_bridge_probe_.bridge_errno = operation_result == 0
            ? result.bridge_errno : operation_result;
    }

    static void* ProviderBridgeWaitMain(void* context) {
        auto* module = static_cast<PathGuardModule*>(context);
        PathGuardLsplantBridgeResultV1 result{};
        const int wait_result = module->provider_lsplant_wait_(
            kProviderBridgeWaitTimeoutMs, &result);
        module->ApplyProviderBridgeResult(result, wait_result);
        module->PublishPreparedProviderStatus();
        LOGI("Provider LSPlant deferred result: process=%s errno=%d methods=%llx",
             module->provider_media_process_ ? "media" : "external_storage",
             module->provider_bridge_probe_.bridge_errno,
             static_cast<unsigned long long>(
                 module->provider_bridge_probe_.self_tested_hooks));
        return nullptr;
    }

    int StartProviderBridgeWait() {
        pthread_t thread{};
        const int result = pthread_create(
            &thread, nullptr, ProviderBridgeWaitMain, this);
        if (result == 0) pthread_detach(thread);
        return result;
    }

    void PrepareProviderRuntimeStatus(int module_dir, uid_t uid) {
        if (module_dir < 0) return;
        const int companion = api_->connectCompanion();
        if (companion < 0) {
            LOGE("provider runtime status companion connection failed");
            return;
        }
        SetSocketTimeout(companion, kCompanionIoTimeoutMs);
        int shared_fd = -1;
        SharedRuntimeStatus* shared = CreateSharedRuntimeStatus(&shared_fd);
        if (shared == nullptr) {
            LOGE("provider runtime status shared state creation failed");
            close(companion);
            return;
        }
        if (!SendRuntimeStatusBootstrap(companion, uid, shared_fd, module_dir)) {
            LOGE("provider runtime status bootstrap failed");
            CancelRuntimeStatus(shared);
            munmap(shared, sizeof(*shared));
        } else {
            provider_status_shared_ = shared;
        }
        close(shared_fd);
        close(companion);
    }

    void InstallPreparedProviderHooks() {
        if (!provider_redirect_required_ || path_policy_.mapping == nullptr) {
            return;
        }
        const pathguard::provider_redirect::PolicyConfig config{
            static_cast<const uint8_t*>(path_policy_.mapping),
            path_policy_.size,
            path_policy_.scopes,
            path_policy_.scope_count,
            pathguard::AdmissionDomain::kProvider,
            pathguard::provider_redirect::IdentityMode::kBinderCallerUid,
        };
        provider_install_result_ = pathguard::provider_redirect::Install(
            api_, env_, config);
        provider_redirect_installed_ =
            provider_install_result_.virtualization_active;
        provider_redirect_module_retained_ =
            pathguard::provider_redirect::MustRetainModule(
                provider_install_result_);
        LOGI("provider redirect post-specialize: process=%s scopes=%u caller_scope=binder_uid attempted=%d committed=%d installed=%d capabilities=%llx operations=%llx",
             provider_media_process_ ? "media" : "external_storage",
             config.scope_count,
             provider_install_result_.hook_registration_attempted ? 1 : 0,
             provider_install_result_.hooks_committed ? 1 : 0,
             provider_redirect_installed_ ? 1 : 0,
             static_cast<unsigned long long>(
                 provider_install_result_.observed_capabilities),
             static_cast<unsigned long long>(
                 provider_install_result_.observed_operations));
    }

    void PublishPreparedProviderStatus() {
        const char* process_name = provider_media_process_
            ? kMainlineMediaProviderProcess : kExternalStorageProviderProcess;
        pathguard::RuntimeStatusRecord status;
        const bool status_built = BuildPathRuntimeStatus(
            getpid(), provider_uid_, path_policy_,
            pathguard::AdmissionDomain::kProvider,
            provider_install_result_, &status);
        status.provider_bridge = provider_bridge_probe_;
        status.observed_capabilities &= ~pathguard::kCapabilityProviderQueryInsertMapping;
        const bool status_published = status_built
            && PublishRuntimeStatusRecord(
                provider_status_shared_, process_name, status);
        if (!status_published) {
            CancelRuntimeStatus(provider_status_shared_);
            LOGE("provider runtime status submission failed: process=%s",
                 provider_media_process_ ? "media" : "external_storage");
        }
        if (provider_status_shared_ != nullptr) {
            munmap(provider_status_shared_, sizeof(*provider_status_shared_));
            provider_status_shared_ = nullptr;
        }
        if (!provider_redirect_module_retained_) {
            ReleasePathPolicy(&path_policy_);
        }
    }

    void Unload() { api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }

    zygisk::Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool provider_redirect_required_ = false;
    bool provider_process_ = false;
    bool provider_media_process_ = false;
    bool provider_redirect_installed_ = false;
    bool provider_redirect_module_retained_ = false;
    uid_t provider_uid_ = 0;
    bool media_query_hook_installed_ = false;
    bool media_query_hook_required_ = false;
    bool mount_request_sent_ = false;
    SharedMountState* mount_shared_state_ = nullptr;
    SharedRuntimeStatus* provider_status_shared_ = nullptr;
    ProcessPlan media_plan_;
    jint media_uid_ = 0;
    PathPolicyMapping path_policy_;
    pathguard::provider_redirect::InstallResult provider_install_result_;
    bool provider_lsplant_ready_ = false;
    void* provider_lsplant_handle_ = nullptr;
    PathGuardLsplantInitializeV1 provider_lsplant_initialize_ = nullptr;
    PathGuardLsplantInstallPassthroughV1 provider_lsplant_install_ = nullptr;
    PathGuardLsplantWaitPassthroughV1 provider_lsplant_wait_ = nullptr;
    PathGuardLsplantConfigureMappingV1 provider_lsplant_configure_ = nullptr;
    ModuleBytes provider_hooker_dex_;
    pathguard::ProviderJavaBridgeStatusV1 provider_bridge_probe_;
};

bool ForwardProvenanceRequest(int client) {
    pathguard::provenance_protocol::Request request;
    if (recv(client, &request, sizeof(request), MSG_WAITALL)
        != static_cast<ssize_t>(sizeof(request))
        || request.magic != pathguard::provenance_protocol::kMagic
        || request.version != pathguard::provenance_protocol::kVersion) {
        return false;
    }
    const int broker = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (broker < 0) return false;
    SetSocketTimeout(broker, kCompanionIoTimeoutMs);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    constexpr const char* socket_path =
        pathguard::provenance_protocol::kAndroidSocketPath;
    static_assert(sizeof(address.sun_path) > sizeof(
        pathguard::provenance_protocol::kAndroidSocketPath));
    memcpy(address.sun_path, socket_path, sizeof(
        pathguard::provenance_protocol::kAndroidSocketPath));
    pathguard::provenance_protocol::Response response;
    const bool handled = connect(
            broker, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0
        && send(broker, &request, sizeof(request), MSG_NOSIGNAL)
            == static_cast<ssize_t>(sizeof(request))
        && recv(broker, &response, sizeof(response), MSG_WAITALL)
            == static_cast<ssize_t>(sizeof(response))
        && response.magic == pathguard::provenance_protocol::kMagic
        && response.version == pathguard::provenance_protocol::kVersion;
    close(broker);
    return handled && send(client, &response, sizeof(response), MSG_NOSIGNAL)
        == static_cast<ssize_t>(sizeof(response));
}

void ReceiveRuntimeStatusSubmission(int client) {
    StatusSubmissionHeader header;
    int shared_fd = -1;
    int module_dir = -1;
    if (!ReceiveRuntimeStatusBootstrap(
            client, &header, &shared_fd, &module_dir)) {
        if (shared_fd >= 0) close(shared_fd);
        if (module_dir >= 0) close(module_dir);
        LOGE("provider runtime status bootstrap receive failed");
        return;
    }
    struct stat module_identity {};
    const bool header_valid = header.magic == kStatusSubmissionMagic
        && header.version == kStatusSubmissionVersion
        && header.pid > 0 && header.uid >= 10000
        && header.process_start_time != 0;
    const bool module_valid = header_valid
        && fstat(module_dir, &module_identity) == 0
        && S_ISDIR(module_identity.st_mode)
        && module_identity.st_uid == 0;
    SharedRuntimeStatus* shared = module_valid
        ? MapSharedRuntimeStatus(shared_fd) : nullptr;
    close(shared_fd);
    if (shared == nullptr) {
        close(module_dir);
        LOGE("provider runtime status bootstrap validation failed: pid=%d uid=%u",
             header.pid, header.uid);
        return;
    }
    const bool ready = WaitForRuntimeStatus(shared, kRuntimeStatusTimeoutMs);
    const uint32_t process_name_length = shared->process_name_length;
    const uint32_t status_length = shared->status_length;
    uint64_t current_start_time = 0;
    const bool payload_valid = ready
        && process_name_length > 0
        && process_name_length < kMaxProcessNameBytes
        && status_length > 0 && status_length <= kMaxRuntimeStatusBytes
        && shared->process_name[process_name_length] == '\0';
    const bool identity_valid = payload_valid
        && ReadProcessUid(header.pid, static_cast<uid_t>(header.uid))
        && ReadProcessStartTime(header.pid, &current_start_time)
        && current_start_time == header.process_start_time;
    const bool written = identity_valid && WriteRuntimeStatusText(
        module_dir, header.pid, shared->text, status_length);
    if (written) {
        LOGI("provider runtime status stored: pid=%d uid=%u process=%s bytes=%u",
             header.pid, header.uid, shared->process_name, status_length);
    } else {
        LOGE("provider runtime status rejected: pid=%d uid=%u ready=%d identity=%d",
             header.pid, header.uid, ready ? 1 : 0, identity_valid ? 1 : 0);
    }
    munmap(shared, sizeof(*shared));
    close(module_dir);
}

void CompanionHandler(int client) {
    LOGI("companion_enter pid=%d", getpid());
    const uint64_t handler_started = pathguard::perf::NowNs();
    SetSocketTimeout(client, kCompanionIoTimeoutMs);
    uint32_t request_magic = 0;
    if (recv(client, &request_magic, sizeof(request_magic), MSG_PEEK)
            == static_cast<ssize_t>(sizeof(request_magic))
        && request_magic == pathguard::provenance_protocol::kMagic) {
        ForwardProvenanceRequest(client);
        return;
    }
    if (request_magic == kStatusSubmissionMagic) {
        ReceiveRuntimeStatusSubmission(client);
        return;
    }
    BootstrapHeader header{};
    int shared_fd = -1;
    int module_dir_fd = -1;
    ProcessPlan plan;
    if (!ReceiveBootstrap(client, &header, &shared_fd, &module_dir_fd)
        || header.magic != kBootstrapMagic
        || header.version != kBootstrapVersion
        || header.pid <= 0
        || header.uid < 10000
        || header.rule_count == 0
        || header.rule_count > kMaxMountRules
        || header.process_name_length == 0
        || header.process_name_length >= kMaxProcessNameBytes
        || (header.feature_flags & ~kBootstrapFeatureProviderEnabled) != 0
        || (header.app_path_install.flags
            & ~(kPathInstallAvailable | kPathInstallAttempted
                | kPathInstallCommitted | kPathInstallActive
                | kPathIdentityAttempted | kPathIdentityHooks)) != 0
        || (header.policy_flags
            & ~pathguard::binary_format::kPolicyFlagAllowLegacyStringBind) != 0) {
        if (shared_fd >= 0) close(shared_fd);
        if (module_dir_fd >= 0) close(module_dir_fd);
        return;
    }
    SharedMountState* state = MapSharedState(shared_fd);
    close(shared_fd);
    if (state == nullptr) {
        close(module_dir_fd);
        return;
    }
    plan.count = header.rule_count;
    plan.policy_flags = header.policy_flags;
    plan.path_bytes = 1;
    plan.snapshot_generation = header.snapshot_generation;
    plan.plan_generation = header.plan_generation;
    plan.provider_enabled =
        (header.feature_flags & kBootstrapFeatureProviderEnabled) != 0;
    plan.app_path_status_available =
        (header.app_path_install.flags & kPathInstallAvailable) != 0;
    plan.app_path_install = DecodePathInstallTelemetry(header.app_path_install);
    if (!ReadFully(client, plan.process_name, header.process_name_length)) {
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }
    plan.process_name[header.process_name_length] = '\0';
    for (uint32_t index = 0; index < plan.count; ++index) {
        PlannedMount& mount = plan.mounts[index];
        uint32_t visible_length = 0;
        uint32_t backing_length = 0;
        char visible[PATH_MAX]{};
        char backing[PATH_MAX]{};
        if (!ReadFully(client, &mount.action, sizeof(mount.action))
            || !ReadFully(client, &visible_length, sizeof(visible_length))
            || visible_length == 0 || visible_length >= PATH_MAX
            || !ReadFully(client, visible, visible_length)
            || !ReadFully(client, &backing_length, sizeof(backing_length))
            || backing_length >= PATH_MAX
            || (backing_length > 0
                && !ReadFully(client, backing, backing_length))) {
            close(module_dir_fd);
            munmap(state, sizeof(*state));
            return;
        }
        visible[visible_length] = '\0';
        backing[backing_length] = '\0';
        if (!IsExecutableMountAction(mount.action)
            || (mount.action == kDenyAction ? backing_length != 0
                                            : backing_length == 0)
            || !IsAllowedTarget(visible)
            || (mount.action == kRedirectAction && !IsAllowedTarget(backing))
            || !StorePlanPath(&plan, visible, &mount.visible_path)
            || !StorePlanPath(&plan, backing, &mount.backing_path)) {
            close(module_dir_fd);
            munmap(state, sizeof(*state));
            return;
        }
    }

    ProcessPlan current_plan;
    if (!LoadProcessPlan(module_dir_fd, plan.process_name,
                         static_cast<jint>(header.uid), &current_plan, nullptr)
        || !SameProcessPlan(plan, current_plan)) {
        CompanionResult result;
        result.mount.result = ESTALE;
        result.mount.runtime_reason = static_cast<uint32_t>(
            pathguard::RuntimeReason::kPolicyChanged);
        PublishPreflightFailure(state, result);
        WriteRuntimeStatus(module_dir_fd, header.pid,
                           static_cast<uid_t>(header.uid), plan, result.mount,
                           LoadSharedStatus(state));
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }

    const uint64_t ready_started = pathguard::perf::NowNs();
    const ProcessReadyResult ready_result = WaitForProcessReady(
        header.pid, static_cast<uid_t>(header.uid), state);
    const uint64_t ready_ns = pathguard::perf::ElapsedNs(ready_started);
    if (ready_result != ProcessReadyResult::kReady) {
        CompanionResult result;
        result.ready_ns = ready_ns;
        result.mount.result = ready_result == ProcessReadyResult::kCancelled
            ? ECANCELED
            : ETIMEDOUT;
        const uint64_t send_started = pathguard::perf::NowNs();
        const bool sent = ready_result == ProcessReadyResult::kCancelled
            ? MarkSharedCancelled(state)
            : PublishPreflightFailure(state, result);
        LOGI("perf companion pid=%d rules=%u ready_ok=0 ready_us=%llu "
             "mount_us=0 result_send_us=%llu total_us=%llu result=%d sent=%d",
             header.pid, plan.count,
             static_cast<unsigned long long>(pathguard::perf::NsToUs(ready_ns)),
             static_cast<unsigned long long>(pathguard::perf::NsToUs(
                 pathguard::perf::ElapsedNs(send_started))),
             static_cast<unsigned long long>(pathguard::perf::NsToUs(
                 pathguard::perf::ElapsedNs(handler_started))),
             result.mount.result, sent ? 1 : 0);
        LOGE("target process readiness failed: pid=%d errno=%d",
             header.pid, result.mount.result);
        WriteRuntimeStatus(module_dir_fd, header.pid,
                           static_cast<uid_t>(header.uid), plan, result.mount,
                           LoadSharedStatus(state));
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }
    state->result.ready_ns = ready_ns;

    if (!BeginPreflight(state)) {
        MountPerfResult mount;
        mount.result = ECANCELED;
        WriteRuntimeStatus(module_dir_fd, header.pid,
                           static_cast<uid_t>(header.uid), plan, mount,
                           LoadSharedStatus(state));
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }

    pathguard::MountInfoSnapshot candidate_mounts;
    const uint64_t candidate_topology_started = pathguard::perf::NowNs();
    const int candidate_snapshot_error = pathguard::CaptureMountInfoSnapshot(
        &candidate_mounts);
    const bool candidate_topology_valid = candidate_snapshot_error == 0
        && CaptureStorageTopologyFromSnapshot(
            static_cast<uid_t>(header.uid), nullptr,
            candidate_mounts, &plan.topology);
    LOGI("perf topology_candidate pid=%d total_us=%llu read_us=%llu parse_us=%llu rows=%zu errno=%d",
         header.pid,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             pathguard::perf::ElapsedNs(candidate_topology_started))),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             candidate_mounts.read_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             candidate_mounts.parse_ns)),
         candidate_mounts.entry_count, candidate_snapshot_error);
    pathguard::DestroyMountInfoSnapshot(&candidate_mounts);
    if (!candidate_topology_valid) {
        CompanionResult result = state->result;
        result.mount.result = ENOTSUP;
        result.mount.runtime_reason = static_cast<uint32_t>(
            pathguard::RuntimeReason::kTopologyChanged);
        PublishPreflightFailure(state, result);
        LOGE("storage topology unsupported: pid=%d uid=%d", header.pid, header.uid);
        WriteRuntimeStatus(module_dir_fd, header.pid,
                           static_cast<uid_t>(header.uid), plan, result.mount,
                           LoadSharedStatus(state));
        close(module_dir_fd);
        munmap(state, sizeof(*state));
        return;
    }

    int result_sockets[2];
    MountPerfResult mount_result;
    bool mount_result_received = false;
    bool owner_death_tainted = false;
    struct stat target_namespace_identity {};
    char target_namespace_path[64]{};
    snprintf(target_namespace_path, sizeof(target_namespace_path),
             "/proc/%d/ns/mnt", header.pid);
    const bool namespace_identity_ready =
        stat(target_namespace_path, &target_namespace_identity) == 0;
    const uint64_t mount_started = pathguard::perf::NowNs();
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, result_sockets) == 0) {
        const pid_t child = fork();
        if (child == 0) {
            close(result_sockets[0]);
            const MountPerfResult child_result = ApplyProcessPlan(
                header.pid, static_cast<uid_t>(header.uid), module_dir_fd,
                plan, state);
            close(module_dir_fd);
            send(result_sockets[1], &child_result, sizeof(child_result), MSG_NOSIGNAL);
            close(result_sockets[1]);
            _exit(child_result.result == 0 ? 0 : 1);
        }
        close(result_sockets[1]);
        if (child > 0) {
            SetSocketTimeout(result_sockets[0], kCompanionIoTimeoutMs);
            mount_result_received = ReadFully(
                result_sockets[0], &mount_result, sizeof(mount_result));
            if (!mount_result_received) {
                const MountState current = LoadSharedStatus(state);
                if (current == MountState::kApplying
                    || current == MountState::kCancelRequested) {
                    kill(child, SIGKILL);
                    owner_death_tainted = true;
                    mount_result = {};
                    mount_result.result = EOWNERDEAD;
                    mount_result.runtime_reason = static_cast<uint32_t>(
                        pathguard::RuntimeReason::kOwnerDeath);
                } else {
                    kill(child, SIGTERM);
                    const timeval grace{0, kChildTerminateGraceMs * 1000};
                    setsockopt(result_sockets[0], SOL_SOCKET, SO_RCVTIMEO,
                               &grace, sizeof(grace));
                    mount_result_received = ReadFully(
                        result_sockets[0], &mount_result, sizeof(mount_result));
                    if (!mount_result_received) {
                        kill(child, SIGKILL);
                        mount_result = {};
                        mount_result.result = EIO;
                    }
                }
            }
            while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
            const MountState after_wait = LoadSharedStatus(state);
            if (owner_death_tainted
                && (after_wait == MountState::kApplying
                    || after_wait == MountState::kCancelRequested)) {
                CompanionResult result = state->result;
                result.mount = mount_result;
                PublishSharedResult(state, result, after_wait,
                                    MountState::kNamespaceTainted);
                NamespaceTerminationResult termination;
                if (namespace_identity_ready) {
                    termination = TerminateNamespaceMembers(
                        target_namespace_identity.st_dev,
                        target_namespace_identity.st_ino);
                }
                LOGE("mount worker owner death: pid=%d namespace_known=%d matched=%zu signaled=%zu remaining=%zu",
                     header.pid, namespace_identity_ready ? 1 : 0,
                     termination.matched, termination.signaled,
                     termination.remaining);
            }
        } else {
            mount_result = {};
            mount_result.result = errno;
        }
        close(result_sockets[0]);
    } else {
        mount_result = {};
        mount_result.result = errno;
    }
    const uint64_t mount_ns = pathguard::perf::ElapsedNs(mount_started);
    const MountState final_state = LoadSharedStatus(state);
    if (final_state == MountState::kPending
        || final_state == MountState::kPreflighting) {
        CompanionResult result = state->result;
        result.mount = mount_result;
        if (mount_result.result == 0) {
            result.mount.result = EPROTO;
        }
        PublishPreflightFailure(state, result);
    } else if (mount_result_received
               && final_state == MountState::kCancelRequested
               && mount_result.result != 0) {
        CompanionResult result = state->result;
        result.mount = mount_result;
        PublishSharedResult(state, result, MountState::kCancelRequested,
                            MountState::kRollbackComplete);
    } else if (mount_result_received && final_state == MountState::kApplying) {
        CompanionResult result = state->result;
        result.mount = mount_result;
        PublishSharedResult(
            state, result, MountState::kApplying,
            mount_result.result == 0 ? MountState::kComplete
                                     : MountState::kRollbackComplete);
    }

    WriteRuntimeStatus(module_dir_fd, header.pid, static_cast<uid_t>(header.uid),
                       plan, mount_result, LoadSharedStatus(state));

    LOGI("perf companion pid=%d rules=%u ready_ok=1 ready_us=%llu mount_us=%llu "
         "setns_us=%llu topology_us=%llu source_pin_us=%llu policy_us=%llu "
         "propagation_check_us=%llu propagation_us=%llu "
         "mount_total_us=%llu mount_max_us=%llu "
         "rollback_us=%llu mi_snapshots=%u mi_read_us=%llu mi_parse_us=%llu "
         "total_us=%llu result=%d committed=%d",
         header.pid, plan.count,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(ready_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.setns_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.topology_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.source_pin_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             mount_result.policy_revalidate_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             mount_result.propagation_check_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.propagation_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.mount_total_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.mount_max_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(mount_result.rollback_ns)),
         mount_result.mountinfo_snapshot_count,
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             mount_result.mountinfo_read_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             mount_result.mountinfo_parse_ns)),
         static_cast<unsigned long long>(pathguard::perf::NsToUs(
             pathguard::perf::ElapsedNs(handler_started))),
         mount_result.result,
         LoadSharedStatus(state) == MountState::kComplete ? 1 : 0);

    close(module_dir_fd);
    munmap(state, sizeof(*state));
    if (mount_result.result == 0) {
        LOGI("companion redirect mount applied: pid=%d rules=%u backend=%u",
             header.pid, plan.count, mount_result.backend);
    } else {
        LOGE("companion redirect mount failed: pid=%d errno=%d backend=%u reason=%u",
             header.pid, mount_result.result, mount_result.backend,
             mount_result.backend_reason);
    }
}

}  // namespace

REGISTER_ZYGISK_MODULE(PathGuardModule)
REGISTER_ZYGISK_COMPANION(CompanionHandler)
