#include "pathguard/binary.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "pathguard/policy_format.h"
#include "pathguard/validation.h"

namespace pathguard {
namespace {

using binary_format::kEventRuleSize;
using binary_format::kHeaderSize;
using binary_format::kMountRuleSize;
using binary_format::kPackageSize;

bool Fail(ParseError* error, std::string message) {
    if (error != nullptr) *error = {0, std::move(message)};
    return false;
}

void Put8(std::vector<std::uint8_t>* output, std::uint8_t value) {
    output->push_back(value);
}

void Put16(std::vector<std::uint8_t>* output, std::uint16_t value) {
    output->push_back(static_cast<std::uint8_t>(value));
    output->push_back(static_cast<std::uint8_t>(value >> 8));
}

void Put32(std::vector<std::uint8_t>* output, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        output->push_back(static_cast<std::uint8_t>(value >> (index * 8)));
    }
}

void Put64(std::vector<std::uint8_t>* output, std::uint64_t value) {
    for (int index = 0; index < 8; ++index) {
        output->push_back(static_cast<std::uint8_t>(value >> (index * 8)));
    }
}

void Store32(std::vector<std::uint8_t>* output, std::size_t offset,
             std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        (*output)[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
}

std::uint16_t Read16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0])
        | static_cast<std::uint16_t>(input[1]) << 8;
}

std::uint32_t Read32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8);
    }
    return value;
}

std::uint64_t Read64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8);
    }
    return value;
}

void PutCanonicalString(std::vector<std::uint8_t>* output, std::string_view value) {
    Put32(output, static_cast<std::uint32_t>(value.size()));
    output->insert(output->end(), value.begin(), value.end());
}

std::vector<std::uint8_t> CanonicalPlan(const AppPolicy& policy,
                                        FailureMode failure_mode) {
    std::vector<std::uint8_t> bytes = {'P', 'G', 'P', 'L', '4', 0};
    Put8(&bytes, static_cast<std::uint8_t>(failure_mode));
    Put8(&bytes, static_cast<std::uint8_t>(policy.media_compat));
    PutCanonicalString(&bytes, policy.package);
    Put32(&bytes, static_cast<std::uint32_t>(policy.users.size()));
    for (const std::string& user : policy.users) PutCanonicalString(&bytes, user);
    Put32(&bytes, static_cast<std::uint32_t>(policy.processes.size()));
    for (const std::string& process : policy.processes) {
        PutCanonicalString(&bytes, process);
    }
    Put32(&bytes, static_cast<std::uint32_t>(policy.mounts.size()));
    for (const LogicalMountRule& rule : policy.mounts) {
        Put8(&bytes, static_cast<std::uint8_t>(rule.action));
        Put16(&bytes, rule.depth);
        Put16(&bytes, rule.flags);
        PutCanonicalString(&bytes, rule.visible_path);
        PutCanonicalString(&bytes, rule.backing_path);
    }
    Put32(&bytes, static_cast<std::uint32_t>(policy.events.size()));
    for (const EventRule& rule : policy.events) {
        Put8(&bytes, static_cast<std::uint8_t>(rule.action));
        Put32(&bytes, rule.options);
        PutCanonicalString(&bytes, rule.source_path);
        PutCanonicalString(&bytes, rule.target_path);
    }
    return bytes;
}

std::uint64_t PlanGeneration(const AppPolicy& policy, FailureMode failure_mode) {
    const std::vector<std::uint8_t> bytes = CanonicalPlan(policy, failure_mode);
    return binary_format::Fnv1a64(bytes.data(), bytes.size());
}

std::uint64_t ContentGeneration(const PolicyDocument& document) {
    std::vector<const AppPolicy*> apps;
    apps.reserve(document.apps.size());
    for (const AppPolicy& app : document.apps) apps.push_back(&app);
    std::sort(apps.begin(), apps.end(), [](const AppPolicy* lhs, const AppPolicy* rhs) {
        return lhs->package < rhs->package;
    });

    std::vector<std::uint8_t> bytes = {'P', 'G', 'I', 'R', '4', 0};
    Put16(&bytes, static_cast<std::uint16_t>(document.schema));
    Put8(&bytes, static_cast<std::uint8_t>(document.failure_mode));
    Put32(&bytes, static_cast<std::uint32_t>(apps.size()));
    for (const AppPolicy* app : apps) {
        const std::vector<std::uint8_t> plan = CanonicalPlan(*app, document.failure_mode);
        Put32(&bytes, static_cast<std::uint32_t>(plan.size()));
        bytes.insert(bytes.end(), plan.begin(), plan.end());
    }
    return binary_format::Fnv1a64(bytes.data(), bytes.size());
}

std::string Join(const std::vector<std::string>& values) {
    std::string joined;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) joined.push_back(',');
        joined.append(values[index]);
    }
    return joined;
}

bool Split(std::string_view value, std::vector<std::string>* output) {
    output->clear();
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(',', start);
        const std::string_view item = value.substr(
            start, end == std::string_view::npos ? value.size() - start : end - start);
        if (item.empty()) return false;
        output->emplace_back(item);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return !output->empty();
}

class StringTable {
public:
    StringTable() { Add(""); }

    std::uint32_t Add(std::string_view value) {
        const auto existing = offsets_.find(std::string(value));
        if (existing != offsets_.end()) return existing->second;
        const std::uint32_t offset = static_cast<std::uint32_t>(bytes_.size());
        std::string key(value);
        offsets_.emplace(key, offset);
        bytes_.insert(bytes_.end(), key.begin(), key.end());
        bytes_.push_back(0);
        return offset;
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::unordered_map<std::string, std::uint32_t> offsets_;
    std::vector<std::uint8_t> bytes_;
};

bool ReadString(const std::vector<std::uint8_t>& input, std::uint32_t string_offset,
                std::uint32_t relative_offset, std::string* output) {
    if (string_offset > input.size() || relative_offset >= input.size() - string_offset) {
        return false;
    }
    const std::size_t begin = string_offset + relative_offset;
    const auto end = std::find(input.begin() + static_cast<std::ptrdiff_t>(begin),
                               input.end(), 0);
    if (end == input.end()) return false;
    output->assign(input.begin() + static_cast<std::ptrdiff_t>(begin), end);
    return true;
}

bool SameMount(const LogicalMountRule& lhs, const LogicalMountRule& rhs) {
    return lhs.action == rhs.action && lhs.visible_path == rhs.visible_path
        && lhs.backing_path == rhs.backing_path && lhs.depth == rhs.depth
        && lhs.flags == rhs.flags;
}

bool SameEvent(const EventRule& lhs, const EventRule& rhs) {
    return lhs.action == rhs.action && lhs.source_path == rhs.source_path
        && lhs.target_path == rhs.target_path && lhs.options == rhs.options;
}

bool CanonicalizeDocument(const PolicyDocument& input, PolicyDocument* output,
                          ParseError* error) {
    if (input.schema != binary_format::kSchemaVersion
        || input.failure_mode != FailureMode::kOpen || input.apps.empty()) {
        return Fail(error, "policy document is not executable schema 2");
    }
    *output = input;
    std::unordered_set<std::string> packages;
    for (AppPolicy& app : output->apps) {
        if (!packages.insert(app.package).second) {
            return Fail(error, "duplicate package");
        }
        if (!ValidatePolicy(&app, error)) return false;
        for (const LogicalMountRule& rule : app.mounts) {
            if (rule.action != MountAction::kDeny) {
                return Fail(error, "mount action is not executable in Phase R0");
            }
        }
        if (!app.events.empty()) {
            return Fail(error, "event action is not executable before Phase R4");
        }
    }
    return true;
}

}  // namespace

std::uint64_t ComputeContentGeneration(const PolicyDocument& document) {
    PolicyDocument canonical;
    if (!CanonicalizeDocument(document, &canonical, nullptr)) return 0;
    return ContentGeneration(canonical);
}

std::uint64_t ComputePlanGeneration(const AppPolicy& policy,
                                    FailureMode failure_mode) {
    AppPolicy canonical = policy;
    if (failure_mode != FailureMode::kOpen
        || !ValidatePolicy(&canonical, nullptr)) {
        return 0;
    }
    return PlanGeneration(canonical, failure_mode);
}

bool EncodePolicy(const PolicyDocument& document, std::vector<std::uint8_t>* output,
                  ParseError* error) {
    if (output == nullptr) return false;
    PolicyDocument canonical;
    if (!CanonicalizeDocument(document, &canonical, error)) return false;

    std::vector<const AppPolicy*> apps;
    apps.reserve(canonical.apps.size());
    std::uint64_t mount_count64 = 0;
    std::uint64_t event_count64 = 0;
    for (const AppPolicy& app : canonical.apps) {
        apps.push_back(&app);
        mount_count64 += app.mounts.size();
        event_count64 += app.events.size();
    }
    if (apps.size() > std::numeric_limits<std::uint32_t>::max()
        || mount_count64 > std::numeric_limits<std::uint32_t>::max()
        || event_count64 > std::numeric_limits<std::uint32_t>::max()) {
        return Fail(error, "policy table count overflow");
    }
    std::sort(apps.begin(), apps.end(), [](const AppPolicy* lhs, const AppPolicy* rhs) {
        const std::uint32_t lhs_hash = binary_format::PackageNameHash(
            lhs->package.data(), lhs->package.size());
        const std::uint32_t rhs_hash = binary_format::PackageNameHash(
            rhs->package.data(), rhs->package.size());
        return std::tuple(lhs_hash, lhs->package) < std::tuple(rhs_hash, rhs->package);
    });

    std::vector<std::uint8_t> packages;
    std::vector<std::uint8_t> mounts;
    std::vector<std::uint8_t> events;
    packages.reserve(apps.size() * kPackageSize);
    mounts.reserve(static_cast<std::size_t>(mount_count64) * kMountRuleSize);
    events.reserve(static_cast<std::size_t>(event_count64) * kEventRuleSize);
    StringTable strings;
    std::uint32_t first_mount = 0;
    std::uint32_t first_event = 0;
    for (const AppPolicy* app : apps) {
        Put32(&packages, binary_format::PackageNameHash(
            app->package.data(), app->package.size()));
        Put32(&packages, strings.Add(app->package));
        Put32(&packages, strings.Add(Join(app->users)));
        Put32(&packages, strings.Add(Join(app->processes)));
        Put32(&packages, first_mount);
        Put32(&packages, static_cast<std::uint32_t>(app->mounts.size()));
        Put32(&packages, first_event);
        Put32(&packages, static_cast<std::uint32_t>(app->events.size()));
        Put64(&packages, PlanGeneration(*app, canonical.failure_mode));
        Put8(&packages, static_cast<std::uint8_t>(canonical.failure_mode));
        Put8(&packages, static_cast<std::uint8_t>(app->media_compat));
        Put16(&packages, 0);
        Put32(&packages, 0);

        for (const LogicalMountRule& rule : app->mounts) {
            Put8(&mounts, static_cast<std::uint8_t>(rule.action));
            Put8(&mounts, 0);
            Put16(&mounts, rule.depth);
            Put16(&mounts, rule.flags);
            Put16(&mounts, 0);
            Put32(&mounts, strings.Add(rule.visible_path));
            Put32(&mounts, strings.Add(rule.backing_path));
        }
        for (const EventRule& rule : app->events) {
            Put8(&events, static_cast<std::uint8_t>(rule.action));
            Put8(&events, 0);
            Put16(&events, 0);
            Put32(&events, strings.Add(rule.source_path));
            Put32(&events, strings.Add(rule.target_path));
            Put32(&events, rule.options);
        }
        first_mount += static_cast<std::uint32_t>(app->mounts.size());
        first_event += static_cast<std::uint32_t>(app->events.size());
    }

    const std::uint64_t file_size64 = kHeaderSize + packages.size() + mounts.size()
        + events.size() + strings.bytes().size();
    if (file_size64 > std::numeric_limits<std::uint32_t>::max()) {
        return Fail(error, "policy file too large");
    }
    const std::uint32_t package_offset = static_cast<std::uint32_t>(kHeaderSize);
    const std::uint32_t mount_offset = package_offset
        + static_cast<std::uint32_t>(packages.size());
    const std::uint32_t event_offset = mount_offset
        + static_cast<std::uint32_t>(mounts.size());
    const std::uint32_t string_offset = event_offset
        + static_cast<std::uint32_t>(events.size());

    output->clear();
    output->reserve(static_cast<std::size_t>(file_size64));
    Put32(output, binary_format::kMagic);
    Put16(output, binary_format::kFormatVersion);
    Put16(output, binary_format::kSchemaVersion);
    Put32(output, static_cast<std::uint32_t>(file_size64));
    Put32(output, 0);
    Put64(output, ContentGeneration(canonical));
    Put32(output, static_cast<std::uint32_t>(apps.size()));
    Put32(output, static_cast<std::uint32_t>(mount_count64));
    Put32(output, static_cast<std::uint32_t>(event_count64));
    Put32(output, package_offset);
    Put32(output, mount_offset);
    Put32(output, event_offset);
    Put32(output, string_offset);
    Put32(output, 0);
    output->insert(output->end(), packages.begin(), packages.end());
    output->insert(output->end(), mounts.begin(), mounts.end());
    output->insert(output->end(), events.begin(), events.end());
    output->insert(output->end(), strings.bytes().begin(), strings.bytes().end());
    const std::uint32_t checksum = binary_format::Crc32(
        output->data() + kHeaderSize, output->size() - kHeaderSize);
    Store32(output, binary_format::kPayloadChecksumOffset, checksum);
    return true;
}

bool DecodePolicy(const std::vector<std::uint8_t>& input, PolicyDocument* document,
                  std::uint64_t* content_generation, ParseError* error) {
    if (document == nullptr || content_generation == nullptr || input.size() < kHeaderSize) {
        return false;
    }
    const std::uint8_t* data = input.data();
    const std::uint32_t file_size = Read32(data + binary_format::kFileSizeOffset);
    const std::uint32_t checksum = Read32(data + binary_format::kPayloadChecksumOffset);
    const std::uint32_t package_count = Read32(data + binary_format::kPackageCountOffset);
    const std::uint32_t mount_count = Read32(data + binary_format::kMountRuleCountOffset);
    const std::uint32_t event_count = Read32(data + binary_format::kEventRuleCountOffset);
    const std::uint32_t package_offset = Read32(data + binary_format::kPackageTableOffset);
    const std::uint32_t mount_offset = Read32(data + binary_format::kMountRuleTableOffset);
    const std::uint32_t event_offset = Read32(data + binary_format::kEventRuleTableOffset);
    const std::uint32_t string_offset = Read32(data + binary_format::kStringTableOffset);
    const std::uint64_t expected_mount_offset = kHeaderSize
        + static_cast<std::uint64_t>(package_count) * kPackageSize;
    const std::uint64_t expected_event_offset = expected_mount_offset
        + static_cast<std::uint64_t>(mount_count) * kMountRuleSize;
    const std::uint64_t expected_string_offset = expected_event_offset
        + static_cast<std::uint64_t>(event_count) * kEventRuleSize;
    if (Read32(data) != binary_format::kMagic
        || Read16(data + 4) != binary_format::kFormatVersion
        || Read16(data + 6) != binary_format::kSchemaVersion
        || file_size != input.size() || package_count == 0
        || package_offset != kHeaderSize || mount_offset != expected_mount_offset
        || event_offset != expected_event_offset || string_offset != expected_string_offset
        || string_offset >= input.size() || Read32(data + 52) != 0
        || binary_format::Crc32(data + kHeaderSize, input.size() - kHeaderSize)
            != checksum) {
        return Fail(error, "invalid policy header or checksum");
    }

    PolicyDocument decoded;
    decoded.schema = binary_format::kSchemaVersion;
    std::uint32_t previous_hash = 0;
    std::uint32_t expected_first_mount = 0;
    std::uint32_t expected_first_event = 0;
    std::string previous_package;
    for (std::uint32_t index = 0; index < package_count; ++index) {
        const std::uint8_t* entry = data + package_offset + index * kPackageSize;
        const std::uint32_t package_hash = Read32(entry + binary_format::kPackageHashOffset);
        AppPolicy app;
        std::string users;
        std::string processes;
        if (!ReadString(input, string_offset,
                        Read32(entry + binary_format::kPackageNameOffset), &app.package)
            || !ReadString(input, string_offset,
                           Read32(entry + binary_format::kPackageUsersOffset), &users)
            || !ReadString(input, string_offset,
                           Read32(entry + binary_format::kPackageProcessesOffset), &processes)
            || !Split(users, &app.users) || !Split(processes, &app.processes)) {
            return Fail(error, "invalid package strings");
        }
        if (binary_format::PackageNameHash(app.package.data(), app.package.size())
                != package_hash
            || (index > 0 && (package_hash < previous_hash
                || (package_hash == previous_hash && app.package <= previous_package)))) {
            return Fail(error, "invalid package index");
        }
        previous_hash = package_hash;
        previous_package = app.package;

        const std::uint8_t failure = entry[binary_format::kPackageFailureModeOffset];
        const std::uint8_t media = entry[binary_format::kPackageMediaCompatOffset];
        if (failure > static_cast<std::uint8_t>(FailureMode::kClosed)
            || media > static_cast<std::uint8_t>(MediaCompat::kHideDenied)
            || Read16(entry + 42) != 0 || Read32(entry + 44) != 0) {
            return Fail(error, "invalid package flags");
        }
        const FailureMode package_failure = static_cast<FailureMode>(failure);
        if (package_failure != FailureMode::kOpen
            || (index > 0 && package_failure != decoded.failure_mode)) {
            return Fail(error, "unsupported failure mode");
        }
        decoded.failure_mode = package_failure;
        app.media_compat = static_cast<MediaCompat>(media);

        const std::uint32_t first_mount = Read32(
            entry + binary_format::kPackageFirstMountOffset);
        const std::uint32_t app_mount_count = Read32(
            entry + binary_format::kPackageMountCountOffset);
        const std::uint32_t first_event = Read32(
            entry + binary_format::kPackageFirstEventOffset);
        const std::uint32_t app_event_count = Read32(
            entry + binary_format::kPackageEventCountOffset);
        if (first_mount != expected_first_mount
            || app_mount_count > mount_count - first_mount
            || first_event != expected_first_event
            || app_event_count > event_count - first_event) {
            return Fail(error, "invalid package rule range");
        }
        expected_first_mount += app_mount_count;
        expected_first_event += app_event_count;
        for (std::uint32_t rule_index = 0; rule_index < app_mount_count; ++rule_index) {
            const std::uint8_t* rule = data + mount_offset
                + (first_mount + rule_index) * kMountRuleSize;
            if (rule[1] != 0 || Read16(rule + 6) != 0
                || rule[binary_format::kMountActionOffset]
                    > static_cast<std::uint8_t>(MountAction::kIsolateRoot)) {
                return Fail(error, "invalid mount action");
            }
            LogicalMountRule mount;
            mount.action = static_cast<MountAction>(rule[binary_format::kMountActionOffset]);
            mount.depth = Read16(rule + binary_format::kMountDepthOffset);
            mount.flags = Read16(rule + binary_format::kMountFlagsOffset);
            if (!ReadString(input, string_offset,
                            Read32(rule + binary_format::kMountVisiblePathOffset),
                            &mount.visible_path)
                || !ReadString(input, string_offset,
                               Read32(rule + binary_format::kMountBackingPathOffset),
                               &mount.backing_path)) {
                return Fail(error, "invalid mount strings");
            }
            app.mounts.push_back(std::move(mount));
        }
        for (std::uint32_t rule_index = 0; rule_index < app_event_count; ++rule_index) {
            const std::uint8_t* rule = data + event_offset
                + (first_event + rule_index) * kEventRuleSize;
            if (rule[1] != 0 || Read16(rule + 2) != 0
                || rule[binary_format::kEventActionOffset]
                    > static_cast<std::uint8_t>(EventAction::kExport)) {
                return Fail(error, "invalid event action");
            }
            EventRule event;
            event.action = static_cast<EventAction>(rule[binary_format::kEventActionOffset]);
            event.options = Read32(rule + binary_format::kEventOptionsOffset);
            if (!ReadString(input, string_offset,
                            Read32(rule + binary_format::kEventSourcePathOffset),
                            &event.source_path)
                || !ReadString(input, string_offset,
                               Read32(rule + binary_format::kEventTargetPathOffset),
                               &event.target_path)) {
                return Fail(error, "invalid event strings");
            }
            app.events.push_back(std::move(event));
        }

        const AppPolicy encoded_order = app;
        if (!ValidatePolicy(&app, error) || app.mounts.size() != encoded_order.mounts.size()
            || app.events.size() != encoded_order.events.size()) {
            return Fail(error, "invalid canonical package plan");
        }
        for (std::size_t rule_index = 0; rule_index < app.mounts.size(); ++rule_index) {
            if (!SameMount(app.mounts[rule_index], encoded_order.mounts[rule_index])) {
                return Fail(error, "unordered mount table");
            }
        }
        for (std::size_t rule_index = 0; rule_index < app.events.size(); ++rule_index) {
            if (!SameEvent(app.events[rule_index], encoded_order.events[rule_index])) {
                return Fail(error, "unordered event table");
            }
        }
        if (PlanGeneration(app, decoded.failure_mode)
            != Read64(entry + binary_format::kPackagePlanGenerationOffset)) {
            return Fail(error, "invalid plan generation");
        }
        decoded.apps.push_back(std::move(app));
    }
    if (expected_first_mount != mount_count || expected_first_event != event_count) {
        return Fail(error, "unreferenced policy rule");
    }

    const std::uint64_t encoded_generation = Read64(
        data + binary_format::kContentGenerationOffset);
    if (ContentGeneration(decoded) != encoded_generation) {
        return Fail(error, "invalid content generation");
    }
    *content_generation = encoded_generation;
    *document = std::move(decoded);
    return true;
}

}  // namespace pathguard
