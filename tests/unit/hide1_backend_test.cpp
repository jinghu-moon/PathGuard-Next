#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "pathguard/hide1_backend.h"

namespace {

using namespace pathguard;
using namespace pathguard::hide1;

RuleSet Rules(std::string parent, std::string basename = "hidden") {
    return {Rule{0, 0, 0, std::move(parent), std::move(basename)}};
}

Identity Target() {
    return Identity{10123, 4321, 991, 4026536012};
}

Admission AdmissionEvidence(std::uint64_t generation = 7) {
    Admission admission;
    admission.admitted = true;
    admission.fingerprint = "redmi/myron/production";
    admission.kernel_release = "6.12.23-android16-5-g16e473de48a3";
    admission.kmi = "android16-6.12";
    admission.module_sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    admission.evidence_generation = generation;
    return admission;
}

class FakeTransport final : public Transport {
public:
    Result Install(const RuleSet& rules) override {
        ++install_calls;
        if (fail_install) return {ErrorCode::kTransport, "install-failed"};
        installed = rules;
        status.abi_version = kAbiVersion;
        status.state = kStateInactive;
        status.lifecycle = kLifecycleReady;
        status.target_uid = rules.front().target_uid;
        status.target_pid = rules.front().target_pid;
        status.target_mount_namespace = 4026536012;
        status.generation = rules.front().expected_generation;
        return {};
    }
    Result Enable(std::uint64_t generation) override {
        ++enable_calls;
        if (fail_enable) return {ErrorCode::kTransport, "enable-failed"};
        if (!installed || status.generation != generation) {
            return {ErrorCode::kTransport, "enable-generation"};
        }
        status.state = kStateActive;
        status.lifecycle = kLifecycleRunning;
        return {};
    }
    Result Disable() override {
        ++disable_calls;
        if (fail_disable) return {ErrorCode::kTransport, "disable-failed"};
        status.state = kStateInactive;
        status.lifecycle = kLifecycleReady;
        return {};
    }
    Result Clear() override {
        ++clear_calls;
        if (fail_clear) return {ErrorCode::kTransport, "clear-failed"};
        installed.reset();
        status = {};
        return {};
    }
    std::optional<Status> StatusSnapshot() override {
        if (fail_status) return std::nullopt;
        return status;
    }

    bool fail_install = false;
    bool fail_enable = false;
    bool fail_disable = false;
    bool fail_clear = false;
    bool fail_status = false;
    int install_calls = 0;
    int enable_calls = 0;
    int disable_calls = 0;
    int clear_calls = 0;
    std::optional<RuleSet> installed;
    Status status;
};

void TranslationContract() {
    const auto accepted = TranslateRules(
        {{"/storage/emulated/0/Pictures", "hidden"}}, Target(), 1);
    assert(accepted.ok());
    assert(accepted.rules->size() == 1);
    assert(accepted.rules->front().parent == "/storage/emulated/0/Pictures");
    assert(accepted.rules->front().basename == "hidden");
    assert(accepted.rules->front().expected_generation == 1);

    assert(!TranslateRules({{"Pictures", "hidden"}}, Target(), 1).ok());
    assert(!TranslateRules({{"/storage/emulated/0/Pictures", "a/b"}}, Target(), 1).ok());
    const auto multiple = TranslateRules(
        {{"/storage/emulated/0/Pictures", "hidden"},
         {"/storage/emulated/0/DCIM", "private"}}, Target(), 1);
    assert(multiple.ok() && multiple.rules->size() == 2);
}

void BackendTransactionContract() {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    Identity current = Target();
    Backend backend(std::move(transport), [&]() -> std::optional<Identity> {
        return current;
    });

    assert(!backend.Admit({}).ok());
    assert(backend.state() == BackendState::kFailed);

    assert(backend.Admit(AdmissionEvidence()).ok());
    assert(backend.state() == BackendState::kInactive);
    assert(backend.Apply(Rules("/storage/emulated/0/Pictures"),
                         AdmissionEvidence()).ok());
    assert(backend.state() == BackendState::kActive);
    assert(fake->install_calls == 1 && fake->enable_calls == 1);
    assert(backend.deployment_generation() == 1);

    assert(backend.Stop().ok());
    assert(backend.state() == BackendState::kInactive);
    assert(fake->disable_calls == 1 && fake->clear_calls == 1);

    assert(backend.Apply(Rules("/storage/emulated/0/Pictures"),
                         AdmissionEvidence()).ok());
    assert(backend.deployment_generation() == 2);
    current.starttime++;
    assert(!backend.Reconcile().ok());
    assert(backend.state() == BackendState::kInactive);
    assert(fake->disable_calls == 2 && fake->clear_calls == 2);
}

void TeardownRetryContract() {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    Identity current = Target();
    Backend backend(std::move(transport), [&]() -> std::optional<Identity> {
        return current;
    });
    assert(backend.Admit(AdmissionEvidence()).ok());
    assert(backend.Apply(Rules("/storage/emulated/0/Pictures"),
                         AdmissionEvidence()).ok());

    fake->fail_disable = true;
    const Result first_revoke = backend.Revoke();
    assert(!first_revoke.ok());
    assert(backend.state() == BackendState::kStopping);
    assert(fake->disable_calls == 1 && fake->clear_calls == 0);

    fake->fail_disable = false;
    assert(backend.Reconcile().ok());
    assert(backend.state() == BackendState::kInactive);
    assert(fake->disable_calls == 2 && fake->clear_calls == 1);
}

void ClearRetryContract() {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    Backend backend(std::move(transport), [] {
        return std::optional<Identity>(Target());
    });
    assert(backend.Admit(AdmissionEvidence()).ok());
    assert(backend.Apply(Rules("/storage/emulated/0/Pictures"),
                         AdmissionEvidence()).ok());

    fake->fail_clear = true;
    assert(!backend.Revoke().ok());
    assert(backend.state() == BackendState::kStopping);
    assert(fake->disable_calls == 1 && fake->clear_calls == 1);

    fake->fail_clear = false;
    assert(backend.Reconcile().ok());
    assert(backend.state() == BackendState::kInactive);
    assert(fake->disable_calls == 2 && fake->clear_calls == 2);
}

void ApplyRollbackRetryContract() {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->fail_enable = true;
    fake->fail_disable = true;
    Backend backend(std::move(transport), [] {
        return std::optional<Identity>(Target());
    });
    assert(backend.Admit(AdmissionEvidence()).ok());
    assert(!backend.Apply(Rules("/storage/emulated/0/Pictures"),
                          AdmissionEvidence()).ok());
    assert(backend.state() == BackendState::kStopping);
    assert(fake->disable_calls == 1 && fake->clear_calls == 0);

    fake->fail_disable = false;
    assert(backend.Reconcile().ok());
    assert(backend.state() == BackendState::kInactive);
    assert(fake->disable_calls == 2 && fake->clear_calls == 1);
}

void RollbackContract() {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->fail_enable = true;
    Backend backend(std::move(transport), [] {
        return std::optional<Identity>(Target());
    });
    assert(backend.Admit(AdmissionEvidence()).ok());
    assert(!backend.Apply(Rules("/storage/emulated/0/Pictures"),
                          AdmissionEvidence()).ok());
    assert(backend.state() == BackendState::kFailed);
    assert(fake->disable_calls == 1 && fake->clear_calls == 1);
}

void AdmissionEvidenceContract() {
    const std::string valid = R"json({
      "schema": 2,
      "device": "myron",
      "fingerprint": "Redmi/myron/myron:16/test:user/release-keys",
      "kernel_release": "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k",
      "boot_id": "boot-1",
      "arch": "aarch64",
      "kmi": "android16-6.12",
      "module_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "undefined_symbol_count": 76,
      "undefined_symbols_sha256": "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
      "status_generation": 40001,
      "status_parent_inode": 848318,
      "status_shadow_mode": 0,
      "regression_run_id": "run-1",
      "regression_conclusion": "candidate_pass_requires_admission",
      "mountinfo_unchanged": true,
      "admission": "admitted",
      "product_state": "unsupported",
      "ota_recheck_required": true
    })json";
    std::string error;
    const auto admission = ReadAdmissionJson(valid, &error);
    assert(admission.has_value());
    assert(admission->admitted);
    assert(admission->evidence_generation == 40001);
    assert(admission->status_parent_inode == 848318);
    assert(admission->status_shadow_mode == 0);
    assert(admission->mountinfo_unchanged);
    assert(ReadAdmissionJson(std::string("\xef\xbb\xbf") + valid, &error)
           .has_value());

    const std::string boot_state_only =
        "phase=post-fs-data\nkernel=6.12.23\nfingerprint=test\n";
    assert(!ReadAdmissionJson(boot_state_only, &error).has_value());
    assert(!ReadAdmissionJson("{invalid-json", &error).has_value());

    std::string invalid = valid;
    const std::string marker = "\"admission\": \"admitted\"";
    const auto marker_pos = invalid.find(marker);
    assert(marker_pos != std::string::npos);
    invalid.replace(marker_pos, marker.size(),
                    "\"admission\": \"pending_hidelab\"");
    assert(!ReadAdmissionJson(invalid, &error).has_value());

    invalid = valid;
    invalid.replace(marker_pos, marker.size(),
                    "\"admission\": \"unsupported\"");
    assert(!ReadAdmissionJson(invalid, &error).has_value());
}

void FixedDeviceAdmissionContract() {
    const std::string profile = R"json({
      "schema": 1,
      "capability": "hide-1.0-direct-vfs",
      "device": "myron",
      "arch": "aarch64",
      "fingerprint": "Redmi/myron/myron:16/test:user/release-keys",
      "kernel_release": "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k",
      "kmi": "android16-6.12",
      "module_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    })json";
    DeviceProfile runtime{
        "myron", "aarch64", "Redmi/myron/myron:16/test:user/release-keys",
        "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k",
        "android16-6.12",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "boot-current", true};
    std::string error;
    auto admission = ReadDeviceAdmissionConfig(profile, runtime, &error);
    assert(admission && admission->admitted);
    assert(admission->boot_id == "boot-current");
    assert(admission->evidence_generation == 1);

    runtime.kernel_release += "-ota";
    assert(!ReadDeviceAdmissionConfig(profile, runtime, &error));
    runtime.kernel_release.pop_back();
    runtime.fingerprint += "-ota";
    assert(!ReadDeviceAdmissionConfig(profile, runtime, &error));
    runtime.fingerprint.pop_back();
    runtime.device = "other-device";
    assert(!ReadDeviceAdmissionConfig(profile, runtime, &error));
    runtime.device = "myron";
    runtime.arch = "arm64-v8a";
    assert(!ReadDeviceAdmissionConfig(profile, runtime, &error));
    runtime.arch = "aarch64";
    runtime.kmi = "android16-6.13";
    assert(!ReadDeviceAdmissionConfig(profile, runtime, &error));
    runtime.kmi = "android16-6.12";
    runtime.module_sha256[0] = 'f';
    assert(!ReadDeviceAdmissionConfig(profile, runtime, &error));
    runtime.module_sha256[0] = '0';
    runtime.module_live = false;
    assert(!ReadDeviceAdmissionConfig(profile, runtime, &error));
    assert(!ReadDeviceAdmissionConfig("{invalid-json", runtime, &error));
}

}  // namespace

int main() {
    TranslationContract();
    BackendTransactionContract();
    TeardownRetryContract();
    ClearRetryContract();
    ApplyRollbackRetryContract();
    RollbackContract();
    AdmissionEvidenceContract();
    FixedDeviceAdmissionContract();
    return 0;
}
