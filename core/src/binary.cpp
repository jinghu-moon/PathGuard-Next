#include "pathguard/binary.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "pathguard/policy_format.h"

namespace pathguard {
namespace {

constexpr std::uint32_t kMagic = binary_format::kMagic;
constexpr std::uint16_t kFormatVersion = binary_format::kFormatVersion;
constexpr std::size_t kHeaderSize = binary_format::kHeaderSize;
constexpr std::size_t kPackageSize = binary_format::kPackageSize;
constexpr std::size_t kRuleSize = binary_format::kRuleSize;

void Put16(std::vector<std::uint8_t>* out, std::uint16_t value) {
    out->push_back(static_cast<std::uint8_t>(value));
    out->push_back(static_cast<std::uint8_t>(value >> 8));
}
void Put32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out->push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void Put64(std::vector<std::uint8_t>* out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out->push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
bool Get16(const std::vector<std::uint8_t>& in, std::size_t* p, std::uint16_t* value) {
    if (*p + 2 > in.size()) return false;
    *value = static_cast<std::uint16_t>(in[*p] | (in[*p + 1] << 8)); *p += 2; return true;
}
bool Get32(const std::vector<std::uint8_t>& in, std::size_t* p, std::uint32_t* value) {
    if (*p + 4 > in.size()) return false;
    *value = 0; for (int i = 0; i < 4; ++i) *value |= static_cast<std::uint32_t>(in[*p + i]) << (i * 8); *p += 4; return true;
}
bool Get64(const std::vector<std::uint8_t>& in, std::size_t* p, std::uint64_t* value) {
    if (*p + 8 > in.size()) return false;
    *value = 0; for (int i = 0; i < 8; ++i) *value |= static_cast<std::uint64_t>(in[*p + i]) << (i * 8); *p += 8; return true;
}
std::uint32_t Hash(const std::vector<std::uint8_t>& input, std::size_t begin) {
    return binary_format::Fnv1a32(input.data() + begin, input.size() - begin);
}
bool ReadString(const std::vector<std::uint8_t>& in, std::uint32_t offset, std::string* out) {
    if (offset >= in.size()) return false;
    const auto end = std::find(in.begin() + offset, in.end(), 0);
    if (end == in.end()) return false;
    out->assign(reinterpret_cast<const char*>(in.data() + offset), static_cast<std::size_t>(end - (in.begin() + offset)));
    return true;
}
bool Fail(ParseError* error, const char* message) { if (error) *error = {0, message}; return false; }
std::string Join(const std::vector<std::string>& values) { std::string out; for (std::size_t i = 0; i < values.size(); ++i) { if (i) out.push_back(','); out += values[i]; } return out; }
std::size_t JoinedSize(const std::vector<std::string>& values) {
    std::size_t size = values.empty() ? 0 : values.size() - 1;
    for (const auto& value : values) size += value.size();
    return size;
}
void Split(const std::string& value, std::vector<std::string>* output) { output->clear(); std::size_t start = 0; while (start <= value.size()) { const auto end = value.find(',', start); output->push_back(value.substr(start, end == std::string::npos ? value.size() - start : end - start)); if (end == std::string::npos) break; start = end + 1; } }

}  // namespace

bool EncodePolicy(const PolicyDocument& document, std::uint64_t generation,
                  std::vector<std::uint8_t>* output, ParseError* error) {
    if (!output || document.schema != 1 || document.apps.empty()) return Fail(error, "invalid policy document");
    std::vector<std::uint8_t> packages(kHeaderSize), rules, strings(1, 0);
    std::uint32_t total_rules = 0;
    std::size_t string_capacity = 1;
    for (const AppPolicy& app : document.apps) {
        string_capacity += app.package.size() + 1;
        string_capacity += JoinedSize(app.users) + 1;
        string_capacity += JoinedSize(app.processes) + 1;
        for (const Rule& rule : app.rules) {
            string_capacity += rule.source.size() + 1;
            if (rule.action == RuleAction::kRedirect) {
                string_capacity += rule.target.size() + 1;
            }
            ++total_rules;
        }
    }
    packages.reserve(kHeaderSize + document.apps.size() * kPackageSize);
    rules.reserve(static_cast<std::size_t>(total_rules) * kRuleSize);
    strings.reserve(string_capacity);
    std::vector<const AppPolicy*> sorted_apps;
    sorted_apps.reserve(document.apps.size());
    for (const AppPolicy& app : document.apps) sorted_apps.push_back(&app);
    std::sort(sorted_apps.begin(), sorted_apps.end(), [](const AppPolicy* lhs, const AppPolicy* rhs) {
        const std::uint32_t lhs_hash = binary_format::PackageNameHash(
            lhs->package.data(), lhs->package.size());
        const std::uint32_t rhs_hash = binary_format::PackageNameHash(
            rhs->package.data(), rhs->package.size());
        return lhs_hash != rhs_hash ? lhs_hash < rhs_hash : lhs->package < rhs->package;
    });
    total_rules = 0;
    for (const AppPolicy* app_ptr : sorted_apps) {
        const AppPolicy& app = *app_ptr;
        const std::uint32_t package_offset = static_cast<std::uint32_t>(strings.size());
        strings.insert(strings.end(), app.package.begin(), app.package.end()); strings.push_back(0);
        const std::uint32_t users_offset = static_cast<std::uint32_t>(strings.size()); const std::string users = Join(app.users); strings.insert(strings.end(), users.begin(), users.end()); strings.push_back(0);
        const std::uint32_t processes_offset = static_cast<std::uint32_t>(strings.size()); const std::string processes = Join(app.processes); strings.insert(strings.end(), processes.begin(), processes.end()); strings.push_back(0);
        const std::uint32_t first_rule = total_rules;
        for (const Rule& rule : app.rules) {
            const std::uint32_t source = static_cast<std::uint32_t>(strings.size());
            strings.insert(strings.end(), rule.source.begin(), rule.source.end()); strings.push_back(0);
            const std::uint32_t target = rule.action == RuleAction::kRedirect ? static_cast<std::uint32_t>(strings.size()) : 0;
            if (rule.action == RuleAction::kRedirect) { strings.insert(strings.end(), rule.target.begin(), rule.target.end()); strings.push_back(0); }
            Put32(&rules, rule.action == RuleAction::kDeny ? 0u : 1u); Put32(&rules, source); Put32(&rules, target); Put64(&rules, rule.line); ++total_rules;
        }
        Put32(&packages, binary_format::PackageNameHash(app.package.data(), app.package.size()));
        Put32(&packages, package_offset); Put32(&packages, users_offset); Put32(&packages, processes_offset);
        Put32(&packages, first_rule); Put32(&packages, total_rules - first_rule);
        Put32(&packages, app.enabled ? 1u : 0u);
        Put32(&packages, app.media_compat == MediaCompat::kQueryFilter ? 1u : 0u);
    }
    const std::uint32_t package_offset = static_cast<std::uint32_t>(packages.size() - document.apps.size() * kPackageSize);
    const std::uint32_t rule_offset = static_cast<std::uint32_t>(packages.size());
    const std::uint32_t string_offset = rule_offset + static_cast<std::uint32_t>(rules.size());
    output->clear(); output->reserve(string_offset + strings.size());
    Put32(output, kMagic); Put16(output, kFormatVersion); Put16(output, static_cast<std::uint16_t>(document.schema)); Put64(output, generation);
    Put32(output, string_offset + static_cast<std::uint32_t>(strings.size())); Put32(output, Hash(strings, 0));
    Put32(output, static_cast<std::uint32_t>(document.apps.size())); Put32(output, package_offset); Put32(output, rule_offset); Put32(output, string_offset);
    output->insert(output->end(), packages.begin() + kHeaderSize, packages.end()); output->insert(output->end(), rules.begin(), rules.end()); output->insert(output->end(), strings.begin(), strings.end());
    return true;
}

bool DecodePolicy(const std::vector<std::uint8_t>& input, PolicyDocument* document,
                  std::uint64_t* generation, ParseError* error) {
    if (!document || input.size() < kHeaderSize) return Fail(error, "truncated policy");
    std::size_t p = 0; std::uint32_t magic, file_size, checksum, package_count, package_offset, rule_offset, string_offset; std::uint16_t format, schema;
    if (!Get32(input,&p,&magic)||!Get16(input,&p,&format)||!Get16(input,&p,&schema)||!Get64(input,&p,generation)||!Get32(input,&p,&file_size)||!Get32(input,&p,&checksum)||!Get32(input,&p,&package_count)||!Get32(input,&p,&package_offset)||!Get32(input,&p,&rule_offset)||!Get32(input,&p,&string_offset)) return Fail(error,"invalid header");
    if (magic != kMagic || format != kFormatVersion || schema != 1 || file_size != input.size() || package_offset < kHeaderSize || rule_offset < package_offset || string_offset < rule_offset || string_offset > input.size()) return Fail(error,"invalid policy header");
    if (package_count > (rule_offset - package_offset) / kPackageSize || Hash(input, string_offset) != checksum) return Fail(error,"invalid policy bounds or checksum");
    document->schema = schema; document->failure_mode = "fail_open_with_alert"; document->apps.clear();
    std::uint32_t previous_hash = 0;
    std::string previous_package;
    for (std::uint32_t i=0;i<package_count;++i) {
        std::size_t q = package_offset + i*kPackageSize; std::uint32_t package_hash, package_string, users_string, processes_string, first, count, enabled, media_compat; std::string users, processes;
        if (!Get32(input,&q,&package_hash)||!Get32(input,&q,&package_string)||!Get32(input,&q,&users_string)||!Get32(input,&q,&processes_string)||!Get32(input,&q,&first)||!Get32(input,&q,&count)||!Get32(input,&q,&enabled)||!Get32(input,&q,&media_compat)||!ReadString(input, string_offset+package_string, &document->apps.emplace_back().package)||!ReadString(input, string_offset+users_string, &users)||!ReadString(input, string_offset+processes_string, &processes)) return Fail(error,"invalid package entry");
        if (media_compat > 1) return Fail(error, "invalid media compat mode");
        AppPolicy& app = document->apps.back(); app.enabled = enabled != 0; app.media_compat = media_compat == 1 ? MediaCompat::kQueryFilter : MediaCompat::kOff; if (!users.empty()) Split(users, &app.users); if (!processes.empty()) Split(processes, &app.processes);
        if (binary_format::PackageNameHash(app.package.data(), app.package.size()) != package_hash
            || (i > 0 && (package_hash < previous_hash
                || (package_hash == previous_hash && app.package < previous_package)))) {
            return Fail(error, "invalid package index");
        }
        previous_hash = package_hash;
        previous_package = app.package;
        if (first > (string_offset-rule_offset)/kRuleSize || count > (string_offset-rule_offset)/kRuleSize-first) return Fail(error,"invalid rule range");
        for (std::uint32_t j=0;j<count;++j) { std::size_t r=rule_offset+(first+j)*kRuleSize; std::uint32_t action,source,target; std::uint64_t line; if(!Get32(input,&r,&action)||!Get32(input,&r,&source)||!Get32(input,&r,&target)||!Get64(input,&r,&line)) return Fail(error,"invalid rule entry"); Rule rule{action==0?RuleAction::kDeny:RuleAction::kRedirect,"","",static_cast<std::size_t>(line)}; if(!ReadString(input,string_offset+source,&rule.source)||(action!=0&&!ReadString(input,string_offset+target,&rule.target))) return Fail(error,"invalid rule string"); app.rules.push_back(std::move(rule)); }
    }
    return true;
}

}  // namespace pathguard
