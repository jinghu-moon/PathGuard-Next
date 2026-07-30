#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pathguard/policy_format.h"

namespace pathguard::policy_v6_view {

enum class Error : uint8_t {
    kNone,
    kNullInput,
    kFileSize,
    kVersion,
    kHeader,
    kChecksum,
    kTableRange,
    kString,
    kRow,
    kReference,
    kScope,
};

struct StringRef {
    const char* data = nullptr;
    uint32_t size = 0;

    bool empty() const { return size == 0; }
    bool Equals(const char* value, size_t length) const {
        return value != nullptr && size == length
            && (size == 0 || memcmp(data, value, size) == 0);
    }
    bool CopyTo(char* output, size_t capacity) const {
        if (output == nullptr || static_cast<size_t>(size) >= capacity) return false;
        if (size != 0) memcpy(output, data, size);
        output[size] = '\0';
        return true;
    }
};

struct PackageRef {
    uint32_t index = 0;
    uint32_t name_id = 0;
    uint32_t first_scope = 0;
    uint16_t user_count = 0;
    uint16_t process_count = 0;
    uint32_t first_selector = 0;
    uint32_t selector_count = 0;
    uint32_t first_action = 0;
    uint32_t action_count = 0;
    uint64_t plan_generation = 0;
    uint32_t flags = 0;
};

struct SelectorRef {
    uint32_t index = 0;
    uint32_t root_id = 0;
    uint32_t base_pattern_id = binary_format::kInvalidId;
    uint32_t first_except = 0;
    uint32_t first_action = 0;
    uint16_t except_count = 0;
    uint16_t action_count = 0;
    uint8_t match_kind = 0;
    uint8_t object_type = 0;
    uint32_t first_literal_id = binary_format::kInvalidId;
    uint32_t extension_id = binary_format::kInvalidId;
};

struct ActionRef {
    uint32_t index = 0;
    uint32_t selector_id = 0;
    uint32_t target_id = binary_format::kInvalidId;
    uint64_t rule_id = 0;
    uint64_t required_capabilities = 0;
    uint64_t required_operations = 0;
    int32_t priority = 0;
    uint32_t options = 0;
    uint8_t kind = 0;
    uint8_t domain = 0;
    uint8_t preserve = 0;
    uint8_t collision = 0;
    uint8_t reverse = 0;
};

struct PatternRef {
    uint32_t first_token = 0;
    uint16_t token_count = 0;
    uint16_t component_count = 0;
    uint32_t first_literal_id = binary_format::kInvalidId;
    uint32_t extension_id = binary_format::kInvalidId;
    uint16_t flags = 0;
};

struct TokenRef {
    uint8_t kind = 0;
    uint32_t operand = 0;
};

struct CharacterClassRef {
    uint64_t low = 0;
    uint64_t high = 0;
    bool negated = false;
};

class PolicyV6View {
public:
    bool Initialize(const uint8_t* data, size_t size, Error* error = nullptr) {
        Reset();
        if (data == nullptr) return Fail(Error::kNullInput, error);
        if (size < binary_format::kHeaderSize
            || size > binary_format::kMaxPolicyFileSize) {
            return Fail(Error::kFileSize, error);
        }
        data_ = data;
        size_ = size;
        if (Read32(data) != binary_format::kMagic
            || Read16(data + 4) != binary_format::kFormatVersion
            || Read16(data + 6) != binary_format::kSchemaVersion) {
            return Fail(Error::kVersion, error);
        }
        if (Read32(data + binary_format::kFileSizeOffset) != size
            || Read32(data + 8) != binary_format::kHeaderSize
            || data[binary_format::kFailureModeOffset] != 0
            || data[binary_format::kOperationMaskVersionOffset]
                != binary_format::kOperationMaskVersion
            || !AllZero(data + 114, binary_format::kHeaderSize - 114)
            || (Read32(data + binary_format::kHeaderFlagsOffset)
                & ~binary_format::kPolicyFlagAllowLegacyStringBind) != 0) {
            return Fail(Error::kHeader, error);
        }
        if (binary_format::Crc32(data + binary_format::kHeaderSize,
                                 size - binary_format::kHeaderSize)
            != Read32(data + binary_format::kPayloadChecksumOffset)) {
            return Fail(Error::kChecksum, error);
        }
        static constexpr uint32_t kCeilings[9] = {
            binary_format::kMaxPackageCount,
            binary_format::kMaxScopeRefCount,
            binary_format::kMaxSelectorCount,
            binary_format::kMaxActionCount,
            binary_format::kMaxPatternCount,
            binary_format::kMaxTokenCount,
            binary_format::kMaxClassCount,
            binary_format::kMaxExceptRefCount,
            binary_format::kMaxStringCount,
        };
        static constexpr size_t kRowSizes[9] = {
            binary_format::kPackageSize,
            binary_format::kScopeRefSize,
            binary_format::kSelectorSize,
            binary_format::kActionSize,
            binary_format::kPatternSize,
            binary_format::kPatternTokenSize,
            binary_format::kCharacterClassSize,
            binary_format::kSelectorExceptRefSize,
            binary_format::kStringIndexSize,
        };
        for (size_t i = 0; i < 9; ++i) {
            counts_[i] = Read32(data + binary_format::kPackageCountOffset
                                + i * sizeof(uint32_t));
            offsets_[i] = Read32(data + binary_format::kPackageTableOffset
                                 + i * sizeof(uint32_t));
        }
        offsets_[9] = Read32(data + binary_format::kStringDataOffset);
        if (counts_[0] == 0 || counts_[2] == 0 || counts_[3] == 0
            || counts_[8] == 0 || offsets_[0] != binary_format::kHeaderSize) {
            return Fail(Error::kTableRange, error);
        }
        for (size_t i = 0; i < 9; ++i) {
            const uint64_t end = static_cast<uint64_t>(offsets_[i])
                + static_cast<uint64_t>(counts_[i]) * kRowSizes[i];
            if (counts_[i] > kCeilings[i] || end != offsets_[i + 1]) {
                return Fail(Error::kTableRange, error);
            }
        }
        string_bytes_ = Read32(data + binary_format::kStringBytesOffset);
        if (string_bytes_ > binary_format::kMaxStringBytes
            || static_cast<uint64_t>(offsets_[9]) + string_bytes_ != size) {
            return Fail(Error::kTableRange, error);
        }
        if (!ValidateStrings()) return Fail(Error::kString, error);
        if (!ValidateClasses() || !ValidatePatterns() || !ValidateExceptRefs()) {
            return Fail(Error::kRow, error);
        }
        if (!ValidatePackages()) return Fail(validation_error_, error);
        valid_ = true;
        if (error != nullptr) *error = Error::kNone;
        return true;
    }

    bool valid() const { return valid_; }
    uint64_t content_generation() const {
        return valid_ ? Read64(data_ + binary_format::kContentGenerationOffset) : 0;
    }
    uint32_t flags() const {
        return valid_ ? Read32(data_ + binary_format::kHeaderFlagsOffset) : 0;
    }
    uint32_t package_count() const { return valid_ ? counts_[0] : 0; }

    bool StringAt(uint32_t id, StringRef* output) const {
        if (output == nullptr || id >= counts_[8]) return false;
        const uint8_t* row = Row(8, id, binary_format::kStringIndexSize);
        const uint32_t at = Read32(row);
        const uint32_t length = Read32(row + 4);
        if (static_cast<uint64_t>(at) + length > string_bytes_) return false;
        output->data = reinterpret_cast<const char*>(data_ + offsets_[9] + at);
        output->size = length;
        return true;
    }

    bool PackageAt(uint32_t index, PackageRef* output) const {
        if (!valid_ || output == nullptr || index >= counts_[0]) return false;
        DecodePackage(index, output);
        return true;
    }

    bool SelectorAt(uint32_t index, SelectorRef* output) const {
        if (!valid_ || output == nullptr || index >= counts_[2]) return false;
        DecodeSelector(index, output);
        return true;
    }

    bool ActionAt(uint32_t index, ActionRef* output) const {
        if (!valid_ || output == nullptr || index >= counts_[3]) return false;
        DecodeAction(index, output);
        return true;
    }

    bool PatternAt(uint32_t index, PatternRef* output) const {
        if (!valid_ || output == nullptr || index >= counts_[4]) return false;
        const uint8_t* row = Row(4, index, binary_format::kPatternSize);
        output->first_token = Read32(row);
        output->token_count = Read16(row + 4);
        output->component_count = Read16(row + 6);
        output->first_literal_id = Read32(row + 8);
        output->extension_id = Read32(row + 12);
        output->flags = Read16(row + 16);
        return true;
    }

    bool TokenAt(uint32_t index, TokenRef* output) const {
        if (!valid_ || output == nullptr || index >= counts_[5]) return false;
        const uint8_t* row = Row(5, index, binary_format::kPatternTokenSize);
        output->kind = row[0];
        output->operand = Read32(row + 4);
        return true;
    }

    bool CharacterClassAt(uint32_t index, CharacterClassRef* output) const {
        if (!valid_ || output == nullptr || index >= counts_[6]) return false;
        const uint8_t* row = Row(6, index, binary_format::kCharacterClassSize);
        output->low = Read64(row);
        output->high = Read64(row + 8);
        output->negated = (Read32(row + 16)
            & binary_format::kCharacterClassFlagNegated) != 0;
        return true;
    }

    bool SelectorExceptPatternAt(const SelectorRef& selector,
                                 uint32_t local_index,
                                 uint32_t* pattern_id) const {
        if (!valid_ || pattern_id == nullptr || local_index >= selector.except_count) {
            return false;
        }
        const uint8_t* row = Row(7, selector.first_except + local_index,
                                 binary_format::kSelectorExceptRefSize);
        *pattern_id = Read32(row);
        return *pattern_id < counts_[4];
    }

    bool PackageUserAt(const PackageRef& package, uint32_t local_index,
                       uint32_t* user_id) const {
        if (!valid_ || user_id == nullptr || local_index >= package.user_count) {
            return false;
        }
        const uint8_t* row = Row(1, package.first_scope + local_index,
                                 binary_format::kScopeRefSize);
        if (row[0] != 0) return false;
        *user_id = Read32(row + 4);
        return true;
    }

    bool FindPackage(const char* package_name, size_t length,
                     PackageRef* output) const {
        if (!valid_ || package_name == nullptr || length == 0 || output == nullptr) {
            return false;
        }
        const uint32_t hash = binary_format::PackageNameHash(package_name, length);
        uint32_t low = 0;
        uint32_t high = counts_[0];
        while (low < high) {
            const uint32_t middle = low + (high - low) / 2;
            const uint8_t* row = Row(0, middle, binary_format::kPackageSize);
            if (Read32(row) < hash) low = middle + 1;
            else high = middle;
        }
        for (uint32_t index = low; index < counts_[0]; ++index) {
            const uint8_t* row = Row(0, index, binary_format::kPackageSize);
            if (Read32(row) != hash) break;
            StringRef candidate;
            if (StringAt(Read32(row + 4), &candidate)
                && candidate.Equals(package_name, length)) {
                DecodePackage(index, output);
                return true;
            }
        }
        return false;
    }

    bool PackageMatchesScope(const PackageRef& package,
                             const char* process_name, size_t process_length,
                             uint32_t user_id) const {
        const bool all_users = (package.flags & binary_format::kPackageFlagAllUsers) != 0;
        bool user_matches = all_users;
        for (uint32_t i = 0; !user_matches && i < package.user_count; ++i) {
            const uint8_t* row = Row(1, package.first_scope + i,
                                     binary_format::kScopeRefSize);
            user_matches = Read32(row + 4) == user_id;
        }
        const bool all_processes =
            (package.flags & binary_format::kPackageFlagAllProcesses) != 0;
        bool process_matches = all_processes;
        for (uint32_t i = 0; !process_matches && i < package.process_count; ++i) {
            const uint8_t* row = Row(1, package.first_scope + package.user_count + i,
                                     binary_format::kScopeRefSize);
            StringRef process;
            process_matches = StringAt(Read32(row + 4), &process)
                && process.Equals(process_name, process_length);
        }
        return user_matches && process_matches;
    }

private:
    static uint16_t Read16(const uint8_t* value) {
        return static_cast<uint16_t>(value[0])
            | static_cast<uint16_t>(value[1]) << 8;
    }
    static uint32_t Read32(const uint8_t* value) {
        uint32_t result = 0;
        for (int i = 0; i < 4; ++i) result |= static_cast<uint32_t>(value[i]) << (i * 8);
        return result;
    }
    static uint64_t Read64(const uint8_t* value) {
        uint64_t result = 0;
        for (int i = 0; i < 8; ++i) result |= static_cast<uint64_t>(value[i]) << (i * 8);
        return result;
    }
    static bool AllZero(const uint8_t* value, size_t size) {
        for (size_t i = 0; i < size; ++i) if (value[i] != 0) return false;
        return true;
    }
    static bool ValidUtf8(const uint8_t* value, size_t size) {
        size_t i = 0;
        while (i < size) {
            const uint8_t first = value[i++];
            if (first == 0) return false;
            if (first < 0x80) continue;
            uint32_t scalar = 0;
            uint32_t minimum = 0;
            size_t trailing = 0;
            if ((first & 0xe0) == 0xc0) { scalar = first & 0x1f; minimum = 0x80; trailing = 1; }
            else if ((first & 0xf0) == 0xe0) { scalar = first & 0x0f; minimum = 0x800; trailing = 2; }
            else if ((first & 0xf8) == 0xf0) { scalar = first & 0x07; minimum = 0x10000; trailing = 3; }
            else return false;
            if (trailing > size - i) return false;
            for (size_t j = 0; j < trailing; ++j) {
                const uint8_t next = value[i++];
                if ((next & 0xc0) != 0x80) return false;
                scalar = (scalar << 6) | (next & 0x3f);
            }
            if (scalar < minimum || scalar > 0x10ffff
                || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
        }
        return true;
    }
    static int Compare(const StringRef& lhs, const StringRef& rhs) {
        const size_t common = lhs.size < rhs.size ? lhs.size : rhs.size;
        const int result = common == 0 ? 0 : memcmp(lhs.data, rhs.data, common);
        if (result != 0) return result;
        return lhs.size < rhs.size ? -1 : (lhs.size > rhs.size ? 1 : 0);
    }
    const uint8_t* Row(size_t table, uint32_t index, size_t row_size) const {
        return data_ + offsets_[table] + static_cast<size_t>(index) * row_size;
    }
    bool Fail(Error value, Error* output) {
        if (output != nullptr) *output = value;
        Reset();
        return false;
    }
    void Reset() {
        data_ = nullptr;
        size_ = 0;
        string_bytes_ = 0;
        valid_ = false;
        validation_error_ = Error::kRow;
        memset(counts_, 0, sizeof(counts_));
        memset(offsets_, 0, sizeof(offsets_));
    }
    bool ValidateStrings() const {
        uint32_t expected_offset = 0;
        StringRef previous;
        for (uint32_t i = 0; i < counts_[8]; ++i) {
            const uint8_t* row = Row(8, i, binary_format::kStringIndexSize);
            const uint32_t at = Read32(row);
            const uint32_t length = Read32(row + 4);
            if (at != expected_offset || static_cast<uint64_t>(at) + length > string_bytes_) return false;
            StringRef current{reinterpret_cast<const char*>(data_ + offsets_[9] + at), length};
            if (!ValidUtf8(reinterpret_cast<const uint8_t*>(current.data), current.size)
                || (i == 0 && !current.empty())
                || (i != 0 && Compare(previous, current) >= 0)) return false;
            expected_offset += length;
            previous = current;
        }
        return expected_offset == string_bytes_;
    }
    bool ValidateClasses() const {
        for (uint32_t i = 0; i < counts_[6]; ++i) {
            const uint8_t* row = Row(6, i, binary_format::kCharacterClassSize);
            if ((Read32(row + 16) & ~binary_format::kCharacterClassFlagNegated) != 0
                || Read32(row + 20) != 0) return false;
        }
        return true;
    }
    bool ValidatePatterns() const {
        for (uint32_t i = 0; i < counts_[5]; ++i) {
            const uint8_t* token = Row(5, i, binary_format::kPatternTokenSize);
            const uint8_t kind = token[0];
            const uint32_t operand = Read32(token + 4);
            if (token[1] != 0 || Read16(token + 2) != 0 || kind > 5) return false;
            if ((kind == 0 && operand >= counts_[8])
                || (kind == 4 && operand >= counts_[6])
                || ((kind == 1 || kind == 2 || kind == 3 || kind == 5) && operand != 0)) return false;
        }
        for (uint32_t i = 0; i < counts_[4]; ++i) {
            const uint8_t* pattern = Row(4, i, binary_format::kPatternSize);
            const uint32_t first = Read32(pattern);
            const uint16_t count = Read16(pattern + 4);
            const uint16_t components = Read16(pattern + 6);
            const uint32_t literal = Read32(pattern + 8);
            const uint32_t extension = Read32(pattern + 12);
            const uint16_t flags = Read16(pattern + 16);
            if (count == 0 || count > binary_format::kMaxPatternTokens
                || components == 0 || static_cast<uint64_t>(first) + count > counts_[5]
                || (literal != binary_format::kInvalidId && literal >= counts_[8])
                || (extension != binary_format::kInvalidId && extension >= counts_[8])
                || (flags & ~binary_format::kPatternFlagDegenerate) != 0
                || !AllZero(pattern + 18, 6)) return false;
            uint16_t observed_components = 1;
            bool component_has_token = false;
            bool component_globstar = false;
            for (uint32_t j = 0; j < count; ++j) {
                const uint8_t* token = Row(5, first + j, binary_format::kPatternTokenSize);
                if (token[0] == 5) {
                    if (!component_has_token) return false;
                    ++observed_components;
                    component_has_token = false;
                    component_globstar = false;
                } else if (token[0] == 3) {
                    if (component_has_token) return false;
                    component_has_token = true;
                    component_globstar = true;
                } else {
                    if (component_globstar) return false;
                    component_has_token = true;
                }
            }
            if (!component_has_token || observed_components != components) return false;
            const bool degenerate = literal == binary_format::kInvalidId
                && extension == binary_format::kInvalidId;
            if (((flags & binary_format::kPatternFlagDegenerate) != 0) != degenerate) return false;
        }
        return true;
    }
    bool ValidateExceptRefs() const {
        for (uint32_t i = 0; i < counts_[7]; ++i) {
            const uint8_t* row = Row(7, i, binary_format::kSelectorExceptRefSize);
            if (Read32(row) >= counts_[4] || Read32(row + 4) != 0) return false;
        }
        return true;
    }
    void DecodePackage(uint32_t index, PackageRef* output) const {
        const uint8_t* row = Row(0, index, binary_format::kPackageSize);
        output->index = index;
        output->name_id = Read32(row + 4);
        output->first_scope = Read32(row + 8);
        output->user_count = Read16(row + 12);
        output->process_count = Read16(row + 14);
        output->first_selector = Read32(row + 16);
        output->selector_count = Read32(row + 20);
        output->first_action = Read32(row + 24);
        output->action_count = Read32(row + 28);
        output->plan_generation = Read64(row + 32);
        output->flags = Read32(row + 56);
    }
    void DecodeSelector(uint32_t index, SelectorRef* output) const {
        const uint8_t* row = Row(2, index, binary_format::kSelectorSize);
        output->index = index;
        output->root_id = Read32(row);
        output->base_pattern_id = Read32(row + 4);
        output->first_except = Read32(row + 8);
        output->first_action = Read32(row + 12);
        output->except_count = Read16(row + 16);
        output->action_count = Read16(row + 18);
        output->match_kind = row[24];
        output->object_type = row[25];
        output->first_literal_id = Read32(row + 28);
        output->extension_id = Read32(row + 32);
    }
    void DecodeAction(uint32_t index, ActionRef* output) const {
        const uint8_t* row = Row(3, index, binary_format::kActionSize);
        output->index = index;
        output->selector_id = Read32(row);
        output->target_id = Read32(row + 4);
        output->rule_id = Read64(row + 8);
        output->required_capabilities = Read64(row + 16);
        output->required_operations = Read64(row + 24);
        output->priority = static_cast<int32_t>(Read32(row + 32));
        output->options = Read32(row + 36);
        output->kind = row[40];
        output->domain = row[41];
        output->preserve = row[42];
        output->collision = row[43];
        output->reverse = row[44];
    }
    bool ValidatePackages() {
        uint32_t previous_hash = 0;
        StringRef previous_name;
        bool have_previous = false;
        for (uint32_t i = 0; i < counts_[0]; ++i) {
            const uint8_t* row = Row(0, i, binary_format::kPackageSize);
            PackageRef package;
            DecodePackage(i, &package);
            StringRef name;
            if (!StringAt(package.name_id, &name) || name.empty()
                || Read32(row) != binary_format::PackageNameHash(name.data, name.size)
                || !AllZero(row + 60, 4)
                || (package.flags & ~(binary_format::kPackageFlagAllUsers
                    | binary_format::kPackageFlagAllProcesses
                    | binary_format::kPackageFlagProviderEnabled)) != 0
                || package.user_count > binary_format::kMaxUsersPerPackage
                || package.process_count > binary_format::kMaxProcessesPerPackage
                || package.selector_count == 0
                || package.selector_count > binary_format::kMaxSelectorsPerPackage
                || package.action_count == 0
                || package.action_count > binary_format::kMaxActionsPerPackage
                || static_cast<uint64_t>(package.first_scope) + package.user_count
                    + package.process_count > counts_[1]
                || static_cast<uint64_t>(package.first_selector) + package.selector_count > counts_[2]
                || static_cast<uint64_t>(package.first_action) + package.action_count > counts_[3]) {
                validation_error_ = Error::kReference;
                return false;
            }
            const uint32_t hash = Read32(row);
            if (have_previous && (hash < previous_hash
                || (hash == previous_hash && Compare(previous_name, name) >= 0))) {
                validation_error_ = Error::kRow;
                return false;
            }
            if (((package.flags & binary_format::kPackageFlagAllUsers) != 0
                 && package.user_count != 0)
                || ((package.flags & binary_format::kPackageFlagAllProcesses) != 0
                    && package.process_count != 0)
                || !ValidateScopes(package)
                || !ValidatePackageRules(package)) return false;
            previous_hash = hash;
            previous_name = name;
            have_previous = true;
        }
        return true;
    }
    bool ValidateScopes(const PackageRef& package) {
        uint32_t previous_user = 0;
        for (uint32_t i = 0; i < package.user_count; ++i) {
            const uint8_t* row = Row(1, package.first_scope + i,
                                     binary_format::kScopeRefSize);
            const uint32_t user = Read32(row + 4);
            if (row[0] != 0 || row[1] != 0 || Read16(row + 2) != 0
                || (i != 0 && user <= previous_user)) {
                validation_error_ = Error::kScope;
                return false;
            }
            previous_user = user;
        }
        StringRef previous_process;
        for (uint32_t i = 0; i < package.process_count; ++i) {
            const uint8_t* row = Row(1, package.first_scope + package.user_count + i,
                                     binary_format::kScopeRefSize);
            StringRef process;
            if (row[0] != 1 || row[1] != 0 || Read16(row + 2) != 0
                || !StringAt(Read32(row + 4), &process) || process.empty()
                || (i != 0 && Compare(previous_process, process) >= 0)) {
                validation_error_ = Error::kScope;
                return false;
            }
            previous_process = process;
        }
        return true;
    }
    bool ValidatePackageRules(const PackageRef& package) {
        uint32_t expected_action = package.first_action;
        for (uint32_t i = 0; i < package.selector_count; ++i) {
            const uint32_t selector_index = package.first_selector + i;
            const uint8_t* row = Row(2, selector_index, binary_format::kSelectorSize);
            SelectorRef selector;
            DecodeSelector(selector_index, &selector);
            if (selector.root_id >= counts_[8] || selector.match_kind > 1
                || selector.object_type > 2 || selector.first_action != expected_action
                || static_cast<uint64_t>(selector.first_action) + selector.action_count
                    > package.first_action + package.action_count
                || static_cast<uint64_t>(selector.first_except) + selector.except_count > counts_[7]
                || selector.except_count > binary_format::kMaxExceptPerSelector
                || !AllZero(row + 22, 2) || !AllZero(row + 26, 2)
                || !AllZero(row + 36, 4)) {
                validation_error_ = Error::kReference;
                return false;
            }
            StringRef root;
            if (!StringAt(selector.root_id, &root) || root.empty()) {
                validation_error_ = Error::kString;
                return false;
            }
            uint16_t depth = 1;
            for (uint32_t j = 0; j < root.size; ++j) if (root.data[j] == '/') ++depth;
            if (Read16(row + 20) != depth) {
                validation_error_ = Error::kRow;
                return false;
            }
            if ((selector.match_kind == 0
                 && (selector.base_pattern_id != binary_format::kInvalidId
                     || selector.except_count != 0
                     || Read32(row + 28) != binary_format::kInvalidId
                     || Read32(row + 32) != binary_format::kInvalidId))
                || (selector.match_kind == 1 && selector.base_pattern_id >= counts_[4])) {
                validation_error_ = Error::kReference;
                return false;
            }
            uint32_t previous_except = 0;
            for (uint32_t j = 0; j < selector.except_count; ++j) {
                const uint32_t pattern = Read32(Row(7, selector.first_except + j,
                    binary_format::kSelectorExceptRefSize));
                if (pattern == selector.base_pattern_id || (j != 0 && pattern <= previous_except)) {
                    validation_error_ = Error::kRow;
                    return false;
                }
                previous_except = pattern;
            }
            for (uint32_t j = 0; j < selector.action_count; ++j) {
                const uint32_t action_index = selector.first_action + j;
                const uint8_t* action_row = Row(3, action_index, binary_format::kActionSize);
                ActionRef action;
                DecodeAction(action_index, &action);
                const bool has_target = action.kind == 1 || action.kind == 3;
                const bool path_action = action.kind == 0 || action.kind == 1;
                const bool event_action = action.kind == 2 || action.kind == 3;
                if (action.selector_id != selector_index || action.kind > 3
                    || action.domain > 4 || action.preserve > 1
                    || action.collision > 1 || action.reverse > 2
                    || !AllZero(action_row + 45, 3)
                    || (action.required_capabilities & ~binary_format::kKnownCapabilityMask) != 0
                    || (action.required_operations & ~binary_format::kKnownOperationMask) != 0
                    || (has_target != (action.target_id != binary_format::kInvalidId))
                    || (action.target_id != binary_format::kInvalidId
                        && action.target_id >= counts_[8])
                    || (action.domain == 0 && (!path_action || selector.match_kind != 0))
                    || (action.domain == 4 && !event_action)
                    || (action.domain != 4 && !path_action)) {
                    validation_error_ = Error::kReference;
                    return false;
                }
            }
            expected_action += selector.action_count;
        }
        if (expected_action != package.first_action + package.action_count) {
            validation_error_ = Error::kReference;
            return false;
        }
        return true;
    }

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    uint32_t counts_[9]{};
    uint32_t offsets_[10]{};
    uint32_t string_bytes_ = 0;
    bool valid_ = false;
    Error validation_error_ = Error::kRow;
};

}  // namespace pathguard::policy_v6_view
