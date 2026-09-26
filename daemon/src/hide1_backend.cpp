#include "pathguard/hide1_backend.h"

#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>
#include "pathguard_hide1_uapi.h"
#endif

namespace pathguard::hide1 {
namespace {

namespace fs = std::filesystem;

Result Ok() { return {}; }
Result Error(ErrorCode code, std::string reason) {
    return Result{code, std::move(reason)};
}

void SetParseError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::size_t SkipJsonWhitespace(const std::string& json, std::size_t pos) {
    while (pos < json.size() && std::isspace(
               static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    return pos;
}

bool FindJsonField(const std::string& json, std::string_view key,
                  std::size_t* value_start) {
    const std::string needle = "\"" + std::string(key) + "\"";
    std::size_t search = 0;
    while ((search = json.find(needle, search)) != std::string::npos) {
        const std::size_t colon = SkipJsonWhitespace(json, search + needle.size());
        if (colon < json.size() && json[colon] == ':') {
            *value_start = SkipJsonWhitespace(json, colon + 1);
            return true;
        }
        search += needle.size();
    }
    return false;
}

bool ReadJsonStringField(const std::string& json, std::string_view key,
                         std::string* value) {
    std::size_t pos = 0;
    if (!FindJsonField(json, key, &pos) || pos >= json.size() || json[pos] != '"') {
        return false;
    }
    ++pos;
    std::string result;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') {
            *value = std::move(result);
            return true;
        }
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (pos >= json.size()) return false;
        const char escaped = json[pos++];
        switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: return false;
        }
    }
    return false;
}

template <typename T>
bool ReadJsonUnsignedField(const std::string& json, std::string_view key,
                           T* value) {
    std::size_t pos = 0;
    if (!FindJsonField(json, key, &pos) || pos >= json.size()
        || !std::isdigit(static_cast<unsigned char>(json[pos]))) {
        return false;
    }
    const char* begin = json.data() + pos;
    const char* end = json.data() + json.size();
    T parsed{};
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{}) return false;
    *value = parsed;
    return true;
}

bool ReadJsonBoolField(const std::string& json, std::string_view key,
                       bool* value) {
    std::size_t pos = 0;
    if (!FindJsonField(json, key, &pos)) return false;
    if (json.compare(pos, 4, "true") == 0) {
        *value = true;
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        *value = false;
        return true;
    }
    return false;
}

std::optional<Admission> ReadAdmissionJsonInternal(const std::string& json,
                                                   std::string* error) {
    const std::size_t content_begin = json.size() >= 3
            && static_cast<unsigned char>(json[0]) == 0xef
            && static_cast<unsigned char>(json[1]) == 0xbb
            && static_cast<unsigned char>(json[2]) == 0xbf
        ? 3 : 0;
    const std::size_t first = SkipJsonWhitespace(json, content_begin);
    const std::size_t last = json.empty() ? 0 : json.find_last_not_of(" \t\r\n");
    if (first >= json.size() || json[first] != '{' || last == std::string::npos
        || json[last] != '}') {
        SetParseError(error, "hide-admission-invalid-object");
        return std::nullopt;
    }
    Admission admission;
    std::string admission_state;
    std::string product_state;
    std::string regression_conclusion;
    std::uint64_t schema = 0;
    std::uint64_t status_generation = 0;
    std::uint64_t status_parent_inode = 0;
    std::uint64_t shadow_mode = 0;

    const auto require = [&](bool ok, const char* field) {
        if (!ok) SetParseError(error, std::string("hide-admission-invalid-") + field);
        return ok;
    };
    const bool admission_field_ok =
        ReadJsonStringField(json, "admission", &admission_state);
    if (!admission_field_ok || admission_state != "admitted") {
        SetParseError(error, "hide-admission-invalid-state-value=" + admission_state);
        return std::nullopt;
    }
    if (!require(ReadJsonUnsignedField(json, "schema", &schema) && schema >= 2,
                 "schema")
        || !require(ReadJsonStringField(json, "product_state", &product_state)
                    && product_state == "supported_scope_myron", "product-state")
        || !require(ReadJsonStringField(json, "device", &admission.device)
                    && admission.device == "myron", "device")
        || !require(ReadJsonStringField(json, "arch", &admission.arch)
                    && admission.arch == "aarch64", "arch")
        || !require(ReadJsonStringField(json, "fingerprint", &admission.fingerprint), "fingerprint")
        || !require(ReadJsonStringField(json, "kernel_release", &admission.kernel_release), "kernel")
        || !require(ReadJsonStringField(json, "kmi", &admission.kmi), "kmi")
        || !require(ReadJsonStringField(json, "module_sha256", &admission.module_sha256), "module-digest")
        || !require(ReadJsonUnsignedField(json, "undefined_symbol_count",
                                          &admission.undefined_symbol_count), "symbol-count")
        || !require(ReadJsonStringField(json, "undefined_symbols_sha256",
                                        &admission.undefined_symbols_sha256), "symbol-digest")
        || !require(ReadJsonStringField(json, "boot_id", &admission.boot_id), "boot-id")
        || !require(ReadJsonStringField(json, "regression_run_id",
                                        &admission.regression_run_id), "regression-run")
        || !require(ReadJsonStringField(json, "regression_conclusion",
                                        &regression_conclusion)
                    && regression_conclusion == "candidate_pass_requires_admission",
                    "regression")
        || !require(ReadJsonBoolField(json, "mountinfo_unchanged",
                                      &admission.mountinfo_unchanged)
                    && admission.mountinfo_unchanged, "mountinfo")
        || !require(ReadJsonBoolField(json, "ota_recheck_required",
                                      &admission.ota_recheck_required)
                    && admission.ota_recheck_required, "ota")
        || !require(ReadJsonUnsignedField(json, "status_generation",
                                          &status_generation) && status_generation != 0,
                    "generation")
        || !require(ReadJsonUnsignedField(json, "status_parent_inode",
                                          &status_parent_inode) && status_parent_inode != 0,
                    "parent-inode")
        || !require(ReadJsonUnsignedField(json, "status_shadow_mode", &shadow_mode)
                    && shadow_mode == 0, "shadow-mode")
        || !require(admission.module_sha256.size() == 64
                     && admission.undefined_symbols_sha256.size() == 64
                     && !admission.boot_id.empty()
                     && !admission.regression_run_id.empty(), "shape")) {
        return std::nullopt;
    }

    const auto is_hex = [](const std::string& value) {
        return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        });
    };
    if (!is_hex(admission.module_sha256)
        || !is_hex(admission.undefined_symbols_sha256)) {
        SetParseError(error, "hide-admission-invalid-digest");
        return std::nullopt;
    }

    admission.schema = static_cast<std::uint32_t>(schema);
    admission.status_generation = status_generation;
    admission.status_parent_inode = status_parent_inode;
    admission.status_shadow_mode = static_cast<std::int32_t>(shadow_mode);
    admission.regression_conclusion = std::move(regression_conclusion);
    admission.evidence_generation = status_generation;
    admission.admitted = true;
    return admission;
}

#if defined(__linux__)
struct NamespaceIoctlResult {
    std::int32_t ioctl_result = -1;
    std::int32_t error = 0;
    pathguard_hide1_status status{};
};

bool WriteAll(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    while (size != 0) {
        const ssize_t written = write(fd, bytes, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        bytes += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool ReadAll(int fd, void* data, std::size_t size) {
    auto* bytes = static_cast<unsigned char*>(data);
    while (size != 0) {
        const ssize_t count = read(fd, bytes, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        bytes += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}

bool RunNamespaceIoctl(int device_fd, int namespace_fd,
                       unsigned long request, void* argument,
                       NamespaceIoctlResult* result) {
    int pipe_fds[2]{};
    if (pipe2(pipe_fds, O_CLOEXEC) != 0) return false;
    const pid_t child = fork();
    if (child < 0) {
        const int saved = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        errno = saved;
        return false;
    }
    if (child == 0) {
        close(pipe_fds[0]);
        NamespaceIoctlResult child_result{};
        if (setns(namespace_fd, CLONE_NEWNS) != 0) {
            child_result.error = errno;
        } else {
            child_result.ioctl_result = ioctl(device_fd, request, argument);
            if (child_result.ioctl_result != 0) child_result.error = errno;
            if (request == PATHGUARD_HIDE1_IOC_STATUS
                && child_result.ioctl_result == 0) {
                child_result.status = *static_cast<pathguard_hide1_status*>(argument);
            }
        }
        const bool sent = WriteAll(pipe_fds[1], &child_result, sizeof(child_result));
        close(pipe_fds[1]);
        _exit(sent ? 0 : 1);
    }

    close(pipe_fds[1]);
    NamespaceIoctlResult child_result{};
    const bool received = ReadAll(pipe_fds[0], &child_result, sizeof(child_result));
    close(pipe_fds[0]);
    int wait_status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &wait_status, 0);
    } while (waited < 0 && errno == EINTR);
    if (!received || waited != child || !WIFEXITED(wait_status)
        || WEXITSTATUS(wait_status) != 0) {
        if (waited < 0) return false;
        errno = EIO;
        return false;
    }
    *result = child_result;
    return true;
}

class LinuxTransport final : public Transport {
public:
    LinuxTransport(std::string path, std::int32_t target_pid)
        : path_(std::move(path)), target_pid_(target_pid) {}

    Result Install(const RuleSet& rules) override {
        if (rules.empty() || rules.size() > kMaxRules) {
            return Error(ErrorCode::kUnsupportedRule, "hide-rule-count-out-of-range");
        }
        pathguard_hide1_rule_set request{};
        request.abi_version = PATHGUARD_HIDE1_ABI_VERSION;
        request.size = sizeof(request);
        request.rule_count = static_cast<__u32>(rules.size());
        request.target_uid = rules.front().target_uid;
        request.target_pid = rules.front().target_pid;
        request.expected_generation = rules.front().expected_generation;
        for (std::size_t index = 0; index < rules.size(); ++index) {
            const Rule& rule = rules[index];
            if (rule.target_uid != rules.front().target_uid
                || rule.target_pid != rules.front().target_pid
                || rule.expected_generation != rules.front().expected_generation
                || rule.parent.size() >= sizeof(request.rules[index].parent)
                || rule.basename.size() >= sizeof(request.rules[index].basename)) {
                return Error(ErrorCode::kUnsupportedRule, "hide-rule-identity-or-path-mismatch");
            }
            request.rules[index].abi_version = PATHGUARD_HIDE1_ABI_VERSION;
            request.rules[index].size = sizeof(request.rules[index]);
            request.rules[index].target_uid = rule.target_uid;
            request.rules[index].target_pid = rule.target_pid;
            request.rules[index].expected_generation = rule.expected_generation;
            std::memcpy(request.rules[index].parent, rule.parent.data(), rule.parent.size());
            std::memcpy(request.rules[index].basename, rule.basename.data(), rule.basename.size());
        }
        NamespaceIoctlResult result{};
        if (!RunIoctl(PATHGUARD_HIDE1_IOC_INSTALL_SET, &request, &result,
                      true)) {
            return Error(ErrorCode::kTransport,
                         "hide-namespace-ioctl-errno=" + std::to_string(errno));
        }
        return result.ioctl_result == 0 ? Ok() : Error(ErrorCode::kTransport,
            "hide-install-set-errno=" + std::to_string(result.error));
    }

    Result Enable(std::uint64_t generation) override {
        return SimpleIoctl(PATHGUARD_HIDE1_IOC_ENABLE, &generation,
                           "hide-enable", true);
    }
    Result Disable() override {
        return SimpleIoctl(PATHGUARD_HIDE1_IOC_DISABLE, nullptr,
                           "hide-disable", true);
    }
    Result Clear() override {
        return SimpleIoctl(PATHGUARD_HIDE1_IOC_CLEAR, nullptr,
                           "hide-clear", true);
    }
    std::optional<Status> StatusSnapshot() override {
        pathguard_hide1_status value{};
        NamespaceIoctlResult result{};
        const bool transported = RunIoctl(PATHGUARD_HIDE1_IOC_STATUS, &value,
                                          &result, true);
        if (!transported || result.ioctl_result != 0) return std::nullopt;
        value = result.status;
        Status status;
        status.abi_version = value.abi_version;
        status.state = value.state;
        status.lifecycle = value.lifecycle;
        status.last_error = value.last_error;
        status.target_uid = value.target_uid;
        status.target_pid = value.target_pid;
        status.target_mount_namespace = value.target_mnt_ns;
        status.generation = value.generation;
        return status;
    }

private:
    int Open() const { return open(path_.c_str(), O_RDWR | O_CLOEXEC); }

    Result SimpleIoctl(unsigned long request, void* argument,
                       const char* operation, bool enter_target_namespace) {
        NamespaceIoctlResult result{};
        const bool transported = RunIoctl(request, argument, &result,
                                          enter_target_namespace);
        if (!transported) {
            const int namespace_error = errno;
            const bool teardown = request == PATHGUARD_HIDE1_IOC_DISABLE
                || request == PATHGUARD_HIDE1_IOC_CLEAR;
            if (!teardown || !enter_target_namespace
                || (namespace_error != ENOENT && namespace_error != ESRCH)) {
                return Error(ErrorCode::kTransport,
                    std::string(operation) + "-namespace-ioctl-errno="
                        + std::to_string(namespace_error));
            }

            // The target mount namespace disappears with the target process.
            // Teardown must still be possible from the daemon namespace.
            const int device_fd = Open();
            if (device_fd < 0) return Error(ErrorCode::kTransport,
                std::string(operation) + "-fallback-open-errno="
                    + std::to_string(errno));
            result.ioctl_result = ioctl(device_fd, request, argument);
            result.error = result.ioctl_result == 0 ? 0 : errno;
            close(device_fd);
        }
        return result.ioctl_result == 0 ? Ok() : Error(ErrorCode::kTransport,
            std::string(operation) + "-errno=" + std::to_string(result.error));
    }

    bool RunIoctl(unsigned long request, void* argument,
                  NamespaceIoctlResult* result,
                  bool enter_target_namespace) const {
        if (!enter_target_namespace || target_pid_ <= 0) {
            const int device_fd = Open();
            if (device_fd < 0) return false;
            result->ioctl_result = ioctl(device_fd, request, argument);
            result->error = result->ioctl_result == 0 ? 0 : errno;
            if (request == PATHGUARD_HIDE1_IOC_STATUS
                && result->ioctl_result == 0) {
                result->status = *static_cast<pathguard_hide1_status*>(argument);
            }
            close(device_fd);
            return true;
        }
        const int device_fd = Open();
        if (device_fd < 0) return false;
        const std::string target = "/proc/" + std::to_string(target_pid_)
            + "/ns/mnt";
        const int target_namespace = open(target.c_str(), O_RDONLY | O_CLOEXEC);
        if (target_namespace < 0) {
            const int saved = errno;
            close(device_fd);
            errno = saved;
            return false;
        }
        const bool ok = RunNamespaceIoctl(device_fd, target_namespace, request,
                                          argument, result);
        const int saved = errno;
        close(device_fd);
        close(target_namespace);
        errno = saved;
        return ok;
    }

    std::string path_;
    std::int32_t target_pid_ = 0;
};
#endif

}  // namespace

std::optional<Admission> ReadAdmissionJson(const std::string& json,
                                           std::string* error) {
    return ReadAdmissionJsonInternal(json, error);
}

std::optional<Admission> ReadDeviceAdmissionConfig(
        const std::string& config, const DeviceProfile& runtime,
        std::string* error) {
    const std::size_t first = SkipJsonWhitespace(config, 0);
    const std::size_t last = config.empty()
        ? 0 : config.find_last_not_of(" \t\r\n");
    if (first >= config.size() || config[first] != '{'
        || last == std::string::npos || config[last] != '}') {
        SetParseError(error, "hide-device-profile-invalid-json");
        return std::nullopt;
    }

    std::uint64_t schema = 0;
    std::string capability;
    Admission admission;
    if (!ReadJsonUnsignedField(config, "schema", &schema) || schema != 1
        || !ReadJsonStringField(config, "capability", &capability)
        || capability != "hide-1.0-direct-vfs"
        || !ReadJsonStringField(config, "device", &admission.device)
        || !ReadJsonStringField(config, "arch", &admission.arch)
        || !ReadJsonStringField(config, "fingerprint", &admission.fingerprint)
        || !ReadJsonStringField(config, "kernel_release", &admission.kernel_release)
        || !ReadJsonStringField(config, "kmi", &admission.kmi)
        || !ReadJsonStringField(config, "module_sha256", &admission.module_sha256)) {
        SetParseError(error, "hide-device-profile-invalid-fields");
        return std::nullopt;
    }
    if (!runtime.module_live || runtime.boot_id.empty()
        || admission.device != runtime.device || admission.arch != runtime.arch
        || admission.fingerprint != runtime.fingerprint
        || admission.kernel_release != runtime.kernel_release
        || admission.kmi != runtime.kmi
        || admission.module_sha256 != runtime.module_sha256) {
        SetParseError(error, "hide-device-profile-mismatch");
        return std::nullopt;
    }

    admission.schema = static_cast<std::uint32_t>(schema);
    admission.boot_id = runtime.boot_id;
    admission.evidence_generation = 1;
    admission.admitted = true;
    admission.regression_conclusion = "device-profile-allowlisted";
    admission.mountinfo_unchanged = true;
    admission.ota_recheck_required = true;
    admission.status_generation = 1;
    admission.status_parent_inode = 1;
    admission.status_shadow_mode = 0;
    return admission;
}

TranslationResult TranslateRules(
        const std::vector<std::pair<std::string, std::string>>& paths,
        const Identity& identity, std::uint64_t generation) {
    if (identity.uid < 10000 || identity.pid <= 0 || identity.starttime == 0
        || identity.mount_namespace == 0 || generation == 0) {
        return {std::nullopt, Error(ErrorCode::kIdentityMismatch,
                                    "hide-identity-unconfirmed")};
    }
    if (paths.empty() || paths.size() > kMaxRules) {
        return {std::nullopt, Error(ErrorCode::kUnsupportedRule,
                                    "hide-rule-count-out-of-range")};
    }
    RuleSet result;
    result.reserve(paths.size());
    for (const auto& [parent, basename] : paths) {
        if (parent.empty() || parent.front() != '/' || basename.empty()
            || basename == "." || basename == ".."
            || basename.find('/') != std::string::npos
            || parent.size() >= kPathMax
            || basename.size() >= kNameMax) {
            return {std::nullopt, Error(ErrorCode::kUnsupportedRule,
                                        "hide-path-must-be-absolute-single-basename")};
        }
        result.push_back({identity.uid, identity.pid, generation, parent, basename});
    }
    return {std::move(result), Ok()};
}

Backend::Backend(std::unique_ptr<Transport> transport,
                 IdentityReader identity_reader)
    : transport_(std::move(transport)), identity_reader_(std::move(identity_reader)) {}

void Backend::Fail(ErrorCode code, std::string reason) {
    state_ = BackendState::kFailed;
    error_reason_ = std::move(reason);
    (void)code;
}

Result Backend::Reconcile() {
    if (!transport_) return Error(ErrorCode::kTransport, "hide-transport-missing");
    if (state_ == BackendState::kStopping) return Rollback();
    if (state_ != BackendState::kActive) return Ok();
    const auto identity = identity_reader_ ? identity_reader_() : std::nullopt;
    if (!identity || !bound_identity_ || *identity != *bound_identity_) {
        state_ = BackendState::kStopping;
        const Result rollback = Rollback();
        if (!rollback.ok()) {
            error_reason_ = "hide-target-stale-rollback-failed";
            return Error(rollback.error, error_reason_);
        }
        error_reason_ = "hide-target-stale-revoked";
        return Error(ErrorCode::kIdentityMismatch, error_reason_);
    }
    const Result status = ReadAndValidate(
        deployment_generation_, kStateActive, kLifecycleRunning);
    if (!status.ok()) {
        state_ = BackendState::kStopping;
        const Result rollback = Rollback();
        if (!rollback.ok()) return rollback;
        Fail(status.error, status.reason);
        return status;
    }
    return Ok();
}

Result Backend::Admit(const Admission& admission) {
    if (!admission.admitted || admission.evidence_generation == 0
        || admission.fingerprint.empty() || admission.kernel_release.empty()
        || admission.kmi.empty() || admission.module_sha256.empty()) {
        Fail(ErrorCode::kAdmissionMissing, "hide-admission-missing");
        return Error(ErrorCode::kAdmissionMissing, error_reason_);
    }
    admission_ = admission;
    state_ = BackendState::kInactive;
    error_reason_.clear();
    return Ok();
}

Result Backend::ReadAndValidate(std::uint64_t generation,
                                std::uint32_t expected_state,
                                std::uint32_t expected_lifecycle) {
    if (!transport_) return Error(ErrorCode::kTransport, "hide-transport-missing");
    const auto status = transport_->StatusSnapshot();
    if (!status || status->abi_version != kAbiVersion
        || status->state != expected_state
        || status->lifecycle != expected_lifecycle
        || status->generation != generation) {
        return Error(ErrorCode::kInvalidStatus, "hide-status-invalid");
    }
    if (bound_identity_) {
        if (status->target_uid != bound_identity_->uid
            || status->target_pid != bound_identity_->pid
            || status->target_mount_namespace != bound_identity_->mount_namespace) {
            return Error(ErrorCode::kIdentityMismatch, "hide-identity-mismatch");
        }
    }
    return Ok();
}

Result Backend::Rollback() {
    const Result disabled = transport_->Disable();
    if (!disabled.ok()) {
        state_ = BackendState::kStopping;
        error_reason_ = "hide-disable-failed:" + disabled.reason;
        return Error(ErrorCode::kRollbackFailed, error_reason_);
    }
    const Result cleared = transport_->Clear();
    if (!cleared.ok()) {
        state_ = BackendState::kStopping;
        error_reason_ = "hide-clear-failed:" + cleared.reason;
        return Error(ErrorCode::kRollbackFailed, error_reason_);
    }
    state_ = BackendState::kInactive;
    bound_identity_.reset();
    return Ok();
}

Result Backend::Apply(const RuleSet& rules,
                      const Admission& admission) {
    if (!admission.admitted || admission.evidence_generation == 0) {
        Fail(ErrorCode::kAdmissionMissing, "hide-admission-missing");
        return Error(ErrorCode::kAdmissionMissing, error_reason_);
    }
    if (state_ != BackendState::kInactive || admission.evidence_generation
            != admission_.evidence_generation) {
        return Error(ErrorCode::kAdmissionMissing, "hide-admission-stale");
    }
    const auto identity = identity_reader_ ? identity_reader_() : std::nullopt;
    if (!identity) {
        Fail(ErrorCode::kIdentityMismatch, "hide-namespace-unconfirmed");
        return Error(ErrorCode::kIdentityMismatch, error_reason_);
    }
    const std::uint64_t generation = ++deployment_generation_;
    RuleSet translated = rules;
    for (Rule& rule : translated) {
        rule.target_uid = identity->uid;
        rule.target_pid = identity->pid;
        rule.expected_generation = generation;
    }
    const TranslationResult validated = TranslateRules(
        [&]() {
            std::vector<std::pair<std::string, std::string>> paths;
            paths.reserve(translated.size());
            for (const Rule& rule : translated)
                paths.emplace_back(rule.parent, rule.basename);
            return paths;
        }(), *identity, generation);
    if (!validated.ok()) {
        Fail(validated.result.error, validated.result.reason);
        return validated.result;
    }
    state_ = BackendState::kInstalling;
    Result result = transport_->Install(*validated.rules);
    if (!result.ok()) {
        Fail(result.error, result.reason);
        return result;
    }
    result = ReadAndValidate(generation, kStateInactive, kLifecycleReady);
    if (!result.ok()) {
        const Result rollback = Rollback();
        if (!rollback.ok()) return rollback;
        Fail(result.error, result.reason);
        return result;
    }
    result = transport_->Enable(generation);
    if (!result.ok()) {
        const Result rollback = Rollback();
        if (!rollback.ok()) return rollback;
        Fail(result.error, result.reason);
        return result;
    }
    bound_identity_ = *identity;
    result = ReadAndValidate(generation, kStateActive, kLifecycleRunning);
    if (!result.ok()) {
        const Result rollback = Rollback();
        if (!rollback.ok()) return rollback;
        Fail(result.error, result.reason);
        return result;
    }
    state_ = BackendState::kActive;
    error_reason_.clear();
    return Ok();
}

Result Backend::Stop() {
    if (state_ != BackendState::kActive) return Ok();
    state_ = BackendState::kStopping;
    const auto identity = identity_reader_ ? identity_reader_() : std::nullopt;
    if (!identity || !bound_identity_ || *identity != *bound_identity_) {
        Fail(ErrorCode::kIdentityMismatch, "hide-target-stale");
        return Error(ErrorCode::kIdentityMismatch, error_reason_);
    }
    return Rollback();
}

Result Backend::Revoke() {
    if (state_ != BackendState::kActive
        && state_ != BackendState::kStopping) {
        return Ok();
    }
    state_ = BackendState::kStopping;
    return Rollback();
}

std::unique_ptr<Transport> MakeLinuxTransport(std::string device_path,
                                              std::int32_t target_pid) {
#if defined(__linux__)
    return std::make_unique<LinuxTransport>(std::move(device_path), target_pid);
#else
    (void)device_path;
    (void)target_pid;
    return nullptr;
#endif
}

std::optional<Identity> ReadProcessIdentity(std::int32_t pid,
                                             std::string proc_root) {
#if defined(__linux__)
    if (pid <= 0) return std::nullopt;
    const fs::path root(proc_root);
    const fs::path process = root / std::to_string(pid);
    std::ifstream status(process / "status");
    if (!status) return std::nullopt;
    std::string line;
    std::uint32_t uid = 0;
    bool got_uid = false;
    while (std::getline(status, line)) {
        if (line.rfind("Uid:", 0) != 0) continue;
        std::istringstream values(line.substr(4));
        unsigned long real_uid = 0;
        if (!(values >> real_uid) || real_uid > UINT32_MAX) return std::nullopt;
        uid = static_cast<std::uint32_t>(real_uid);
        got_uid = true;
        break;
    }
    if (!got_uid) return std::nullopt;

    std::ifstream stat_file(process / "stat");
    if (!stat_file) return std::nullopt;
    std::string stat_line;
    std::getline(stat_file, stat_line);
    const std::size_t close = stat_line.rfind(") ");
    if (close == std::string::npos) return std::nullopt;
    std::istringstream fields(stat_line.substr(close + 2));
    std::string field;
    std::uint64_t starttime = 0;
    for (int field_number = 3; fields >> field; ++field_number) {
        if (field_number != 22) continue;
        const auto parsed = std::from_chars(field.data(),
                                            field.data() + field.size(),
                                            starttime);
        if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size()) {
            return std::nullopt;
        }
        break;
    }
    if (starttime == 0) return std::nullopt;

    struct stat mount_stat {};
    if (::stat((process / "ns/mnt").c_str(), &mount_stat) != 0
        || mount_stat.st_ino == 0) return std::nullopt;
    return Identity{uid, pid, starttime,
                    static_cast<std::uint64_t>(mount_stat.st_ino)};
#else
    (void)pid;
    (void)proc_root;
    return std::nullopt;
#endif
}

}  // namespace pathguard::hide1
