#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/rules_control.h"
#include "test_assert.h"

namespace fs = std::filesystem;

namespace {

void Write(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    assert(output);
}

std::vector<std::uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string ValidRules(std::string_view target = "B") {
    return "format = 1\n[apps.\"com.example.app\"]\nredirect = [\"A\" -> \""
        + std::string(target) + "\"]\n";
}

}  // namespace

int main() {
    using namespace pathguard;
    using namespace pathguard::control;
    using namespace pathguard::rules;
    const fs::path root = fs::temp_directory_path()
        / ("pathguard-rf7-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path config = root / "config";
    const fs::path run = root / "run";
    fs::create_directories(config);
    fs::create_directories(run);
    Write(config / kRulesFileName, ValidRules());

    Write(config / "editor-save.tmp", ValidRules("Rename"));
    fs::remove(config / kRulesFileName);
    fs::rename(config / "editor-save.tmp", config / kRulesFileName);
    assert(LoadRulesSource(config, RulesLimits{}).ok());
    Write(config / kRulesFileName,
          std::string("\xef\xbb\xbf", 3) + ValidRules("Bom"));
    assert(LoadRulesSource(config, RulesLimits{}).ok());
    std::string crlf = ValidRules("Crlf");
    for (std::size_t at = 0; (at = crlf.find('\n', at)) != std::string::npos;
         at += 2) {
        crlf.replace(at, 1, "\r\n");
    }
    Write(config / kRulesFileName, crlf);
    assert(LoadRulesSource(config, RulesLimits{}).ok());
    Write(config / kRulesFileName, ValidRules());

    const SourceLoadResult loaded = LoadRulesSource(config, RulesLimits{});
    assert(loaded.ok());
    assert(loaded.snapshot->digest.starts_with("sha256:"));
    assert(loaded.snapshot->digest.size() == 71);
    assert(loaded.snapshot->digest
           == "sha256:20e65d69ad95558e2e0b81160d5ee204fc2d5a9e4d0d60f43e7103f2c9179f54");
    assert(loaded.snapshot->source.bytes() == ValidRules());
    RulesLimits tiny_source;
    tiny_source.max_source_bytes = 8;
    assert(!LoadRulesSource(config, tiny_source).ok());

    std::error_code symlink_error;
    fs::create_symlink(config / kRulesFileName, config / "rules-link.toml",
                       symlink_error);
    if (!symlink_error) {
        fs::rename(config / kRulesFileName, config / "rules-real.toml");
        fs::rename(config / "rules-link.toml", config / kRulesFileName);
        assert(!LoadRulesSource(config, RulesLimits{}).ok());
        fs::remove(config / kRulesFileName);
        fs::rename(config / "rules-real.toml", config / kRulesFileName);
    }

    LoadOptions unstable;
    unstable.after_read = [](const fs::path& path) { Write(path, ValidRules("C")); };
    assert(!LoadRulesSource(config, RulesLimits{}, unstable).ok());
    Write(config / kRulesFileName, ValidRules());

    fs::create_directory(config / "not-a-file");
    fs::rename(config / kRulesFileName, config / "saved.toml");
    fs::rename(config / "not-a-file", config / kRulesFileName);
    assert(!LoadRulesSource(config, RulesLimits{}).ok());
    fs::remove(config / kRulesFileName);
    fs::rename(config / "saved.toml", config / kRulesFileName);
#if !defined(_WIN32)
    fs::permissions(config / kRulesFileName, fs::perms::others_write,
                    fs::perm_options::add);
    assert(!LoadRulesSource(config, RulesLimits{}).ok());
    fs::permissions(config / kRulesFileName, fs::perms::others_write,
                    fs::perm_options::remove);
#endif
    Write(config / kRulesFileName,
          std::string("format = 1\n") + "\xef\xbb\xbf" + "x = 1\n");
    assert(!LoadRulesSource(config, RulesLimits{}).ok());
    Write(config / kRulesFileName, ValidRules());

    const RulesBuildResult first = CompileRules(
        LoadRulesSource(config, RulesLimits{}).snapshot->source, RulesLimits{});
    assert(first.ok());
    Publisher publisher(run);
    PublishResult published = publisher.Publish(*first.blob);
    assert(published.published);
    const std::vector<std::uint8_t> active = ReadBytes(run / "policy.bin");
    assert(publisher.Publish(*first.blob).unchanged);

    Write(config / kRulesFileName, ValidRules("C"));
    const RulesBuildResult second = CompileRules(
        LoadRulesSource(config, RulesLimits{}).snapshot->source, RulesLimits{});
    assert(second.ok());
    for (const PublishFault fault : AllPublishFaults()) {
        PublishOptions options;
        options.fail_at = fault;
        const PublishResult failed = publisher.Publish(*second.blob, options);
        assert(!failed.ok());
        assert(ReadBytes(run / "policy.bin") == active);
    }
    assert(publisher.Publish(*second.blob).published);
    assert(ReadBytes(run / "policy.bin") == second.blob->bytes);

    ControlState state;
    state.source_digest = loaded.snapshot->digest;
    state.candidate_sequence = 7;
    state.active_content_generation = second.blob->content_generation;
    state.deployment_epoch = 3;
    state.capability_generation = 11;
    state.topology_generation = 13;
    state.status = ControlStatus::kSourceInvalid;
    state.message = "new configuration was not activated; previous policy remains active";
    const std::string text = RenderControlStatusText(state);
    const std::string json = RenderControlStatusJson(state);
    for (const std::string_view field : {
             "source_digest", "candidate_sequence", "active_content_generation",
             "deployment_epoch", "capability_generation", "topology_generation",
             "source_invalid"}) {
        assert(text.find(field) != std::string::npos);
        assert(json.find(field) != std::string::npos);
    }
    assert(WriteControlStatus(run, state));
    assert(fs::is_regular_file(run / "rules-status.txt"));
    assert(fs::is_regular_file(run / "rules-status.json"));

    DeviceSnapshot device;
    device.mount.primitives = kCapabilityOpenTreeMoveMount;
    device.mount.strict_actions = kMountActionRedirect;
    device.provider_supported = true;
    device.topology_supported = true;
    device.capability_generation = 21;
    device.topology_generation = 22;

    const fs::path late_config = root / "late-config";
    const fs::path late_run = root / "late-run";
    fs::create_directories(late_config);
    fs::create_directories(late_run);
    Write(late_config / kRulesFileName, ValidRules("LateTopology"));
    DeviceSnapshot topology_pending = device;
    topology_pending.topology_supported = false;
    topology_pending.topology_generation = 0;
    Reconciler late_reconciler(
        late_config, late_run, RulesLimits{}, topology_pending);
    const ReconcileResult pending = late_reconciler.Reconcile();
    assert(!pending.ok());
    assert(pending.state.status == ControlStatus::kEnvironmentUnsupported);
    assert(!fs::exists(late_run / "policy.bin"));
    late_reconciler.SetDeviceSnapshot(device);
    const ReconcileResult topology_ready = late_reconciler.Reconcile();
    assert(topology_ready.ok() && topology_ready.published);
    assert(topology_ready.state.status == ControlStatus::kActive);
    assert(fs::is_regular_file(late_run / "policy.bin"));

    Reconciler reconciler(config, run, RulesLimits{}, device);
    ReconcileResult initial = reconciler.Reconcile();
    assert(initial.ok());
    assert(initial.state.status == ControlStatus::kActive);
    const auto epoch = initial.state.deployment_epoch;
    const ReconcileResult no_change = reconciler.Reconcile();
    assert(no_change.ok());
    assert(!no_change.compiled);
    assert(no_change.unchanged);
    assert(no_change.state.candidate_sequence
           == initial.state.candidate_sequence);
    Write(config / kRulesFileName, "format = 1\ninvalid = true\n");
    const ReconcileResult invalid = reconciler.Reconcile();
    assert(!invalid.ok());
    assert(invalid.state.status == ControlStatus::kSourceInvalid);
    assert(invalid.state.deployment_epoch == epoch);
    assert(ReadBytes(run / "policy.bin") == second.blob->bytes);

    Write(config / kRulesFileName,
          "format = 1\n[apps.\"com.example.app\"]\nusers=[0]\n"
          "file_picker=true\nredirect=[\"A\" -> \"C\"]\n");
    DeviceSnapshot unsupported = device;
    unsupported.provider_supported = false;
    reconciler.SetDeviceSnapshot(unsupported);
    const ReconcileResult environment = reconciler.Reconcile();
    assert(!environment.ok());
    assert(environment.state.status == ControlStatus::kEnvironmentUnsupported);
    assert(environment.state.deployment_epoch == epoch);

    unsupported.provider_supported = true;
    ++unsupported.capability_generation;
    reconciler.SetDeviceSnapshot(unsupported);
    ReconcileResult readmitted = reconciler.Reconcile();
    assert(readmitted.ok());
    assert(readmitted.state.status == ControlStatus::kActive);
    Write(config / kRulesFileName,
          "format = 1\n[apps.\"com.example.app\"]\n"
          "redirect=[\"A\" -> \"PublishFailure\"]\n");
    PublishOptions publish_failure;
    publish_failure.fail_at = PublishFault::kWrite;
    const ReconcileResult failed_publish = reconciler.Reconcile(publish_failure);
    assert(!failed_publish.ok());
    assert(failed_publish.state.status == ControlStatus::kPublishFailed);
    assert(failed_publish.state.deployment_epoch
           == readmitted.state.deployment_epoch);

    Write(config / kRulesFileName, ValidRules("Restart"));
    const ReconcileResult before_restart = reconciler.Reconcile();
    assert(before_restart.ok() && before_restart.published);
    Reconciler restarted(config, run, RulesLimits{}, unsupported);
    assert(restarted.state().active_content_generation
           == before_restart.state.active_content_generation);
    assert(restarted.state().deployment_epoch
           == before_restart.state.deployment_epoch);
    const ReconcileResult recovered = restarted.Reconcile();
    assert(recovered.ok() && recovered.unchanged && !recovered.published);
    assert(recovered.state.deployment_epoch
           == before_restart.state.deployment_epoch);

    Write(run / "rules-status.txt",
          "active_content_generation: 1\ndeployment_epoch: 99\n");
    Reconciler stale_status(config, run, RulesLimits{}, unsupported);
    assert(stale_status.state().active_content_generation
           == before_restart.state.active_content_generation);
    assert(stale_status.state().deployment_epoch == 1);

    std::vector<std::uint8_t> corrupt = ReadBytes(run / "policy.bin");
    corrupt[0] ^= 1U;
    {
        std::ofstream output(run / "policy.bin",
                             std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(corrupt.data()),
                     static_cast<std::streamsize>(corrupt.size()));
    }
    Reconciler corrupt_policy(config, run, RulesLimits{}, unsupported);
    assert(corrupt_policy.state().active_content_generation == 0);
    assert(corrupt_policy.state().deployment_epoch == 0);

    fs::remove_all(root);
    return 0;
}
