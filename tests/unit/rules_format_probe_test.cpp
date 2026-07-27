#include <string>
#include <string_view>

#include "pathguard/rules/format_probe.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace {

using pathguard::rules::Diagnostic;
using pathguard::rules::FormatProbeResult;
using pathguard::rules::RulesLimits;
using pathguard::rules::SourceBuffer;

FormatProbeResult Probe(std::string bytes) {
    Diagnostic error;
    auto source = SourceBuffer::Create("rules.toml", std::move(bytes),
                                       RulesLimits{}, &error);
    assert(source.has_value());
    return pathguard::rules::ProbeRulesFormat(*source);
}

void ExpectError(std::string source, std::string_view code) {
    const auto result = Probe(std::move(source));
    assert(!result.ok());
    assert(result.diagnostic.code == code);
}

}  // namespace

int main() {
    assert(Probe("format = 1\n").version == 1);
    assert(Probe("\xEF\xBB\xBF" "  # lead\r\n\tformat\t=\t1 # v1\r\n").version == 1);
    assert(Probe("# first\n# second\nformat = 1").version == 1);

    ExpectError("", pathguard::rules::kFormatMissing);
    ExpectError("  # comment only\n", pathguard::rules::kFormatMissing);
    ExpectError("enabled = true\nformat = 1\n", pathguard::rules::kFormatInvalid);
    ExpectError("\"format\" = 1\n", pathguard::rules::kFormatInvalid);
    ExpectError("format.value = 1\n", pathguard::rules::kFormatInvalid);
    ExpectError("format = +1\n", pathguard::rules::kFormatInvalid);
    ExpectError("format = 01\n", pathguard::rules::kFormatInvalid);
    ExpectError("format = 1_0\n", pathguard::rules::kFormatInvalid);
    ExpectError("format = \"1\"\n", pathguard::rules::kFormatInvalid);
    ExpectError("format = 0\n", pathguard::rules::kFormatUnsupported);
    ExpectError("format = 2\n", pathguard::rules::kFormatUnsupported);

    // Probe intentionally stops after the first declaration.
    assert(Probe("format = 1\nformat = 2\n").version == 1);
    return 0;
}
