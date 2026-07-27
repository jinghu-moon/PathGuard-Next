#pragma once

#include <optional>
#include <vector>

#include "pathguard/rules/desugarer.h"
#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/document.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules_contract.h"

namespace pathguard::rules {

struct RulesCompileResult {
    std::optional<RulesDocument> document;
    OriginMap origins;
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return document.has_value() && diagnostics.empty(); }
};

RulesCompileResult DecodeDesugaredRules(const SourceBuffer& source,
                                        DesugarResult desugared,
                                        const RulesLimits& limits);
RulesCompileResult ParseRulesDocument(const SourceBuffer& source,
                                      const RulesLimits& limits);

}  // namespace pathguard::rules
