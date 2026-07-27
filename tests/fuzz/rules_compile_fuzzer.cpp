#include <cstddef>
#include <cstdint>
#include <string>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    using namespace pathguard::rules;
    RulesLimits limits;
    limits.max_source_bytes = 256 * 1024;
    Diagnostic source_error;
    auto source = SourceBuffer::Create(
        "fuzz.toml", std::string(reinterpret_cast<const char*>(data), size),
        limits, &source_error);
    if (!source.has_value()) return 0;
    const RulesBuildResult result = CompileRules(*source, limits);
    if (result.ok()
        && !VerifyPolicyBlob(*result.canonical, *result.blob)) {
        __builtin_trap();
    }
    if (!result.ok() && result.blob.has_value()) __builtin_trap();
    return 0;
}
