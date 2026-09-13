#include "offline_module_adapter.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace pathguard::hide::lkm {
namespace {

constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kSectionHeaderSize = 64;
constexpr std::size_t kSymbolSize = 24;
constexpr std::uint16_t kElfTypeRelocatable = 1;
constexpr std::uint16_t kMachineAarch64 = 183;
constexpr std::uint32_t kSectionTypeSymbolTable = 2;
constexpr std::uint16_t kSectionUndefined = 0;
constexpr std::uint16_t kSectionAbsolute = 0xfff1;
constexpr std::size_t kMaxVermagicSize = 512;

struct Section {
    std::size_t header_offset = 0;
    std::string name;
    std::uint32_t type = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t link = 0;
    std::uint64_t alignment = 0;
    std::uint64_t entry_size = 0;
};

struct ParsedElf {
    std::vector<Section> sections;
    std::size_t symbol_table_index = 0;
    std::size_t modinfo_index = 0;
    std::size_t versions_index = 0;
};

[[nodiscard]] bool RangeFits(std::size_t size, std::uint64_t offset,
                             std::uint64_t length) {
    return offset <= size && length <= size - static_cast<std::size_t>(offset);
}

[[nodiscard]] bool ReadU16(std::span<const std::uint8_t> bytes,
                           std::size_t offset, std::uint16_t* value) {
    if (!RangeFits(bytes.size(), offset, sizeof(*value))) {
        return false;
    }
    *value = static_cast<std::uint16_t>(bytes[offset]) |
             static_cast<std::uint16_t>(bytes[offset + 1]) << 8;
    return true;
}

[[nodiscard]] bool ReadU32(std::span<const std::uint8_t> bytes,
                           std::size_t offset, std::uint32_t* value) {
    if (!RangeFits(bytes.size(), offset, sizeof(*value))) {
        return false;
    }
    *value = static_cast<std::uint32_t>(bytes[offset]) |
             static_cast<std::uint32_t>(bytes[offset + 1]) << 8 |
             static_cast<std::uint32_t>(bytes[offset + 2]) << 16 |
             static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
    return true;
}

[[nodiscard]] bool ReadU64(std::span<const std::uint8_t> bytes,
                           std::size_t offset, std::uint64_t* value) {
    if (!RangeFits(bytes.size(), offset, sizeof(*value))) {
        return false;
    }
    *value = 0;
    for (std::size_t index = 0; index < sizeof(*value); ++index) {
        *value |= static_cast<std::uint64_t>(bytes[offset + index])
                  << (index * 8);
    }
    return true;
}

void WriteU16(std::vector<std::uint8_t>* bytes, std::size_t offset,
              std::uint16_t value) {
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void WriteU64(std::vector<std::uint8_t>* bytes, std::size_t offset,
              std::uint64_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        (*bytes)[offset + index] =
            static_cast<std::uint8_t>(value >> (index * 8));
    }
}

[[nodiscard]] bool CheckedMultiply(std::size_t left, std::size_t right,
                                   std::size_t* result) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    *result = left * right;
    return true;
}

[[nodiscard]] bool CheckedAdd(std::size_t left, std::size_t right,
                              std::size_t* result) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    *result = left + right;
    return true;
}

[[nodiscard]] bool ReadString(std::span<const std::uint8_t> table,
                              std::uint32_t offset, std::string* value) {
    if (offset >= table.size()) {
        return false;
    }
    const auto begin = table.begin() + offset;
    const auto end = std::find(begin, table.end(), 0);
    if (end == table.end()) {
        return false;
    }
    value->assign(reinterpret_cast<const char*>(&*begin),
                  static_cast<std::size_t>(end - begin));
    return true;
}

[[nodiscard]] AdapterResult Fail(AdapterErrorCode code, std::string message) {
    AdapterResult result;
    result.error.code = code;
    result.error.message = std::move(message);
    return result;
}

[[nodiscard]] bool ParseElf(std::span<const std::uint8_t> bytes,
                            ParsedElf* parsed, AdapterError* error) {
    constexpr std::array<std::uint8_t, 4> kMagic{0x7f, 'E', 'L', 'F'};
    if (bytes.size() < kElfHeaderSize ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        *error = {AdapterErrorCode::kInvalidElf, "invalid ELF magic or header"};
        return false;
    }
    if (bytes[4] != 2 || bytes[5] != 1 || bytes[6] != 1) {
        *error = {AdapterErrorCode::kUnsupportedElf,
                  "only little-endian ELF64 is supported"};
        return false;
    }

    std::uint16_t type = 0;
    std::uint16_t machine = 0;
    std::uint64_t section_offset = 0;
    std::uint16_t header_size = 0;
    std::uint16_t section_entry_size = 0;
    std::uint16_t section_count = 0;
    std::uint16_t section_names_index = 0;
    if (!ReadU16(bytes, 16, &type) || !ReadU16(bytes, 18, &machine) ||
        !ReadU64(bytes, 40, &section_offset) ||
        !ReadU16(bytes, 52, &header_size) ||
        !ReadU16(bytes, 58, &section_entry_size) ||
        !ReadU16(bytes, 60, &section_count) ||
        !ReadU16(bytes, 62, &section_names_index)) {
        *error = {AdapterErrorCode::kMalformedElf, "truncated ELF header"};
        return false;
    }
    if (type != kElfTypeRelocatable || machine != kMachineAarch64) {
        *error = {AdapterErrorCode::kUnsupportedElf,
                  "module must be an AArch64 relocatable ELF"};
        return false;
    }
    if (header_size != kElfHeaderSize ||
        section_entry_size != kSectionHeaderSize || section_count == 0 ||
        section_names_index == 0 || section_names_index >= section_count) {
        *error = {AdapterErrorCode::kMalformedElf,
                  "unsupported ELF section table layout"};
        return false;
    }

    std::size_t table_size = 0;
    if (!CheckedMultiply(section_count, kSectionHeaderSize, &table_size) ||
        !RangeFits(bytes.size(), section_offset, table_size)) {
        *error = {AdapterErrorCode::kMalformedElf,
                  "section table is outside the module"};
        return false;
    }

    parsed->sections.clear();
    parsed->sections.resize(section_count);
    for (std::size_t index = 0; index < section_count; ++index) {
        std::size_t relative = 0;
        std::size_t header_offset_value = 0;
        if (!CheckedMultiply(index, kSectionHeaderSize, &relative) ||
            !CheckedAdd(static_cast<std::size_t>(section_offset), relative,
                        &header_offset_value)) {
            *error = {AdapterErrorCode::kMalformedElf,
                      "section header offset overflow"};
            return false;
        }
        Section& section = parsed->sections[index];
        section.header_offset = header_offset_value;
        if (!ReadU32(bytes, header_offset_value + 4, &section.type) ||
            !ReadU64(bytes, header_offset_value + 24, &section.offset) ||
            !ReadU64(bytes, header_offset_value + 32, &section.size) ||
            !ReadU32(bytes, header_offset_value + 40, &section.link) ||
            !ReadU64(bytes, header_offset_value + 48, &section.alignment) ||
            !ReadU64(bytes, header_offset_value + 56, &section.entry_size)) {
            *error = {AdapterErrorCode::kMalformedElf,
                      "truncated section header"};
            return false;
        }
    }

    const Section& names_section = parsed->sections[section_names_index];
    if (!RangeFits(bytes.size(), names_section.offset, names_section.size)) {
        *error = {AdapterErrorCode::kMalformedElf,
                  "section name table is outside the module"};
        return false;
    }
    const auto names = bytes.subspan(static_cast<std::size_t>(names_section.offset),
                                     static_cast<std::size_t>(names_section.size));

    std::size_t symbol_table_count = 0;
    std::size_t modinfo_count = 0;
    std::size_t versions_count = 0;
    for (std::size_t index = 0; index < parsed->sections.size(); ++index) {
        std::uint32_t name_offset = 0;
        if (!ReadU32(bytes, parsed->sections[index].header_offset, &name_offset) ||
            !ReadString(names, name_offset, &parsed->sections[index].name)) {
            *error = {AdapterErrorCode::kMalformedElf,
                      "invalid section name"};
            return false;
        }
        if (parsed->sections[index].type == kSectionTypeSymbolTable &&
            parsed->sections[index].name == ".symtab") {
            parsed->symbol_table_index = index;
            ++symbol_table_count;
        }
        if (parsed->sections[index].name == ".modinfo") {
            parsed->modinfo_index = index;
            ++modinfo_count;
        }
        if (parsed->sections[index].name == "__versions") {
            parsed->versions_index = index;
            ++versions_count;
        }
    }
    if (symbol_table_count != 1) {
        *error = {AdapterErrorCode::kMissingSection,
                  "module must contain exactly one .symtab"};
        return false;
    }
    if (modinfo_count != 1) {
        *error = {AdapterErrorCode::kMissingSection,
                  "module must contain exactly one .modinfo"};
        return false;
    }
    if (versions_count != 1) {
        *error = {AdapterErrorCode::kMissingSection,
                  "module must contain exactly one __versions section"};
        return false;
    }
    if (parsed->sections[parsed->versions_index].size != 0) {
        *error = {AdapterErrorCode::kInvalidVersions,
                  "module __versions section must be empty"};
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseModinfo(std::span<const std::uint8_t> bytes,
                                const Section& section,
                                std::vector<std::string>* entries,
                                std::string* vermagic,
                                AdapterError* error) {
    if (!RangeFits(bytes.size(), section.offset, section.size)) {
        *error = {AdapterErrorCode::kMalformedElf,
                  ".modinfo is outside the module"};
        return false;
    }
    const auto data = bytes.subspan(static_cast<std::size_t>(section.offset),
                                    static_cast<std::size_t>(section.size));
    if (data.empty() || data.back() != 0) {
        *error = {AdapterErrorCode::kMalformedElf,
                  ".modinfo is not NUL terminated"};
        return false;
    }

    std::size_t position = 0;
    std::size_t vermagic_count = 0;
    while (position < data.size()) {
        const auto begin = data.begin() + position;
        const auto end = std::find(begin, data.end(), 0);
        if (end == data.end()) {
            *error = {AdapterErrorCode::kMalformedElf,
                      "unterminated .modinfo entry"};
            return false;
        }
        if (end != begin) {
            std::string entry(reinterpret_cast<const char*>(&*begin),
                              static_cast<std::size_t>(end - begin));
            if (entry.starts_with("vermagic=")) {
                *vermagic = entry.substr(std::strlen("vermagic="));
                ++vermagic_count;
            }
            entries->push_back(std::move(entry));
        }
        position += static_cast<std::size_t>(end - begin) + 1;
    }
    if (vermagic_count != 1) {
        *error = {AdapterErrorCode::kMissingSection,
                  "module must contain exactly one vermagic entry"};
        return false;
    }
    return true;
}

[[nodiscard]] std::string NormalizeKernelSymbol(std::string_view symbol) {
    std::size_t suffix = symbol.size();
    if (const auto dollar = symbol.find('$'); dollar != std::string_view::npos) {
        suffix = std::min(suffix, dollar);
    }
    if (const auto llvm = symbol.find(".llvm."); llvm != std::string_view::npos) {
        suffix = std::min(suffix, llvm);
    }
    return std::string(symbol.substr(0, suffix));
}

}  // namespace

AdapterResult AdaptModuleOffline(std::span<const std::uint8_t> module,
                                 const KernelSymbolMap& kernel_symbols,
                                 std::string_view required_vermagic) {
    if (required_vermagic.empty() ||
        required_vermagic.size() > kMaxVermagicSize ||
        required_vermagic.find('\0') != std::string_view::npos ||
        required_vermagic.find('\n') != std::string_view::npos ||
        required_vermagic.find('\r') != std::string_view::npos) {
        return Fail(AdapterErrorCode::kInvalidVermagic,
                    "required vermagic is empty or invalid");
    }

    ParsedElf elf;
    AdapterError parse_error;
    if (!ParseElf(module, &elf, &parse_error)) {
        return Fail(parse_error.code, std::move(parse_error.message));
    }

    const Section& symbols = elf.sections[elf.symbol_table_index];
    if (symbols.entry_size != kSymbolSize || symbols.size % kSymbolSize != 0 ||
        !RangeFits(module.size(), symbols.offset, symbols.size) ||
        symbols.link >= elf.sections.size()) {
        return Fail(AdapterErrorCode::kMalformedElf,
                    "invalid ELF symbol table");
    }
    const Section& strings = elf.sections[symbols.link];
    if (!RangeFits(module.size(), strings.offset, strings.size)) {
        return Fail(AdapterErrorCode::kMalformedElf,
                    "symbol string table is outside the module");
    }
    const auto string_data = module.subspan(static_cast<std::size_t>(strings.offset),
                                            static_cast<std::size_t>(strings.size));

    struct PendingRelocation {
        std::size_t symbol_index;
        std::size_t symbol_offset;
        std::string name;
        std::uint64_t address;
    };
    std::vector<PendingRelocation> pending;
    const std::size_t symbol_count =
        static_cast<std::size_t>(symbols.size / kSymbolSize);
    for (std::size_t index = 1; index < symbol_count; ++index) {
        const std::size_t symbol_offset =
            static_cast<std::size_t>(symbols.offset) + index * kSymbolSize;
        std::uint32_t name_offset = 0;
        std::uint16_t section_index = 0;
        if (!ReadU32(module, symbol_offset, &name_offset) ||
            !ReadU16(module, symbol_offset + 6, &section_index)) {
            return Fail(AdapterErrorCode::kMalformedElf,
                        "truncated ELF symbol");
        }
        if (section_index != kSectionUndefined || name_offset == 0) {
            continue;
        }
        std::string name;
        if (!ReadString(string_data, name_offset, &name) || name.empty()) {
            return Fail(AdapterErrorCode::kMalformedElf,
                        "invalid undefined symbol name");
        }
        const auto found = kernel_symbols.find(name);
        if (found == kernel_symbols.end()) {
            return Fail(AdapterErrorCode::kMissingSymbol,
                        "kernel symbol is unavailable: " + name);
        }
        if (found->second == 0) {
            return Fail(AdapterErrorCode::kInvalidSymbolAddress,
                        "kernel symbol has a zero address: " + name);
        }
        pending.push_back({index, symbol_offset, std::move(name), found->second});
    }

    std::vector<std::string> modinfo_entries;
    std::string original_vermagic;
    const Section& modinfo = elf.sections[elf.modinfo_index];
    if (!ParseModinfo(module, modinfo, &modinfo_entries, &original_vermagic,
                      &parse_error)) {
        return Fail(parse_error.code, std::move(parse_error.message));
    }

    std::vector<std::uint8_t> replacement;
    for (const std::string& entry : modinfo_entries) {
        const std::string_view selected = entry.starts_with("vermagic=")
                                              ? required_vermagic
                                              : std::string_view(entry);
        if (entry.starts_with("vermagic=")) {
            constexpr std::string_view kPrefix = "vermagic=";
            replacement.insert(replacement.end(), kPrefix.begin(), kPrefix.end());
        }
        replacement.insert(replacement.end(), selected.begin(), selected.end());
        replacement.push_back(0);
    }

    const std::size_t alignment =
        static_cast<std::size_t>(std::max<std::uint64_t>(modinfo.alignment, 1));
    if ((alignment & (alignment - 1)) != 0) {
        return Fail(AdapterErrorCode::kMalformedElf,
                    "invalid .modinfo alignment");
    }
    std::size_t aligned_offset = 0;
    if (!CheckedAdd(module.size(), alignment - 1, &aligned_offset)) {
        return Fail(AdapterErrorCode::kMalformedElf,
                    ".modinfo alignment overflow");
    }
    aligned_offset &= ~(alignment - 1);
    std::size_t final_size = 0;
    if (!CheckedAdd(aligned_offset, replacement.size(), &final_size)) {
        return Fail(AdapterErrorCode::kMalformedElf,
                    ".modinfo replacement overflow");
    }

    AdapterResult result;
    result.image.assign(module.begin(), module.end());
    result.image.resize(aligned_offset, 0);
    result.image.insert(result.image.end(), replacement.begin(), replacement.end());
    if (result.image.size() != final_size) {
        return Fail(AdapterErrorCode::kMalformedElf,
                    "unexpected .modinfo replacement size");
    }

    for (const PendingRelocation& relocation : pending) {
        WriteU16(&result.image, relocation.symbol_offset + 6, kSectionAbsolute);
        WriteU64(&result.image, relocation.symbol_offset + 8,
                 relocation.address);
        result.report.relocated_symbols.push_back(
            {relocation.name, relocation.symbol_index, relocation.address});
    }
    WriteU64(&result.image, modinfo.header_offset + 24, aligned_offset);
    WriteU64(&result.image, modinfo.header_offset + 32, replacement.size());
    result.report.original_vermagic = std::move(original_vermagic);
    result.report.effective_vermagic = std::string(required_vermagic);
    return result;
}

bool ParseKernelSymbols(std::string_view text, KernelSymbolMap* symbols,
                        std::string* error) {
    if (symbols == nullptr || error == nullptr) {
        return false;
    }
    symbols->clear();
    error->clear();

    std::istringstream input{std::string(text)};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::istringstream fields(line);
        std::string address_text;
        std::string type;
        std::string name;
        std::string extra;
        if (!(fields >> address_text >> type >> name)) {
            continue;
        }
        if (fields >> extra) {
            std::string trailing;
            if (extra.size() < 3 || extra.front() != '[' ||
                extra.back() != ']' || fields >> trailing) {
                *error = "invalid kallsyms fields at line " +
                         std::to_string(line_number);
                symbols->clear();
                return false;
            }
        }

        std::uint64_t address = 0;
        const auto parsed = std::from_chars(address_text.data(),
                                            address_text.data() + address_text.size(),
                                            address, 16);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != address_text.data() + address_text.size()) {
            *error = "invalid kallsyms address at line " +
                     std::to_string(line_number);
            symbols->clear();
            return false;
        }
        if (address == 0) {
            continue;
        }
        const std::string normalized = NormalizeKernelSymbol(name);
        const auto [iterator, inserted] = symbols->emplace(normalized, address);
        if (!inserted && iterator->second != address) {
            *error = "ambiguous kallsyms symbol: " + normalized;
            symbols->clear();
            return false;
        }
    }
    return true;
}

bool ExtractRequiredVermagic(std::string_view kernel_log,
                             std::string* vermagic) {
    if (vermagic == nullptr) {
        return false;
    }
    vermagic->clear();
    constexpr std::string_view kPrefix = "version magic '";
    constexpr std::string_view kSeparator = "' should be '";

    std::size_t end = kernel_log.size();
    while (end > 0) {
        const std::size_t line_start = kernel_log.rfind('\n', end - 1);
        const std::size_t start =
            line_start == std::string_view::npos ? 0 : line_start + 1;
        std::string_view line = kernel_log.substr(start, end - start);
        if (const auto semicolon = line.find(';');
            semicolon != std::string_view::npos) {
            line.remove_prefix(semicolon + 1);
        }

        const auto prefix = line.find(kPrefix);
        if (prefix != std::string_view::npos) {
            const std::size_t provided_start = prefix + kPrefix.size();
            const auto separator = line.find(kSeparator, provided_start);
            if (separator != std::string_view::npos) {
                const std::size_t required_start = separator + kSeparator.size();
                const auto quote = line.find('\'', required_start);
                if (quote != std::string_view::npos && quote > required_start) {
                    const std::string_view required =
                        line.substr(required_start, quote - required_start);
                    if (required.size() <= kMaxVermagicSize &&
                        required.find('\r') == std::string_view::npos) {
                        *vermagic = std::string(required);
                        return true;
                    }
                }
            }
        }
        if (start == 0) {
            break;
        }
        end = start - 1;
    }
    return false;
}

}  // namespace pathguard::hide::lkm
