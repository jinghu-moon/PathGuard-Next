#include <string>

#include "pathguard/rules/schema_v2.h"
#include "pathguard/rules/selector_builder.h"
#include "test_assert.h"

namespace {

pathguard::rules::CanonicalPolicyV2 Compile(std::string text) {
    using namespace pathguard::rules;
    Diagnostic error;
    auto source = SourceBuffer::Create("selectors.toml", std::move(text),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    auto parsed = ParseRulesDocumentV2(*source, RulesLimits{});
    assert(parsed.ok());
    auto built = BuildCanonicalPolicyV2(*parsed.document, RulesLimits{});
    assert(built.ok());
    return std::move(*built.canonical);
}

}  // namespace

int main() {
    using namespace pathguard::rules;
    const auto policy = Compile(R"(format = 2
[apps."com.example.alpha"]
redirect_rules=[{select={root="Pictures",glob="[^abc]*.jpg"},to="Download/a"}]
[apps."com.example.beta"]
redirect_rules=[{select={root="Pictures",glob="[!abc]*.jpg"},to="Download/b"}]
)");
    const std::vector<PackageIdentityBinding> bindings{
        {"com.example.alpha", 10001, 0, true},
        {"com.example.beta", 10002, 0, true},
    };
    const auto first = BuildPatternPlan(policy, bindings);
    const auto second = BuildPatternPlan(policy, bindings);
    assert(first.ok() && second.ok());
    assert(first.plan->selectors.size() == 1);
    assert(first.plan->actions.size() == 2);
    assert(first.plan->packages.size() == 2);
    assert(first.plan->plan_generation == second.plan->plan_generation);
    assert(first.plan->selectors.front().fixed_extension == "jpg");
    assert(first.plan->actions[0].package_id
           != first.plan->actions[1].package_id);

    const auto literal = pathguard::pattern::CompilePattern("a");
    const auto character_class = pathguard::pattern::CompilePattern("[ab]");
    const auto one = pathguard::pattern::CompilePattern("?");
    const auto star = pathguard::pattern::CompilePattern("*");
    const auto globstar = pathguard::pattern::CompilePattern("**");
    assert(literal.ok() && character_class.ok() && one.ok() && star.ok()
           && globstar.ok());
    assert(literal.program->specificity > character_class.program->specificity);
    assert(character_class.program->specificity > one.program->specificity);
    assert(one.program->specificity > star.program->specificity);
    assert(star.program->specificity > globstar.program->specificity);
    return 0;
}
