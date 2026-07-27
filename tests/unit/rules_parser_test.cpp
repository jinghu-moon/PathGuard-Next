#include <algorithm>
#include <string>
#include <string_view>

#include "pathguard/rules/compiler.h"
#include "pathguard/rules/desugarer.h"
#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using pathguard::rules::Diagnostic;
using pathguard::rules::RulesCompileResult;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

SourceBuffer MakeSource(std::string bytes) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(bytes),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return std::move(*source);
}

bool HasCode(const RulesCompileResult& result, std::string_view code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const Diagnostic& value) {
                           return value.code == code;
                       });
}

RulesCompileResult Compile(std::string bytes) {
    SourceBuffer source = MakeSource(std::move(bytes));
    return pathguard::rules::ParseRulesDocument(source, RulesLimits{});
}

}  // namespace

int main() {
    using namespace pathguard::rules;

    const std::string valid =
        "format = 1\n"
        "[compatibility]\nallow_legacy_mount = true\n"
        "[apps.\"com.example.app\"]\n"
        "enabled = true\nusers = [0, 10]\n"
        "processes = [\"com.example.app\", \"com.example.app:worker\"]\n"
        "file_picker = true\n"
        "deny = [\"Pictures/Private\"]\n"
        "redirect = [\"Download/A\" -> \"Download/B\"]\n";
    const RulesCompileResult parsed = Compile(valid);
    assert(parsed.ok());
    assert(parsed.document->format == 1);
    assert(parsed.document->compatibility.allow_legacy_mount);
    assert(parsed.document->apps.size() == 1);
    const AppRules& app = parsed.document->apps.front();
    assert(app.package == "com.example.app" && app.enabled && app.file_picker);
    assert(app.users.size() == 2 && app.users[1] == 10);
    assert(app.processes.size() == 2);
    assert(app.deny.size() == 1 && app.redirects.size() == 1);
    const RuleOrigin* deny_origin = parsed.origins.Find(app.deny.front().id);
    const RuleOrigin* redirect_origin =
        parsed.origins.Find(app.redirects.front().id);
    assert(deny_origin != nullptr && redirect_origin != nullptr);
    assert(deny_origin->primary.size() > 0);
    assert(redirect_origin->source.size() > 0);
    assert(redirect_origin->target.size() > 0);

    const RulesCompileResult defaults = Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect = [\"A\" -> \"B\"]\n");
    assert(defaults.ok());
    assert(defaults.document->apps.front().enabled);
    assert(defaults.document->apps.front().users
           == std::vector<std::int32_t>{0});
    assert(defaults.document->apps.front().processes.empty());
    assert(!defaults.document->apps.front().file_picker);

    const RulesCompileResult unicode = Compile(
        "\xEF\xBB\xBF" "format = 1\r\n[apps.\"com.example.app\"]\r\n"
        "deny = [\"\xE5\xAE\xB6\xE5\xBA\xAD/\xE7\x85\xA7\xE7\x89\x87\"]\r\n");
    assert(unicode.ok());

    assert(HasCode(Compile(
        "format = 1\nformat = 1\n[apps.\"com.example.app\"]\n"
        "deny = [\"A\"]\n"), kTomlParse));
    assert(HasCode(Compile(
        "format = 1\nvalue = {\n  nested = true,\n}\n"), kTomlParse));
    assert(HasCode(Compile(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "deny = [\"bad\\q\"]\n"), kTomlParse));

    const SourceBuffer mapped_source = MakeSource(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect = [\"bad\\q\" -> \"B\"]\n");
    const DesugarResult mapped_desugared =
        DesugarRulesSource(mapped_source, RulesLimits{});
    assert(mapped_desugared.ok() && mapped_desugared.redirects.size() == 1);
    const ByteSpan mapped_operand =
        mapped_desugared.redirects.front().original_source;
    const RulesCompileResult mapped =
        ParseRulesDocument(mapped_source, RulesLimits{});
    assert(HasCode(mapped, kTomlParse));
    assert(mapped.diagnostics.front().primary.begin >= mapped_operand.begin);
    assert(mapped.diagnostics.front().primary.end <= mapped_operand.end);
    const std::string text = RenderDiagnosticText(mapped.diagnostics.front(),
                                                  mapped_source);
    const std::string json = RenderDiagnosticJson(mapped.diagnostics.front(),
                                                  mapped_source);
    assert(text.find("PG-TOML-PARSE") != std::string::npos);
    assert(json.find("PG-TOML-PARSE") != std::string::npos);
    assert(text.find("<desugared>") == std::string::npos);
    assert(json.find("<desugared>") == std::string::npos);

    DesugarResult corrupted = DesugarRulesSource(
        MakeSource("format = 1\n[apps.\"com.example.app\"]\n"
                   "redirect = [\"A\" -> \"B\"]\n"), RulesLimits{});
    SourceBuffer corrupted_source = MakeSource(
        "format = 1\n[apps.\"com.example.app\"]\n"
        "redirect = [\"A\" -> \"B\"]\n");
    assert(corrupted.ok() && corrupted.generated_storage);
    const ByteSpan expected_rule = corrupted.redirects.front().original_rule;
    (*corrupted.generated_storage)[corrupted.redirects.front()
        .generated_table.begin + 7] = '?';
    const RulesCompileResult synthetic = DecodeDesugaredRules(
        corrupted_source, std::move(corrupted), RulesLimits{});
    assert(HasCode(synthetic, kTomlParse));
    assert(synthetic.diagnostics.front().primary == expected_rule);
    return 0;
}
