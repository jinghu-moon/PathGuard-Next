#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "pathguard/rules_control.h"
#include "test_assert.h"

namespace fs = std::filesystem;

namespace {

fs::path g_race_path;

void Write(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    assert(output);
}

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string Rules(std::string_view target, bool provider = false) {
    return "format = 2\n[apps.\"com.example.app\"]\n"
        + std::string(provider ? "users=[0]\nprovider={enabled=true}\n" : "")
        + "redirect_rules=[{select={root=\"A\",glob=\"item\"},to=\""
        + std::string(target) + "\""
        + std::string(provider ? ",enforcement=\"provider\"" : "")
        + "}]\n";
}

void ReplaceDuringSave(const fs::path&) {
    Write(g_race_path, Rules("Concurrent"));
}

}  // namespace

int main() {
    using namespace pathguard;
    using namespace pathguard::control;
    using namespace pathguard::rules;

    const fs::path root = fs::temp_directory_path()
        / ("pathguard-rf9-manager-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path config = root / "config";
    const fs::path run = root / "run";
    fs::create_directories(config);
    fs::create_directories(run);
    const fs::path source_path = config / kRulesFileName;
    Write(source_path, Rules("Initial"));

    DeviceSnapshot device;
    device.mount.primitives = kCapabilityOpenTreeMoveMount;
    device.mount.strict_actions = kMountActionRedirect;
    device.provider_supported = true;
    device.topology_supported = true;
    device.capability_generation = 7;
    device.topology_generation = 9;
    Reconciler reconciler(config, run, RulesLimits{}, device);
    const ReconcileResult initial = reconciler.Reconcile();
    assert(initial.ok() && initial.published);
    const std::string digest = initial.state.source_digest;
    const auto generation = initial.state.active_content_generation;
    const auto epoch = initial.state.deployment_epoch;

    ManagerSaveResult stale = reconciler.SaveRules("sha256:stale", Rules("B"));
    assert(!stale.ok() && !stale.saved);
    assert(stale.error_code == "PG-SOURCE-CONFLICT");
    assert(Read(source_path) == Rules("Initial"));
    assert(reconciler.state().active_content_generation == generation);

    ManagerSaveResult invalid = reconciler.SaveRules(
        digest, "format = 2\ninvalid = true\n");
    assert(!invalid.ok() && !invalid.saved);
    assert(!invalid.error_code.empty());
    assert(Read(source_path) == Rules("Initial"));
    assert(reconciler.state().deployment_epoch == epoch);

    DeviceSnapshot unsupported = device;
    unsupported.provider_supported = false;
    ++unsupported.capability_generation;
    reconciler.SetDeviceSnapshot(unsupported);
    ManagerSaveResult deferred = reconciler.SaveRules(
        digest, Rules("Provider", true));
    assert(deferred.ok() && deferred.saved);
    assert(Read(source_path) == Rules("Provider", true));
    assert(deferred.reconcile.state.deployment_epoch == epoch + 1);
    const std::string provider_digest = deferred.reconcile.state.source_digest;

    reconciler.SetDeviceSnapshot(device);
    ManagerSaveResult saved = reconciler.SaveRules(provider_digest, Rules("Saved"));
    assert(saved.ok() && saved.saved && saved.reconcile.published);
    assert(Read(source_path) == Rules("Saved"));
    assert(saved.reconcile.state.source_digest != digest);
    assert(saved.reconcile.state.active_content_generation != generation);
    assert(saved.reconcile.state.deployment_epoch == epoch + 2);
    assert(saved.reconcile.state.capability_generation
           == device.capability_generation);
    assert(saved.reconcile.state.topology_generation
           == device.topology_generation);
    assert(fs::is_regular_file(run / "policy.bin"));

    ManagerSaveResult old_digest = reconciler.SaveRules(digest, Rules("Again"));
    assert(!old_digest.ok() && !old_digest.saved);
    assert(old_digest.error_code == "PG-SOURCE-CONFLICT");
    assert(Read(source_path) == Rules("Saved"));

    const std::string current_digest = saved.reconcile.state.source_digest;
    g_race_path = source_path;
    SaveRulesOptions race_options;
    race_options.before_commit = ReplaceDuringSave;
    ManagerSaveResult raced = reconciler.SaveRules(
        current_digest, Rules("LostUpdate"), race_options);
    assert(!raced.ok() && !raced.saved);
    assert(raced.error_code == "PG-SOURCE-CONFLICT");
    assert(Read(source_path) == Rules("Concurrent"));

    fs::remove_all(root);
    return 0;
}
