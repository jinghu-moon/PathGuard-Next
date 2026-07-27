#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pathguard/rules_control.h"
#include "test_assert.h"

namespace fs = std::filesystem;

namespace {

void Write(const fs::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(output);
}

std::vector<std::uint8_t> Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

pathguard::rules::DeviceSnapshot Device() {
    pathguard::rules::DeviceSnapshot device;
    device.mount.primitives = pathguard::kCapabilityOpenTreeMoveMount;
    device.mount.strict_actions = pathguard::kMountActionRedirect;
    device.provider_supported = true;
    device.topology_supported = true;
    return device;
}

}  // namespace

int main() {
    using namespace pathguard::control;
    using pathguard::rules::RulesLimits;
    const fs::path root = fs::temp_directory_path()
        / ("pathguard-rf8-generation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path config = root / "config";
    const fs::path run = root / "run";
    fs::create_directories(config);
    fs::create_directories(run);
    const fs::path rules = config / kRulesFileName;
    Write(rules,
          "format = 1\n[apps.\"com.example.app\"]\n"
          "users=[0]\nredirect=[\"A\" -> \"B\"]\n");
    Reconciler reconciler(config, run, RulesLimits{}, Device());
    const ReconcileResult initial = reconciler.Reconcile();
    assert(initial.ok() && initial.published);
    const auto generation = initial.state.active_content_generation;
    const auto epoch = initial.state.deployment_epoch;
    const std::vector<std::uint8_t> active = Read(run / "policy.bin");
    const auto write_time = fs::last_write_time(run / "policy.bin");
    std::string previous_digest = initial.state.source_digest;

    const std::vector<std::string> equivalents{
        "# comment\nformat=1\n[apps.\"com.example.app\"]\n"
        "users=[0]\nredirect=[\"A\" -> \"B\"]\n",
        "format = 1\napps.\"com.example.app\".redirect = [ 'A'->'B' ]\n"
        "apps.\"com.example.app\".users = [ 0 ]\n",
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect = [\n  \"A\"       ->       \"B\",\n]\nusers = [0]\n",
        "format = 1\r\n[apps.\"com.example.app\"]\r\n"
        "users=[0]\r\nredirect=[\"A\" -> \"B\"]\r\n",
    };
    for (const std::string& variant : equivalents) {
        Write(rules, variant);
        const ReconcileResult result = reconciler.Reconcile();
        assert(result.ok());
        assert(result.compiled);
        assert(result.unchanged);
        assert(!result.published);
        assert(result.state.source_digest != previous_digest);
        assert(result.state.active_content_generation == generation);
        assert(result.state.deployment_epoch == epoch);
        assert(Read(run / "policy.bin") == active);
        assert(fs::last_write_time(run / "policy.bin") == write_time);
        previous_digest = result.state.source_digest;
    }
    fs::remove_all(root);
    return 0;
}
