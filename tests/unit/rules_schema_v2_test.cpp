#include <algorithm>
#include <string>
#include <string_view>

#include "pathguard/rules/schema_v2.h"
#include "test_assert.h"

namespace {

using namespace pathguard::rules;

SourceBuffer MakeSource(std::string bytes) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules-v2.toml", std::move(bytes),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return std::move(*source);
}

RulesV2ParseResult Parse(std::string bytes) {
    SourceBuffer source = MakeSource(std::move(bytes));
    return ParseRulesDocumentV2(source, RulesLimits{});
}

void ExpectCode(std::string bytes, std::string_view code) {
    const auto result = Parse(std::move(bytes));
    assert(!result.ok());
    assert(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const Diagnostic& diagnostic) { return diagnostic.code == code; }));
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    const std::string valid = R"(format = 2
[apps."org.localsend.localsend_app"]
users = [0]
processes = ["*", "org.localsend.localsend_app:service"]
provider = { enabled = true }
deny_rules = [
  { select = { root = "Pictures", glob = "**/*.tmp", type = "file" }, priority = 100, enforcement = "provider" },
]
redirect_rules = [
  { select = { root = "Pictures", glob = "IMG_*.jpg", type = "file" }, to = "Download/images", priority = 7, preserve = "relative", collision = "reject" },
  { select = { root = "Download", glob = "literal.txt", type = "any" }, to = "Download/text" },
]
)";
    const auto parsed = Parse(valid);
    assert(parsed.ok());
    assert(parsed.document->format == 2);
    assert(parsed.document->apps.size() == 1);
    const AppRulesV2& app = parsed.document->apps.front();
    assert(app.provider.enabled);
    assert(app.actions.size() == 3);
    assert(app.actions[0].action == RuleActionKind::kDeny);
    assert(app.actions[0].priority == 100);
    assert(app.actions[0].enforcement == RuleEnforcement::kProvider);
    assert(app.actions[1].preserve == PreserveMode::kRelative);
    assert(app.actions[1].collision == CollisionPolicy::kReject);

    const auto built = BuildCanonicalPolicyV2(*parsed.document, RulesLimits{});
    assert(built.ok());
    assert(built.canonical->apps.front().actions.size() == 3);
    const auto& actions = built.canonical->apps.front().actions;
    assert(std::count_if(actions.begin(), actions.end(),
        [](const CanonicalActionV2& action) {
            return action.selector.source_kind == SelectorSourceKind::kLiteral;
        }) == 1);
    assert(std::count_if(actions.begin(), actions.end(),
        [](const CanonicalActionV2& action) {
            return action.selector.source_kind == SelectorSourceKind::kGlob;
        }) == 2);

    ExpectCode("format = 1\n[apps.\"com.example.app\"]\ndeny=[]\n",
               kFormatUnsupported);
    ExpectCode("format = 2\n[apps.\"com.example.app\"]\n"
               "deny_rules=[{select={root=\"Pictures\",glob=\"*.tmp\"},"
               "enforcement=\"sometimes\"}]\n", kInvalidValue);
    ExpectCode("format = 2\n[apps.\"com.example.app\"]\n"
               "deny_rules=[{select={root=\"Pictures\",glob=\"*.tmp\"}}]\n",
               kInvalidValue);
    ExpectCode("format = 2\n[apps.\"com.example.app\"]\n"
               "redirect_rules=[{select={root=\"Pictures\",glob=\"*.jpg\"},"
               "to=\"Download/x\",preserve=\"flat\"}]\n", kInvalidValue);
    ExpectCode("format = 2\n[apps.\"com.example.app\"]\n"
               "redirect_rules=[{select={root=\"Pictures\",glob=\"*.jpg\"},"
               "to=\"Download/x\",collision=\"replace\"}]\n", kInvalidValue);
    ExpectCode("format = 2\n[apps.\"com.example.app\"]\n"
               "provider={enabled=\"yes\"}\n", kTypeMismatch);

    const auto unsafe = Parse("format = 2\n[apps.\"com.example.app\"]\n"
        "redirect_rules=[{select={root=\"/Pictures\",glob=\"*.jpg\"},"
        "to=\"Download/x\"}]\n");
    assert(unsafe.ok());
    const auto unsafe_build = BuildCanonicalPolicyV2(
        *unsafe.document, RulesLimits{});
    assert(!unsafe_build.ok());
    assert(unsafe_build.diagnostics.front().code == kPathInvalid);
    return 0;
}
