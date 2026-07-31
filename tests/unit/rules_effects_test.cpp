#include <string>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    using namespace pathguard::rules;
    Diagnostic source_error;
    auto source = SourceBuffer::Create("rules.toml", R"(format = 2
[apps."com.example.effects"]
users = [0]
observe_rules = [
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" } },
]
export_rules = [
  { select = { root = "Pictures", glob = "**/*.png", type = "file" }, to = "Download/export", mode = "move", media_scan = true },
]
)", RulesLimits{}, &source_error);
    assert(source.has_value());
    const RulesBuildResult result = CompileRules(*source, RulesLimits{});
    assert(result.ok() && result.policy_v6.has_value());
    const auto& actions = result.policy_v6->packages.front().actions;
    assert(actions.size() == 2);
    bool observe = false;
    bool export_rule = false;
    for (const auto& action : actions) {
        assert(action.domain == PolicyExecutionDomain::kEvent);
        assert(action.required_operations == kOperationCloseWriteEvent);
        if (action.kind == PolicyActionKind::kObserve) observe = true;
        if (action.kind == PolicyActionKind::kExport) {
            export_rule = true;
            assert(action.target == "Download/export");
            assert((action.options & 3U) == 1U);
            assert((action.options & 4U) != 0);
        }
    }
    assert(observe && export_rule);
    return 0;
}
