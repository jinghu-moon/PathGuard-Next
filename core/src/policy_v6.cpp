#include "pathguard/policy_v6.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "pathguard/policy_format.h"

namespace pathguard {
namespace {

using namespace binary_format;

void Put8(std::vector<std::uint8_t>* out, std::uint8_t value) {
    out->push_back(value);
}
void Put16(std::vector<std::uint8_t>* out, std::uint16_t value) {
    out->push_back(static_cast<std::uint8_t>(value));
    out->push_back(static_cast<std::uint8_t>(value >> 8));
}
void Put32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) Put8(out, static_cast<std::uint8_t>(value >> (i * 8)));
}
void Put64(std::vector<std::uint8_t>* out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) Put8(out, static_cast<std::uint8_t>(value >> (i * 8)));
}
void Store16(std::vector<std::uint8_t>* out, std::size_t at, std::uint16_t value) {
    (*out)[at] = static_cast<std::uint8_t>(value);
    (*out)[at + 1] = static_cast<std::uint8_t>(value >> 8);
}
void Store32(std::vector<std::uint8_t>* out, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) (*out)[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void Store64(std::vector<std::uint8_t>* out, std::size_t at, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) (*out)[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint16_t Read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0])
        | static_cast<std::uint16_t>(data[1]) << 8;
}
std::uint32_t Read32(const std::uint8_t* data) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[i]) << (i * 8);
    return value;
}
std::uint64_t Read64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(data[i]) << (i * 8);
    return value;
}

void PutBytes(std::vector<std::uint8_t>* out, std::string_view value) {
    Put32(out, static_cast<std::uint32_t>(value.size()));
    out->insert(out->end(), value.begin(), value.end());
}

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

bool IsValidUtf8(std::string_view value) {
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<std::uint8_t>(value[i]);
        std::size_t width = 0;
        std::uint32_t scalar = 0;
        if (first <= 0x7f) { width = 1; scalar = first; }
        else if ((first & 0xe0) == 0xc0) { width = 2; scalar = first & 0x1f; }
        else if ((first & 0xf0) == 0xe0) { width = 3; scalar = first & 0x0f; }
        else if ((first & 0xf8) == 0xf0) { width = 4; scalar = first & 0x07; }
        else return false;
        if (i + width > value.size()) return false;
        for (std::size_t j = 1; j < width; ++j) {
            const auto next = static_cast<std::uint8_t>(value[i + j]);
            if ((next & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if ((width == 2 && scalar < 0x80) || (width == 3 && scalar < 0x800)
            || (width == 4 && scalar < 0x10000) || scalar > 0x10ffff
            || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
        i += width;
    }
    return true;
}

std::string PatternKey(const pattern::PatternProgram& program) {
    return program.canonical;
}

std::string SelectorKey(const PolicySelectorV6& selector) {
    std::string key;
    key.append(selector.root).push_back('\0');
    key.push_back(static_cast<char>(selector.match_kind));
    key.push_back(static_cast<char>(selector.object_type));
    key.append(selector.match_kind == PolicyMatchKind::kGlob
                   ? PatternKey(selector.base_pattern) : std::string{});
    key.push_back('\0');
    for (const auto& except : selector.except_patterns) {
        key.append(PatternKey(except)).push_back('\0');
    }
    return key;
}

std::string ActionKey(const PolicyActionV6& action) {
    std::vector<std::uint8_t> bytes;
    Put32(&bytes, action.selector_index);
    Put8(&bytes, static_cast<std::uint8_t>(action.domain));
    Put8(&bytes, static_cast<std::uint8_t>(action.kind));
    Put32(&bytes, static_cast<std::uint32_t>(action.priority));
    PutBytes(&bytes, action.target);
    Put32(&bytes, action.options);
    Put64(&bytes, action.required_capabilities);
    Put64(&bytes, action.required_operations);
    Put8(&bytes, static_cast<std::uint8_t>(action.preserve));
    Put8(&bytes, static_cast<std::uint8_t>(action.collision));
    Put8(&bytes, static_cast<std::uint8_t>(action.reverse));
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::uint64_t RuleId(std::string_view package, const PolicySelectorV6& selector,
                     const PolicyActionV6& action) {
    std::vector<std::uint8_t> bytes{'P','G','R','L','6',0};
    PutBytes(&bytes, package);
    PutBytes(&bytes, SelectorKey(selector));
    PutBytes(&bytes, ActionKey(action));
    return Fnv1a64(bytes.data(), bytes.size());
}

bool ValidRoot(std::string_view root) {
    return !root.empty() && root.front() != '/' && root.back() != '/'
        && root.find("//") == std::string_view::npos
        && root.find('\0') == std::string_view::npos && IsValidUtf8(root);
}

bool ValidateProgram(const pattern::PatternProgram& program, std::string* error) {
    if (program.components.empty() || program.token_count > kMaxPatternTokens
        || program.canonical.empty()) return Fail(error, "invalid pattern program");
    std::uint32_t count = 0;
    for (const auto& component : program.components) {
        if (component.globstar) { ++count; continue; }
        if (component.tokens.empty()) return Fail(error, "empty pattern component");
        for (const auto& token : component.tokens) {
            ++count;
            if (token.kind == pattern::PatternTokenKind::kLiteral) {
                if (token.literal.empty() || token.literal.find('/') != std::string::npos
                    || !IsValidUtf8(token.literal)) return Fail(error, "invalid literal token");
            } else if (token.kind == pattern::PatternTokenKind::kCharacterClass) {
                if (token.character_class >= program.character_classes.size()) {
                    return Fail(error, "invalid character class reference");
                }
            }
        }
    }
    if (count != program.token_count) return Fail(error, "pattern token count mismatch");
    return true;
}

std::string FirstLiteralComponent(const pattern::PatternProgram& program) {
    if (program.components.empty() || program.components.front().globstar
        || program.components.front().tokens.empty()) return {};
    std::string result;
    for (const auto& token : program.components.front().tokens) {
        if (token.kind != pattern::PatternTokenKind::kLiteral) return {};
        result.append(token.literal);
    }
    return result;
}

std::string FixedExtension(const pattern::PatternProgram& program) {
    if (program.components.empty()) return {};
    const auto& component = program.components.back();
    if (component.globstar || component.tokens.empty()
        || component.tokens.back().kind != pattern::PatternTokenKind::kLiteral) return {};
    const std::string& literal = component.tokens.back().literal;
    const std::size_t dot = literal.rfind('.');
    if (dot == std::string::npos || dot + 1 == literal.size()) return {};
    return literal.substr(dot + 1);
}

std::uint16_t RootDepth(std::string_view root) {
    return static_cast<std::uint16_t>(1 + std::count(root.begin(), root.end(), '/'));
}

bool NormalizePolicy(const PolicyV6& input, PolicyV6* output, std::string* error) {
    if (input.packages.empty() || input.packages.size() > kMaxPackageCount) {
        return Fail(error, "invalid package count");
    }
    *output = input;
    for (auto& package : output->packages) {
        if (package.package.empty() || !IsValidUtf8(package.package)
            || package.package.find('\0') != std::string::npos) return Fail(error, "invalid package");
        std::sort(package.users.begin(), package.users.end());
        package.users.erase(std::unique(package.users.begin(), package.users.end()), package.users.end());
        std::sort(package.processes.begin(), package.processes.end());
        package.processes.erase(std::unique(package.processes.begin(), package.processes.end()), package.processes.end());
        if (package.users.size() > kMaxUsersPerPackage
            || package.processes.size() > kMaxProcessesPerPackage
            || (package.all_users && !package.users.empty())
            || (package.all_processes && !package.processes.empty())) return Fail(error, "invalid package scope");
        for (const auto& process : package.processes) {
            if (process.empty() || !IsValidUtf8(process)) return Fail(error, "invalid process scope");
        }
        if (package.selectors.empty() || package.selectors.size() > kMaxSelectorsPerPackage
            || package.actions.empty() || package.actions.size() > kMaxActionsPerPackage) {
            return Fail(error, "invalid package rule count");
        }
        std::vector<std::pair<std::uint32_t, PolicySelectorV6>> selectors;
        selectors.reserve(package.selectors.size());
        for (std::uint32_t i = 0; i < package.selectors.size(); ++i) {
            auto selector = package.selectors[i];
            if (!ValidRoot(selector.root)) return Fail(error, "invalid selector root");
            if (selector.match_kind == PolicyMatchKind::kLiteralPrefix) {
                if (!selector.except_patterns.empty()) return Fail(error, "literal selector has except");
                selector.base_pattern = {};
            } else {
                if (!ValidateProgram(selector.base_pattern, error)
                    || selector.except_patterns.size() > kMaxExceptPerSelector) return false;
                for (const auto& except : selector.except_patterns) {
                    if (!ValidateProgram(except, error)) return false;
                }
                std::sort(selector.except_patterns.begin(), selector.except_patterns.end(),
                    [](const auto& a, const auto& b) { return a.canonical < b.canonical; });
                selector.except_patterns.erase(std::unique(selector.except_patterns.begin(),
                    selector.except_patterns.end(), [](const auto& a, const auto& b) {
                        return a.canonical == b.canonical;
                    }), selector.except_patterns.end());
                for (const auto& except : selector.except_patterns) {
                    if (except.canonical == selector.base_pattern.canonical) {
                        return Fail(error, "except equals base pattern");
                    }
                }
            }
            selectors.emplace_back(i, std::move(selector));
        }
        std::sort(selectors.begin(), selectors.end(), [](const auto& a, const auto& b) {
            return SelectorKey(a.second) < SelectorKey(b.second);
        });
        for (std::size_t i = 1; i < selectors.size(); ++i) {
            if (SelectorKey(selectors[i - 1].second) == SelectorKey(selectors[i].second)) {
                return Fail(error, "duplicate selector");
            }
        }
        std::vector<std::uint32_t> remap(selectors.size());
        package.selectors.clear();
        for (std::uint32_t i = 0; i < selectors.size(); ++i) {
            remap[selectors[i].first] = i;
            package.selectors.push_back(std::move(selectors[i].second));
        }
        std::uint32_t except_count = 0;
        std::uint32_t token_count = 0;
        for (const auto& selector : package.selectors) {
            except_count += static_cast<std::uint32_t>(selector.except_patterns.size());
            if (selector.match_kind == PolicyMatchKind::kGlob) token_count += selector.base_pattern.token_count;
            for (const auto& except : selector.except_patterns) token_count += except.token_count;
        }
        if (except_count > kMaxExceptPerPackage || token_count > kMaxPatternTokensPerPackage) {
            return Fail(error, "package pattern budget exceeded");
        }
        std::set<std::uint64_t> rule_ids;
        for (auto& action : package.actions) {
            if (action.selector_index >= remap.size()) return Fail(error, "invalid action selector");
            action.selector_index = remap[action.selector_index];
            if ((action.required_capabilities & ~kKnownCapabilityMask) != 0
                || (action.required_operations & ~kKnownOperationMask) != 0) return Fail(error, "unknown requirement bit");
            const bool has_target = action.kind == PolicyActionKind::kRedirect
                || action.kind == PolicyActionKind::kExport;
            if (has_target != !action.target.empty() || (!action.target.empty() && !ValidRoot(action.target))) {
                return Fail(error, "invalid action target");
            }
            const bool path_action = action.kind == PolicyActionKind::kDeny
                || action.kind == PolicyActionKind::kRedirect;
            const bool event_action = action.kind == PolicyActionKind::kObserve
                || action.kind == PolicyActionKind::kExport;
            if ((action.domain == PolicyExecutionDomain::kMount
                 && (!path_action || package.selectors[action.selector_index].match_kind != PolicyMatchKind::kLiteralPrefix))
                || (action.domain == PolicyExecutionDomain::kEvent && !event_action)
                || (action.domain != PolicyExecutionDomain::kEvent && !path_action)) {
                return Fail(error, "invalid action domain");
            }
            action.rule_id = RuleId(package.package, package.selectors[action.selector_index], action);
            if (!rule_ids.insert(action.rule_id).second) return Fail(error, "rule id collision");
        }
        std::sort(package.actions.begin(), package.actions.end(), [](const auto& a, const auto& b) {
            return std::tuple(a.selector_index, a.domain, a.kind, -static_cast<std::int64_t>(a.priority),
                              a.target, a.options, a.required_capabilities, a.required_operations, a.rule_id)
                 < std::tuple(b.selector_index, b.domain, b.kind, -static_cast<std::int64_t>(b.priority),
                              b.target, b.options, b.required_capabilities, b.required_operations, b.rule_id);
        });
    }
    std::sort(output->packages.begin(), output->packages.end(), [](const auto& a, const auto& b) {
        return std::tuple(PackageNameHash(a.package.data(), a.package.size()), a.package)
             < std::tuple(PackageNameHash(b.package.data(), b.package.size()), b.package);
    });
    for (std::size_t i = 1; i < output->packages.size(); ++i) {
        if (output->packages[i - 1].package == output->packages[i].package) return Fail(error, "duplicate package");
    }
    for (auto& package : output->packages) {
        package.plan_generation = ComputePolicyV6PlanGeneration(package, output->allow_legacy_mount);
    }
    return true;
}

void AppendProgramSemantic(std::vector<std::uint8_t>* out,
                           const pattern::PatternProgram& program) {
    PutBytes(out, program.canonical);
}

void AppendPackageSemantic(std::vector<std::uint8_t>* out,
                           const PolicyPackageV6& package,
                           bool allow_legacy_mount) {
    out->insert(out->end(), {'P','G','P','L','6',0});
    Put16(out, kSchemaVersion);
    Put32(out, allow_legacy_mount ? kPolicyFlagAllowLegacyStringBind : 0);
    Put8(out, 0);
    PutBytes(out, package.package);
    std::uint32_t flags = (package.all_users ? kPackageFlagAllUsers : 0)
        | (package.all_processes ? kPackageFlagAllProcesses : 0)
        | (package.provider_enabled ? kPackageFlagProviderEnabled : 0);
    Put32(out, flags);
    Put32(out, static_cast<std::uint32_t>(package.users.size()));
    for (auto user : package.users) Put32(out, user);
    Put32(out, static_cast<std::uint32_t>(package.processes.size()));
    for (const auto& process : package.processes) PutBytes(out, process);
    Put32(out, static_cast<std::uint32_t>(package.selectors.size()));
    for (std::uint32_t i = 0; i < package.selectors.size(); ++i) {
        const auto& selector = package.selectors[i];
        PutBytes(out, selector.root);
        Put8(out, static_cast<std::uint8_t>(selector.match_kind));
        Put8(out, static_cast<std::uint8_t>(selector.object_type));
        if (selector.match_kind == PolicyMatchKind::kGlob) AppendProgramSemantic(out, selector.base_pattern);
        else Put32(out, 0);
        Put32(out, static_cast<std::uint32_t>(selector.except_patterns.size()));
        for (const auto& except : selector.except_patterns) AppendProgramSemantic(out, except);
        std::vector<const PolicyActionV6*> actions;
        for (const auto& action : package.actions) if (action.selector_index == i) actions.push_back(&action);
        Put32(out, static_cast<std::uint32_t>(actions.size()));
        for (const auto* action : actions) {
            Put64(out, action->rule_id);
            Put8(out, static_cast<std::uint8_t>(action->kind));
            Put8(out, static_cast<std::uint8_t>(action->domain));
            Put32(out, static_cast<std::uint32_t>(action->priority));
            PutBytes(out, action->target);
            Put8(out, static_cast<std::uint8_t>(action->preserve));
            Put8(out, static_cast<std::uint8_t>(action->collision));
            Put8(out, static_cast<std::uint8_t>(action->reverse));
            Put32(out, action->options);
            Put64(out, action->required_capabilities);
            Put64(out, action->required_operations);
        }
    }
}

struct PatternRecord {
    pattern::PatternProgram program;
    std::string first_literal;
    std::string extension;
};

}  // namespace

std::uint64_t ComputePolicyV6PlanGeneration(const PolicyPackageV6& package,
                                            bool allow_legacy_mount) {
    std::vector<std::uint8_t> bytes;
    AppendPackageSemantic(&bytes, package, allow_legacy_mount);
    return Fnv1a64(bytes.data(), bytes.size());
}

std::uint64_t ComputePolicyV6ContentGeneration(const PolicyV6& input) {
    PolicyV6 policy;
    if (!NormalizePolicy(input, &policy, nullptr)) return 0;
    std::vector<const PolicyPackageV6*> packages;
    for (const auto& package : policy.packages) packages.push_back(&package);
    std::sort(packages.begin(), packages.end(), [](const auto* a, const auto* b) {
        return a->package < b->package;
    });
    std::vector<std::uint8_t> bytes{'P','G','I','R','6',0};
    Put16(&bytes, kSchemaVersion);
    Put32(&bytes, policy.allow_legacy_mount ? kPolicyFlagAllowLegacyStringBind : 0);
    Put8(&bytes, 0);
    Put32(&bytes, static_cast<std::uint32_t>(packages.size()));
    for (const auto* package : packages) {
        std::vector<std::uint8_t> plan;
        AppendPackageSemantic(&plan, *package, policy.allow_legacy_mount);
        Put32(&bytes, static_cast<std::uint32_t>(plan.size()));
        bytes.insert(bytes.end(), plan.begin(), plan.end());
    }
    return Fnv1a64(bytes.data(), bytes.size());
}

bool EncodePolicyV6(const PolicyV6& input, std::vector<std::uint8_t>* output,
                    std::string* error) {
    if (output == nullptr) return Fail(error, "output is null");
    PolicyV6 policy;
    if (!NormalizePolicy(input, &policy, error)) return false;

    std::map<std::string, PatternRecord> pattern_map;
    std::set<std::tuple<bool, std::uint64_t, std::uint64_t>> class_set;
    std::set<std::string> strings{""};
    for (const auto& package : policy.packages) {
        strings.insert(package.package);
        strings.insert(package.processes.begin(), package.processes.end());
        for (const auto& selector : package.selectors) {
            strings.insert(selector.root);
            auto add_pattern = [&](const pattern::PatternProgram& program) {
                pattern_map.emplace(program.canonical, PatternRecord{
                    program, FirstLiteralComponent(program), FixedExtension(program)});
                const auto& record = pattern_map.at(program.canonical);
                if (!record.first_literal.empty()) strings.insert(record.first_literal);
                if (!record.extension.empty()) strings.insert(record.extension);
                for (const auto& component : program.components) for (const auto& token : component.tokens) {
                    if (token.kind == pattern::PatternTokenKind::kLiteral) strings.insert(token.literal);
                    if (token.kind == pattern::PatternTokenKind::kCharacterClass) {
                        const auto& cls = program.character_classes[token.character_class];
                        class_set.emplace(cls.negated, cls.bitmap[0], cls.bitmap[1]);
                    }
                }
            };
            if (selector.match_kind == PolicyMatchKind::kGlob) add_pattern(selector.base_pattern);
            for (const auto& except : selector.except_patterns) add_pattern(except);
        }
        for (const auto& action : package.actions) if (!action.target.empty()) strings.insert(action.target);
    }

    std::vector<std::string> string_values(strings.begin(), strings.end());
    std::unordered_map<std::string, std::uint32_t> string_ids;
    for (std::uint32_t i = 0; i < string_values.size(); ++i) string_ids.emplace(string_values[i], i);
    std::vector<PatternRecord> patterns;
    std::unordered_map<std::string, std::uint32_t> pattern_ids;
    for (const auto& [key, value] : pattern_map) {
        pattern_ids.emplace(key, static_cast<std::uint32_t>(patterns.size()));
        patterns.push_back(value);
    }
    std::vector<std::tuple<bool, std::uint64_t, std::uint64_t>> classes(class_set.begin(), class_set.end());
    std::map<std::tuple<bool, std::uint64_t, std::uint64_t>, std::uint32_t> class_ids;
    for (std::uint32_t i = 0; i < classes.size(); ++i) class_ids.emplace(classes[i], i);

    std::vector<std::uint8_t> package_rows, scope_rows, selector_rows, action_rows;
    std::vector<std::uint8_t> pattern_rows, token_rows, class_rows, except_rows;
    std::uint32_t first_scope = 0, first_selector = 0, first_action = 0, first_except = 0;
    for (const auto& package : policy.packages) {
        const std::size_t package_row = package_rows.size();
        package_rows.resize(package_row + kPackageSize, 0);
        Store32(&package_rows, package_row, PackageNameHash(package.package.data(), package.package.size()));
        Store32(&package_rows, package_row + 4, string_ids.at(package.package));
        Store32(&package_rows, package_row + 8, first_scope);
        Store16(&package_rows, package_row + 12, static_cast<std::uint16_t>(package.users.size()));
        Store16(&package_rows, package_row + 14, static_cast<std::uint16_t>(package.processes.size()));
        Store32(&package_rows, package_row + 16, first_selector);
        Store32(&package_rows, package_row + 20, static_cast<std::uint32_t>(package.selectors.size()));
        Store32(&package_rows, package_row + 24, first_action);
        Store32(&package_rows, package_row + 28, static_cast<std::uint32_t>(package.actions.size()));
        Store64(&package_rows, package_row + 32, package.plan_generation);
        std::uint64_t caps = 0, operations = 0;
        for (const auto& action : package.actions) { caps |= action.required_capabilities; operations |= action.required_operations; }
        Store64(&package_rows, package_row + 40, caps);
        Store64(&package_rows, package_row + 48, operations);
        Store32(&package_rows, package_row + 56,
            (package.all_users ? kPackageFlagAllUsers : 0)
            | (package.all_processes ? kPackageFlagAllProcesses : 0)
            | (package.provider_enabled ? kPackageFlagProviderEnabled : 0));
        for (auto user : package.users) {
            Put8(&scope_rows, 0); Put8(&scope_rows, 0); Put16(&scope_rows, 0); Put32(&scope_rows, user);
        }
        for (const auto& process : package.processes) {
            Put8(&scope_rows, 1); Put8(&scope_rows, 0); Put16(&scope_rows, 0); Put32(&scope_rows, string_ids.at(process));
        }
        for (std::uint32_t local_selector = 0; local_selector < package.selectors.size(); ++local_selector) {
            const auto& selector = package.selectors[local_selector];
            const std::size_t row = selector_rows.size();
            selector_rows.resize(row + kSelectorSize, 0);
            Store32(&selector_rows, row, string_ids.at(selector.root));
            const std::uint32_t base_id = selector.match_kind == PolicyMatchKind::kGlob
                ? pattern_ids.at(selector.base_pattern.canonical) : kInvalidId;
            Store32(&selector_rows, row + 4, base_id);
            Store32(&selector_rows, row + 8, first_except);
            Store32(&selector_rows, row + 12, first_action);
            Store16(&selector_rows, row + 16, static_cast<std::uint16_t>(selector.except_patterns.size()));
            const auto action_count = static_cast<std::uint16_t>(std::count_if(package.actions.begin(), package.actions.end(),
                [&](const auto& action) { return action.selector_index == local_selector; }));
            Store16(&selector_rows, row + 18, action_count);
            Store16(&selector_rows, row + 20, RootDepth(selector.root));
            selector_rows[row + 24] = static_cast<std::uint8_t>(selector.match_kind);
            selector_rows[row + 25] = static_cast<std::uint8_t>(selector.object_type);
            if (selector.match_kind == PolicyMatchKind::kGlob) {
                const auto& record = patterns[base_id];
                Store32(&selector_rows, row + 28, record.first_literal.empty() ? kInvalidId : string_ids.at(record.first_literal));
                Store32(&selector_rows, row + 32, record.extension.empty() ? kInvalidId : string_ids.at(record.extension));
            } else {
                Store32(&selector_rows, row + 28, kInvalidId);
                Store32(&selector_rows, row + 32, kInvalidId);
            }
            for (const auto& except : selector.except_patterns) {
                Put32(&except_rows, pattern_ids.at(except.canonical)); Put32(&except_rows, 0); ++first_except;
            }
            first_action += action_count;
        }
        for (const auto& action : package.actions) {
            Put32(&action_rows, first_selector + action.selector_index);
            Put32(&action_rows, action.target.empty() ? kInvalidId : string_ids.at(action.target));
            Put64(&action_rows, action.rule_id);
            Put64(&action_rows, action.required_capabilities);
            Put64(&action_rows, action.required_operations);
            Put32(&action_rows, static_cast<std::uint32_t>(action.priority));
            Put32(&action_rows, action.options);
            Put8(&action_rows, static_cast<std::uint8_t>(action.kind));
            Put8(&action_rows, static_cast<std::uint8_t>(action.domain));
            Put8(&action_rows, static_cast<std::uint8_t>(action.preserve));
            Put8(&action_rows, static_cast<std::uint8_t>(action.collision));
            Put8(&action_rows, static_cast<std::uint8_t>(action.reverse));
            Put8(&action_rows, 0); Put16(&action_rows, 0);
        }
        first_scope += static_cast<std::uint32_t>(package.users.size() + package.processes.size());
        first_selector += static_cast<std::uint32_t>(package.selectors.size());
    }
    std::uint32_t first_token = 0;
    for (const auto& record : patterns) {
        const std::size_t row = pattern_rows.size();
        pattern_rows.resize(row + kPatternSize, 0);
        Store32(&pattern_rows, row, first_token);
        std::uint16_t physical_tokens = 0;
        for (std::size_t ci = 0; ci < record.program.components.size(); ++ci) {
            if (ci != 0) { Put8(&token_rows, 5); Put8(&token_rows, 0); Put16(&token_rows, 0); Put32(&token_rows, 0); ++physical_tokens; }
            const auto& component = record.program.components[ci];
            if (component.globstar) {
                Put8(&token_rows, 3); Put8(&token_rows, 0); Put16(&token_rows, 0); Put32(&token_rows, 0); ++physical_tokens;
                continue;
            }
            for (const auto& token : component.tokens) {
                std::uint8_t kind = 0; std::uint32_t operand = 0;
                if (token.kind == pattern::PatternTokenKind::kLiteral) { kind = 0; operand = string_ids.at(token.literal); }
                else if (token.kind == pattern::PatternTokenKind::kStarComponent) kind = 1;
                else if (token.kind == pattern::PatternTokenKind::kOneComponentChar) kind = 2;
                else {
                    kind = 4;
                    const auto& cls = record.program.character_classes[token.character_class];
                    operand = class_ids.at({cls.negated, cls.bitmap[0], cls.bitmap[1]});
                }
                Put8(&token_rows, kind); Put8(&token_rows, 0); Put16(&token_rows, 0); Put32(&token_rows, operand); ++physical_tokens;
            }
        }
        Store16(&pattern_rows, row + 4, physical_tokens);
        Store16(&pattern_rows, row + 6, static_cast<std::uint16_t>(record.program.components.size()));
        Store32(&pattern_rows, row + 8, record.first_literal.empty() ? kInvalidId : string_ids.at(record.first_literal));
        Store32(&pattern_rows, row + 12, record.extension.empty() ? kInvalidId : string_ids.at(record.extension));
        Store16(&pattern_rows, row + 16, record.first_literal.empty() && record.extension.empty() ? kPatternFlagDegenerate : 0);
        first_token += physical_tokens;
    }
    for (const auto& [negated, low, high] : classes) {
        Put64(&class_rows, low); Put64(&class_rows, high);
        Put32(&class_rows, negated ? kCharacterClassFlagNegated : 0); Put32(&class_rows, 0);
    }
    std::vector<std::uint8_t> string_index, string_data;
    for (const auto& value : string_values) {
        Put32(&string_index, static_cast<std::uint32_t>(string_data.size()));
        Put32(&string_index, static_cast<std::uint32_t>(value.size()));
        string_data.insert(string_data.end(), value.begin(), value.end());
    }
    const std::array<std::size_t, 9> sizes{package_rows.size(), scope_rows.size(), selector_rows.size(), action_rows.size(),
        pattern_rows.size(), token_rows.size(), class_rows.size(), except_rows.size(), string_index.size()};
    std::array<std::uint32_t, 10> offsets{};
    offsets[0] = static_cast<std::uint32_t>(kHeaderSize);
    for (std::size_t i = 0; i < sizes.size(); ++i) offsets[i + 1] = offsets[i] + static_cast<std::uint32_t>(sizes[i]);
    const std::uint64_t file_size64 = static_cast<std::uint64_t>(offsets[9]) + string_data.size();
    if (file_size64 > kMaxPolicyFileSize || string_values.size() > kMaxStringCount
        || string_data.size() > kMaxStringBytes || patterns.size() > kMaxPatternCount
        || classes.size() > kMaxClassCount) return Fail(error, "policy file budget exceeded");
    output->assign(kHeaderSize, 0);
    Store32(output, 0, kMagic); Store16(output, 4, kFormatVersion); Store16(output, 6, kSchemaVersion);
    Store32(output, 8, static_cast<std::uint32_t>(kHeaderSize));
    Store32(output, 12, static_cast<std::uint32_t>(file_size64));
    Store32(output, 20, policy.allow_legacy_mount ? kPolicyFlagAllowLegacyStringBind : 0);
    Store64(output, 24, ComputePolicyV6ContentGeneration(policy));
    Store32(output, 32, static_cast<std::uint32_t>(policy.packages.size()));
    Store32(output, 36, static_cast<std::uint32_t>(scope_rows.size() / kScopeRefSize));
    Store32(output, 40, static_cast<std::uint32_t>(selector_rows.size() / kSelectorSize));
    Store32(output, 44, static_cast<std::uint32_t>(action_rows.size() / kActionSize));
    Store32(output, 48, static_cast<std::uint32_t>(patterns.size()));
    Store32(output, 52, static_cast<std::uint32_t>(token_rows.size() / kPatternTokenSize));
    Store32(output, 56, static_cast<std::uint32_t>(classes.size()));
    Store32(output, 60, static_cast<std::uint32_t>(except_rows.size() / kSelectorExceptRefSize));
    Store32(output, 64, static_cast<std::uint32_t>(string_values.size()));
    Store32(output, 68, static_cast<std::uint32_t>(string_data.size()));
    for (std::size_t i = 0; i < offsets.size(); ++i) Store32(output, 72 + i * 4, offsets[i]);
    (*output)[112] = 0; (*output)[113] = kOperationMaskVersion;
    for (const auto* rows : {&package_rows, &scope_rows, &selector_rows, &action_rows, &pattern_rows,
                             &token_rows, &class_rows, &except_rows, &string_index, &string_data}) {
        output->insert(output->end(), rows->begin(), rows->end());
    }
    Store32(output, 16, Crc32(output->data() + kHeaderSize, output->size() - kHeaderSize));
    return true;
}

PolicyV6DecodeResult DecodePolicyV6(const std::vector<std::uint8_t>& input,
                                    PolicyV6* output) {
    auto reject = [](std::string message) { return PolicyV6DecodeResult{false, std::move(message), 0}; };
    if (output == nullptr) return reject("output is null");
    if (input.size() < kHeaderSize || input.size() > kMaxPolicyFileSize) return reject("invalid file size");
    const auto* data = input.data();
    if (Read32(data) != kMagic || Read16(data + 4) != kFormatVersion || Read16(data + 6) != kSchemaVersion) return reject("policy version mismatch");
    if (Read32(data + 8) != kHeaderSize || Read32(data + 12) != input.size()
        || data[112] != 0 || data[113] != kOperationMaskVersion) return reject("invalid header");
    if (Crc32(data + kHeaderSize, input.size() - kHeaderSize) != Read32(data + 16)) return reject("payload checksum mismatch");
    const std::array<std::uint32_t, 9> counts{Read32(data + 32), Read32(data + 36), Read32(data + 40), Read32(data + 44),
        Read32(data + 48), Read32(data + 52), Read32(data + 56), Read32(data + 60), Read32(data + 64)};
    const std::array<std::uint32_t, 9> ceilings{kMaxPackageCount, kMaxScopeRefCount, kMaxSelectorCount, kMaxActionCount,
        kMaxPatternCount, kMaxTokenCount, kMaxClassCount, kMaxExceptRefCount, kMaxStringCount};
    const std::array<std::size_t, 9> row_sizes{kPackageSize, kScopeRefSize, kSelectorSize, kActionSize, kPatternSize,
        kPatternTokenSize, kCharacterClassSize, kSelectorExceptRefSize, kStringIndexSize};
    std::array<std::uint32_t, 10> offsets{};
    for (std::size_t i = 0; i < offsets.size(); ++i) offsets[i] = Read32(data + 72 + i * 4);
    if (counts[0] == 0 || counts[2] == 0 || counts[3] == 0 || counts[8] == 0 || offsets[0] != kHeaderSize) return reject("empty required table");
    for (std::size_t i = 0; i < counts.size(); ++i) {
        const std::uint64_t expected = static_cast<std::uint64_t>(offsets[i]) + static_cast<std::uint64_t>(counts[i]) * row_sizes[i];
        if (counts[i] > ceilings[i] || expected != offsets[i + 1]) return reject("non-canonical table range");
    }
    const std::uint32_t string_bytes = Read32(data + 68);
    if (string_bytes > kMaxStringBytes || static_cast<std::uint64_t>(offsets[9]) + string_bytes != input.size()) return reject("invalid string data range");
    std::vector<std::string> strings;
    strings.reserve(counts[8]);
    for (std::uint32_t i = 0; i < counts[8]; ++i) {
        const auto* row = data + offsets[8] + i * kStringIndexSize;
        const std::uint32_t at = Read32(row), length = Read32(row + 4);
        if (static_cast<std::uint64_t>(at) + length > string_bytes) return reject("invalid string range");
        std::string value(reinterpret_cast<const char*>(data + offsets[9] + at), length);
        if (value.find('\0') != std::string::npos || !IsValidUtf8(value)) return reject("invalid UTF-8 string");
        strings.push_back(std::move(value));
    }
    auto string_at = [&](std::uint32_t id) -> const std::string* { return id < strings.size() ? &strings[id] : nullptr; };
    std::vector<pattern::CharacterClass> classes;
    for (std::uint32_t i = 0; i < counts[6]; ++i) {
        const auto* row = data + offsets[6] + i * kCharacterClassSize;
        const std::uint32_t flags = Read32(row + 16);
        if ((flags & ~kCharacterClassFlagNegated) != 0) return reject("unknown character class flag");
        classes.push_back({{Read64(row), Read64(row + 8)}, (flags & kCharacterClassFlagNegated) != 0});
    }
    std::vector<pattern::PatternProgram> patterns;
    for (std::uint32_t i = 0; i < counts[4]; ++i) {
        const auto* row = data + offsets[4] + i * kPatternSize;
        const std::uint32_t first = Read32(row), count = Read16(row + 4);
        if (count == 0 || count > kMaxPatternTokens || static_cast<std::uint64_t>(first) + count > counts[5]) return reject("invalid pattern range");
        pattern::PatternProgram program;
        pattern::PatternComponent component;
        std::uint32_t semantic_tokens = 0;
        for (std::uint32_t j = 0; j < count; ++j) {
            const auto* token = data + offsets[5] + (first + j) * kPatternTokenSize;
            const std::uint8_t kind = token[0]; const std::uint32_t operand = Read32(token + 4);
            if (kind == 5) {
                if (component.globstar || !component.tokens.empty()) { program.components.push_back(std::move(component)); component = {}; }
                else return reject("invalid pattern separator");
                continue;
            }
            if (kind == 3) { if (!component.tokens.empty() || component.globstar || operand != 0) return reject("invalid globstar"); component.globstar = true; ++semantic_tokens; continue; }
            if (component.globstar || kind > 4) return reject("invalid token kind");
            pattern::PatternToken decoded;
            if (kind == 0) { const auto* value = string_at(operand); if (value == nullptr || value->empty()) return reject("invalid literal id"); decoded.kind = pattern::PatternTokenKind::kLiteral; decoded.literal = *value; }
            else if (kind == 1) { if (operand != 0) return reject("invalid star operand"); decoded.kind = pattern::PatternTokenKind::kStarComponent; }
            else if (kind == 2) { if (operand != 0) return reject("invalid one-char operand"); decoded.kind = pattern::PatternTokenKind::kOneComponentChar; }
            else { if (operand >= classes.size()) return reject("invalid class id"); decoded.kind = pattern::PatternTokenKind::kCharacterClass; decoded.character_class = static_cast<std::uint16_t>(program.character_classes.size()); program.character_classes.push_back(classes[operand]); }
            component.tokens.push_back(std::move(decoded)); ++semantic_tokens;
        }
        if (!component.globstar && component.tokens.empty()) return reject("empty final component");
        program.components.push_back(std::move(component)); program.token_count = static_cast<std::uint16_t>(semantic_tokens);
        // Canonical bytes are derived from semantic tokens, never trusted from the file.
        for (std::size_t ci = 0; ci < program.components.size(); ++ci) {
            if (ci) program.canonical.push_back('/');
            const auto& part = program.components[ci];
            if (part.globstar) { program.canonical.push_back('G'); program.specificity += 1; continue; }
            for (const auto& token : part.tokens) {
                program.canonical.push_back(static_cast<char>(token.kind));
                if (token.kind == pattern::PatternTokenKind::kLiteral) { program.canonical.append(std::to_string(token.literal.size())); program.canonical.push_back(':'); program.canonical.append(token.literal); program.specificity += 16; }
                else if (token.kind == pattern::PatternTokenKind::kStarComponent) program.specificity += 2;
                else if (token.kind == pattern::PatternTokenKind::kOneComponentChar) program.specificity += 4;
                else { const auto& cls = program.character_classes[token.character_class]; program.canonical.push_back(cls.negated ? 1 : 0); for (auto word : cls.bitmap) for (int b = 0; b < 8; ++b) program.canonical.push_back(static_cast<char>(word >> (b * 8))); program.specificity += 8; }
            }
        }
        patterns.push_back(std::move(program));
    }
    PolicyV6 decoded;
    decoded.allow_legacy_mount = (Read32(data + 20) & kPolicyFlagAllowLegacyStringBind) != 0;
    for (std::uint32_t pi = 0; pi < counts[0]; ++pi) {
        const auto* row = data + offsets[0] + pi * kPackageSize;
        const auto* package_name = string_at(Read32(row + 4));
        if (package_name == nullptr) return reject("invalid package string");
        PolicyPackageV6 package; package.package = *package_name; package.plan_generation = Read64(row + 32);
        const std::uint32_t flags = Read32(row + 56);
        package.all_users = (flags & kPackageFlagAllUsers) != 0; package.all_processes = (flags & kPackageFlagAllProcesses) != 0; package.provider_enabled = (flags & kPackageFlagProviderEnabled) != 0;
        const std::uint32_t first_scope = Read32(row + 8); const std::uint16_t users = Read16(row + 12), processes = Read16(row + 14);
        if (static_cast<std::uint64_t>(first_scope) + users + processes > counts[1]) return reject("invalid scope range");
        for (std::uint32_t i = 0; i < users + processes; ++i) {
            const auto* scope = data + offsets[1] + (first_scope + i) * kScopeRefSize;
            if (scope[0] == 0) package.users.push_back(Read32(scope + 4));
            else if (scope[0] == 1) { const auto* process = string_at(Read32(scope + 4)); if (process == nullptr) return reject("invalid process string"); package.processes.push_back(*process); }
            else return reject("unknown scope kind");
        }
        const std::uint32_t first_selector_id = Read32(row + 16), selector_count = Read32(row + 20);
        const std::uint32_t first_action_id = Read32(row + 24), action_count = Read32(row + 28);
        if (static_cast<std::uint64_t>(first_selector_id) + selector_count > counts[2]
            || static_cast<std::uint64_t>(first_action_id) + action_count > counts[3]) return reject("invalid package rule range");
        for (std::uint32_t si = 0; si < selector_count; ++si) {
            const auto* selector = data + offsets[2] + (first_selector_id + si) * kSelectorSize;
            const auto* root = string_at(Read32(selector)); if (root == nullptr) return reject("invalid selector root");
            PolicySelectorV6 value; value.root = *root;
            if (selector[24] > 1 || selector[25] > 2) return reject("unknown selector enum");
            value.match_kind = static_cast<PolicyMatchKind>(selector[24]); value.object_type = static_cast<PolicyObjectType>(selector[25]);
            const std::uint32_t base = Read32(selector + 4);
            if (value.match_kind == PolicyMatchKind::kGlob) { if (base >= patterns.size()) return reject("invalid base pattern"); value.base_pattern = patterns[base]; }
            const std::uint32_t first_ex = Read32(selector + 8); const std::uint16_t ex_count = Read16(selector + 16);
            if (static_cast<std::uint64_t>(first_ex) + ex_count > counts[7]) return reject("invalid except range");
            for (std::uint32_t ei = 0; ei < ex_count; ++ei) { const std::uint32_t id = Read32(data + offsets[7] + (first_ex + ei) * kSelectorExceptRefSize); if (id >= patterns.size()) return reject("invalid except id"); value.except_patterns.push_back(patterns[id]); }
            package.selectors.push_back(std::move(value));
        }
        for (std::uint32_t ai = 0; ai < action_count; ++ai) {
            const auto* action = data + offsets[3] + (first_action_id + ai) * kActionSize;
            const std::uint32_t selector_id = Read32(action);
            if (selector_id < first_selector_id || selector_id >= first_selector_id + selector_count) return reject("cross-package selector reference");
            if (action[40] > 3 || action[41] > 4 || action[42] > 1 || action[43] > 1 || action[44] > 2) return reject("unknown action enum");
            PolicyActionV6 value; value.selector_index = selector_id - first_selector_id; value.rule_id = Read64(action + 8);
            value.required_capabilities = Read64(action + 16); value.required_operations = Read64(action + 24); value.priority = static_cast<std::int32_t>(Read32(action + 32)); value.options = Read32(action + 36);
            value.kind = static_cast<PolicyActionKind>(action[40]); value.domain = static_cast<PolicyExecutionDomain>(action[41]); value.preserve = static_cast<PolicyPreserveMode>(action[42]); value.collision = static_cast<PolicyCollisionMode>(action[43]); value.reverse = static_cast<PolicyReverseMode>(action[44]);
            const std::uint32_t target = Read32(action + 4); if (target != kInvalidId) { const auto* target_value = string_at(target); if (target_value == nullptr) return reject("invalid target id"); value.target = *target_value; }
            package.actions.push_back(std::move(value));
        }
        decoded.packages.push_back(std::move(package));
    }
    std::vector<std::uint8_t> canonical; std::string encode_error;
    if (!EncodePolicyV6(decoded, &canonical, &encode_error)) return reject("decoded policy invalid: " + encode_error);
    if (canonical != input) return reject("policy is not canonical");
    const std::uint64_t generation = Read64(data + 24);
    *output = std::move(decoded);
    return {true, {}, generation};
}

}  // namespace pathguard
