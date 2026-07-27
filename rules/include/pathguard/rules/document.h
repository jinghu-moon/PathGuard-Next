#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "pathguard/rules/diagnostic.h"

namespace pathguard::rules {

struct CompatibilityRules {
    bool allow_legacy_mount = false;
};

struct DenyRule {
    RuleId id = 0;
    std::string path;
};

struct RedirectRule {
    RuleId id = 0;
    std::string source;
    std::string target;
};

struct AppRules {
    std::string package;
    bool enabled = true;
    std::vector<std::int32_t> users{0};
    std::vector<std::string> processes;
    bool file_picker = false;
    std::vector<DenyRule> deny;
    std::vector<RedirectRule> redirects;
};

struct RulesDocument {
    std::uint32_t format = 1;
    CompatibilityRules compatibility;
    std::vector<AppRules> apps;
};

struct RuleOrigin {
    ByteSpan primary;
    ByteSpan source;
    ByteSpan arrow;
    ByteSpan target;
    bool has_redirect_operands = false;
};

class OriginMap {
public:
    bool Add(RuleId id, RuleOrigin origin);
    const RuleOrigin* Find(RuleId id) const;
    std::size_t size() const { return values_.size(); }

private:
    std::unordered_map<RuleId, RuleOrigin> values_;
};

}  // namespace pathguard::rules
