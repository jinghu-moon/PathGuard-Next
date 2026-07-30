#include <string>

#include "pathguard/mount_plan_adapter.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    using namespace pathguard::rules;
    Diagnostic error;
    auto source = SourceBuffer::Create("mount.toml", R"(format = 2
[apps."com.example.app"]
users = [0]
deny_rules = [{ select = { root = "Pictures", glob = "Private", type = "directory" } }]
redirect_rules = [{ select = { root = "Download", glob = "Source", type = "directory" }, to = "Download/Target" }]
)", RulesLimits{}, &error);
    assert(source.has_value());
    const RulesBuildResult built = CompileRules(*source, RulesLimits{});
    assert(built.ok());
    policy_v6_view::PolicyV6View view;
    assert(view.Initialize(built.blob->bytes.data(), built.blob->bytes.size()));
    policy_v6_view::PackageRef package;
    assert(view.FindPackage("com.example.app", sizeof("com.example.app") - 1,
                            &package));
    LiteralMountPlan plan;
    assert(BuildLiteralMountPlan(view, package, &plan));
    assert(plan.count == 2);
    assert((plan.required_actions & kMountActionDenyAnchor) != 0);
    assert((plan.required_actions & kMountActionRedirect) != 0);
    assert(plan.content_generation == built.blob->content_generation);
    assert(plan.Path(plan.entries[0].visible_path) != nullptr);
    assert(plan.Path(plan.entries[1].target_path) != nullptr);
    return 0;
}
