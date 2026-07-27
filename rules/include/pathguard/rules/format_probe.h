#pragma once

#include <cstdint>

#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/source.h"

namespace pathguard::rules {

struct FormatProbeResult {
    std::uint32_t version = 0;
    Diagnostic diagnostic;

    bool ok() const { return version != 0 && diagnostic.code.empty(); }
};

FormatProbeResult ProbeRulesFormat(const SourceBuffer& source);

}  // namespace pathguard::rules
