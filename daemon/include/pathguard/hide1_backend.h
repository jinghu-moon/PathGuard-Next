#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "pathguard/policy_v6.h"

namespace pathguard::hide1 {

inline constexpr std::uint32_t kAbiVersion = 8;
inline constexpr std::uint32_t kStateInactive = 1;
inline constexpr std::uint32_t kStateActive = 2;
inline constexpr std::uint32_t kLifecycleReady = 1;
inline constexpr std::uint32_t kLifecycleRunning = 2;

enum class BackendState : std::uint8_t {
    kUnsupported,
    kInactive,
    kAdmissionPending,
    kInstalling,
    kActive,
    kStopping,
    kFailed,
};

struct Identity {
    std::uint32_t uid = 0;
    std::int32_t pid = 0;
    std::uint64_t starttime = 0;
    std::uint64_t mount_namespace = 0;

    bool operator==(const Identity&) const = default;
};

struct Rule {
    std::uint32_t target_uid = 0;
    std::int32_t target_pid = 0;
    std::uint64_t expected_generation = 0;
    std::string parent;
    std::string basename;
};

struct Admission {
    bool admitted = false;
    std::uint32_t schema = 0;
    std::string device;
    std::string arch;
    std::string fingerprint;
    std::string kernel_release;
    std::string kmi;
    std::string module_sha256;
    std::uint32_t undefined_symbol_count = 0;
    std::string undefined_symbols_sha256;
    std::string boot_id;
    std::string regression_run_id;
    std::string regression_conclusion;
    bool mountinfo_unchanged = false;
    bool ota_recheck_required = false;
    std::uint64_t status_generation = 0;
    std::uint64_t status_parent_inode = 0;
    std::int32_t status_shadow_mode = -1;
    std::uint64_t evidence_generation = 0;
};

struct DeviceProfile {
    std::string device;
    std::string arch;
    std::string fingerprint;
    std::string kernel_release;
    std::string kmi;
    std::string module_sha256;
    std::string boot_id;
    bool module_live = false;
};

struct Status {
    std::uint32_t abi_version = 0;
    std::uint32_t state = 0;
    std::uint32_t lifecycle = 0;
    std::int32_t last_error = 0;
    std::uint32_t target_uid = 0;
    std::int32_t target_pid = 0;
    std::uint64_t target_mount_namespace = 0;
    std::uint64_t generation = 0;
};

enum class ErrorCode : std::uint8_t {
    kNone,
    kAdmissionMissing,
    kUnsupportedRule,
    kIdentityMismatch,
    kGenerationMismatch,
    kTransport,
    kInvalidStatus,
    kRollbackFailed,
};

struct Result {
    ErrorCode error = ErrorCode::kNone;
    std::string reason;

    bool ok() const { return error == ErrorCode::kNone; }
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual Result Install(const Rule& rule) = 0;
    virtual Result Enable(std::uint64_t generation) = 0;
    virtual Result Disable() = 0;
    virtual Result Clear() = 0;
    virtual std::optional<Status> StatusSnapshot() = 0;
};

struct TranslationResult {
    std::optional<Rule> rule;
    Result result;

    bool ok() const { return rule.has_value() && result.ok(); }
};

TranslationResult TranslateRule(const pathguard::PolicyV6& policy,
                                const Identity& identity,
                                std::uint64_t generation);

// Parses the device-side admission artifact emitted by admit_hide1.ps1.
// boot-state is intentionally not accepted by this API.
std::optional<Admission> ReadAdmissionJson(const std::string& json,
                                           std::string* error = nullptr);

// Validates the package-owned fixed-device profile against live boot data.
std::optional<Admission> ReadDeviceAdmissionConfig(
    const std::string& config, const DeviceProfile& runtime,
    std::string* error = nullptr);

class Backend {
public:
    using IdentityReader = std::function<std::optional<Identity>()>;

    Backend(std::unique_ptr<Transport> transport,
            IdentityReader identity_reader);

    Result Admit(const Admission& admission);
    Result Apply(const pathguard::PolicyV6& policy,
                 const Admission& admission);
    Result Stop();
    Result Revoke();
    Result Reconcile();

    BackendState state() const { return state_; }
    std::uint64_t deployment_generation() const { return deployment_generation_; }
    const std::string& error_reason() const { return error_reason_; }

private:
    Result ReadAndValidate(std::uint64_t generation,
                           std::uint32_t expected_state,
                           std::uint32_t expected_lifecycle);
    Result Rollback();
    void Fail(ErrorCode code, std::string reason);

    std::unique_ptr<Transport> transport_;
    IdentityReader identity_reader_;
    Admission admission_;
    BackendState state_ = BackendState::kUnsupported;
    std::uint64_t deployment_generation_ = 0;
    std::optional<Identity> bound_identity_;
    std::string error_reason_;
};

std::unique_ptr<Transport> MakeLinuxTransport(
    std::string device_path = "/dev/pathguard_hide1",
    std::int32_t target_pid = 0);

std::optional<Identity> ReadProcessIdentity(
    std::int32_t pid, std::string proc_root = "/proc");

}  // namespace pathguard::hide1
