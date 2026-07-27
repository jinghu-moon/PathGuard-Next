#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pathguard/rules/semantic.h"

namespace pathguard::control {

inline constexpr char kRulesFileName[] = "rules.toml";

struct SourceSnapshot {
    rules::SourceBuffer source;
    std::string digest;
};

struct LoadOptions {
    void (*after_read)(const std::filesystem::path& path) = nullptr;
};

struct SourceLoadResult {
    std::optional<SourceSnapshot> snapshot;
    std::string error_code;
    std::string message;

    bool ok() const { return snapshot.has_value(); }
};

SourceLoadResult LoadRulesSource(const std::filesystem::path& config_directory,
                                 const rules::RulesLimits& limits,
                                 LoadOptions options = {});

enum class PublishFault : std::uint8_t {
    kNone,
    kCreate,
    kWrite,
    kSetMode,
    kSetOwner,
    kSetContext,
    kVerify,
    kFileSync,
    kRename,
    kDirectorySync,
};

constexpr std::array<PublishFault, 9> AllPublishFaults() {
    return {PublishFault::kCreate, PublishFault::kWrite,
            PublishFault::kSetMode, PublishFault::kSetOwner,
            PublishFault::kSetContext, PublishFault::kVerify,
            PublishFault::kFileSync, PublishFault::kRename,
            PublishFault::kDirectorySync};
}

struct PublishOptions {
    PublishFault fail_at = PublishFault::kNone;
};

struct PublishResult {
    bool published = false;
    bool unchanged = false;
    std::uint64_t content_generation = 0;
    std::string message;

    bool ok() const { return published || unchanged; }
};

class Publisher {
public:
    explicit Publisher(std::filesystem::path run_directory);
    PublishResult Publish(const rules::PolicyBlob& blob,
                          PublishOptions options = {}) const;

private:
    std::filesystem::path run_directory_;
};

enum class ControlStatus : std::uint8_t {
    kActive,
    kSourceInvalid,
    kEnvironmentUnsupported,
    kPublishFailed,
};

struct ControlState {
    std::string source_digest;
    std::uint64_t candidate_sequence = 0;
    std::uint64_t active_content_generation = 0;
    std::uint64_t deployment_epoch = 0;
    std::uint64_t capability_generation = 0;
    std::uint64_t topology_generation = 0;
    ControlStatus status = ControlStatus::kSourceInvalid;
    std::string error_code;
    std::string message;
};

std::string RenderControlStatusText(const ControlState& state);
std::string RenderControlStatusJson(const ControlState& state);
bool WriteControlStatus(const std::filesystem::path& run_directory,
                        const ControlState& state);

struct ReconcileResult {
    ControlState state;
    bool compiled = false;
    bool published = false;
    bool unchanged = false;

    bool ok() const { return state.status == ControlStatus::kActive; }
};

struct SaveRulesOptions {
    void (*before_commit)(const std::filesystem::path& path) = nullptr;
};

struct ManagerSaveResult {
    ReconcileResult reconcile;
    bool saved = false;
    std::string error_code;
    std::string message;

    bool ok() const { return saved && reconcile.ok(); }
};

class Reconciler {
public:
    Reconciler(std::filesystem::path config_directory,
               std::filesystem::path run_directory,
               rules::RulesLimits limits,
               rules::DeviceSnapshot snapshot);

    void SetDeviceSnapshot(rules::DeviceSnapshot snapshot);
    ReconcileResult Reconcile(PublishOptions options = {});
    ManagerSaveResult SaveRules(std::string_view expected_source_digest,
                                std::string replacement,
                                SaveRulesOptions options = {});
    const ControlState& state() const { return state_; }

private:
    std::filesystem::path config_directory_;
    std::filesystem::path run_directory_;
    rules::RulesLimits limits_;
    rules::DeviceSnapshot snapshot_;
    ControlState state_;
    bool device_dirty_ = false;
};

}  // namespace pathguard::control
