#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "pathguard/binary.h"
#include "pathguard/capabilities.h"
#include "pathguard/mount_backend.h"
#include "pathguard/policy_format.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "policy_v5_golden_fixture.h"
#include "test_assert.h"

namespace {

using pathguard::rules::Diagnostic;
using pathguard::rules::RulesBuildResult;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

SourceBuffer MakeSource(std::string bytes) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(bytes),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return std::move(*source);
}

RulesBuildResult Compile(std::string bytes) {
    SourceBuffer source = MakeSource(std::move(bytes));
    return pathguard::rules::CompileRules(source, RulesLimits{});
}

}  // namespace

int main() {
    using namespace pathguard;
    using namespace pathguard::rules;

    const std::string source =
        "format = 1\n"
        "[apps.\"org.localsend.localsend_app\"]\n"
        "users = [0]\nfile_picker = true\n"
        "redirect = [\"Download/localsend-source\" -> "
        "\"Download/localsend-redirect\"]\n";
    const RulesBuildResult built = Compile(source);
    assert(built.ok());
    const PolicyV5GoldenFixture golden = LoadPolicyV5GoldenFixture();
    assert(built.blob->bytes.size() == 207);
    assert(built.blob->bytes.size() == golden.expected_file_size);
    assert(built.blob->content_generation == golden.expected_content_generation);
    assert(built.blob->bytes.size() * 2 == golden.expected_hex.size());
    for (std::size_t index = 0; index < built.blob->bytes.size(); ++index) {
        const auto expected = static_cast<std::uint8_t>(std::stoul(
            golden.expected_hex.substr(index * 2, 2), nullptr, 16));
        assert(built.blob->bytes[index] == expected);
    }
    assert(VerifyPolicyBlob(*built.canonical, *built.blob));

    std::vector<std::uint8_t> corrupted = built.blob->bytes;
    corrupted[binary_format::kPayloadChecksumOffset] ^= 1U;
    assert(!VerifyPolicyBytes(corrupted, built.blob->content_generation));
    corrupted = built.blob->bytes;
    corrupted[binary_format::kPackageTableOffset] ^= 1U;
    assert(!VerifyPolicyBytes(corrupted, built.blob->content_generation));
    corrupted = built.blob->bytes;
    corrupted[binary_format::kContentGenerationOffset] ^= 1U;
    assert(!VerifyPolicyBytes(corrupted, built.blob->content_generation));

    const RulesBuildResult formatting_variant = Compile(
        "# comment\nformat=1\napps.\"org.localsend.localsend_app\".users=[0]\n"
        "apps.\"org.localsend.localsend_app\".redirect=["
        "'Download/localsend-source'->'Download/localsend-redirect']\n"
        "apps.\"org.localsend.localsend_app\".file_picker=true\n");
    assert(formatting_variant.ok());
    assert(formatting_variant.blob->bytes == built.blob->bytes);
    assert(formatting_variant.blob->content_generation
           == built.blob->content_generation);

    const RulesBuildResult ordered = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect = [\"A\" -> \"B\", \"C\" -> \"D\"]\n");
    const RulesBuildResult reversed = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect = [\"C\" -> \"D\", \"A\" -> \"B\"]\n");
    assert(ordered.ok() && reversed.ok());
    assert(ordered.blob->bytes == reversed.blob->bytes);

    const RulesBuildResult changed = Compile(
        "format = 1\n[apps.\"org.localsend.localsend_app\"]\n"
        "redirect = [\"Download/A\" -> \"Download/C\"]\n");
    assert(changed.ok());
    assert(changed.blob->content_generation != built.blob->content_generation);

    const RulesBuildResult deny = Compile(
        "format = 1\n[apps.\"org.localsend.localsend_app\"]\n"
        "users = [0]\n"
        "deny = [\"Pictures/Nagram\", \"DCIM/Screenshots\"]\n"
        "redirect = [\"Download/localsend-source\" -> "
        "\"Download/localsend-redirect\", "
        "\"Pictures\" -> \"Download/localsend-redirect\"]\n");
    assert(deny.ok());
    assert(deny.canonical->apps.front().deny.size() == 2);
    assert(deny.canonical->apps.front().redirects.size() == 2);
    assert(deny.requirements.mount_actions
           == (kMountActionRedirect | kMountActionDenyAnchor));
    PolicyDocument deny_document;
    std::uint64_t deny_generation = 0;
    ParseError deny_error;
    assert(DecodePolicy(deny.blob->bytes, &deny_document,
                        &deny_generation, &deny_error));
    assert(deny_document.apps.front().mounts.size() == 4);
    assert(std::count_if(
        deny_document.apps.front().mounts.begin(),
        deny_document.apps.front().mounts.end(),
        [](const LogicalMountRule& rule) {
            return rule.action == MountAction::kDeny
                && rule.backing_path.empty();
        }) == 2);

    DeviceSnapshot strict;
    strict.mount.primitives = kCapabilityOpenTreeMoveMount;
    strict.mount.strict_actions = kMountActionRedirect;
    strict.provider_supported = true;
    strict.topology_supported = true;
    strict.capability_generation = 7;
    strict.topology_generation = 9;
    const AdmissionResult admitted = AdmitPolicy(
        *built.canonical, built.requirements, strict);
    assert(admitted.admitted);
    assert(admitted.backend == MountBackendKind::kStrictOpenTree);
    assert(admitted.content_generation == built.blob->content_generation);
    assert(admitted.capability_generation == 7);
    assert(admitted.topology_generation == 9);

    assert(!AdmitPolicy(*deny.canonical, deny.requirements, strict).admitted);
    strict.mount.strict_actions |= kMountActionDenyAnchor;
    assert(AdmitPolicy(*deny.canonical, deny.requirements, strict).admitted);

    DeviceSnapshot missing_provider = strict;
    missing_provider.provider_supported = false;
    assert(!AdmitPolicy(*built.canonical, built.requirements,
                        missing_provider).admitted);
    DeviceSnapshot changed_topology = strict;
    changed_topology.topology_generation = 10;
    const AdmissionResult readmitted = AdmitPolicy(
        *built.canonical, built.requirements, changed_topology);
    assert(readmitted.admitted);
    assert(readmitted.content_generation == admitted.content_generation);
    assert(readmitted.topology_generation != admitted.topology_generation);

    DeviceSnapshot legacy = strict;
    legacy.mount.primitives = kCapabilityStringBindMount;
    legacy.mount.strict_actions = 0;
    legacy.mount.legacy_actions = kMountActionRedirect;
    assert(!AdmitPolicy(*built.canonical, built.requirements, legacy).admitted);
    const RulesBuildResult legacy_enabled = Compile(
        "format = 1\n[compatibility]\nallow_legacy_mount = true\n"
        "[apps.\"com.example.app\"]\nredirect = [\"A\" -> \"B\"]\n");
    assert(legacy_enabled.ok());
    const AdmissionResult legacy_admission = AdmitPolicy(
        *legacy_enabled.canonical, legacy_enabled.requirements, legacy);
    assert(legacy_admission.admitted);
    assert(legacy_admission.backend == MountBackendKind::kLegacyString);

    const RulesBuildResult disabled = Compile(
        "format = 1\n[apps.\"disabled.app\"]\nenabled = false\n"
        "redirect = [\"A\" -> \"B\"]\n"
        "[apps.\"enabled.app\"]\nredirect = [\"C\" -> \"D\"]\n");
    assert(disabled.ok());
    assert(disabled.resolved->apps.size() == 2);
    assert(disabled.canonical->apps.size() == 1);
    assert(disabled.canonical->apps.front().package == "enabled.app");

    const RulesBuildResult invalid = Compile(
        "format = 1\n[apps.\"com.example.app\"]\nredirect = [\"A\" ->]\n");
    assert(!invalid.ok());
    assert(!invalid.blob.has_value());
    assert(!invalid.canonical.has_value());
    return 0;
}
