#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "offline_module_adapter.h"

namespace {

void Write16(std::vector<std::uint8_t>* bytes, std::size_t offset,
             std::uint16_t value) {
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void Write32(std::vector<std::uint8_t>* bytes, std::size_t offset,
             std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        (*bytes)[offset + index] =
            static_cast<std::uint8_t>(value >> (index * 8));
    }
}

void Write64(std::vector<std::uint8_t>* bytes, std::size_t offset,
             std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        (*bytes)[offset + index] =
            static_cast<std::uint8_t>(value >> (index * 8));
    }
}

std::size_t Align(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

std::vector<std::uint8_t> BuildModule() {
    constexpr std::size_t kHeaderSize = 64;
    constexpr std::size_t kSectionSize = 64;
    constexpr std::size_t kSectionCount = 6;
    constexpr std::size_t kSectionTableOffset = 0x400;
    std::vector<std::uint8_t> module(kSectionTableOffset +
                                     kSectionSize * kSectionCount, 0);

    module[0] = 0x7f;
    module[1] = 'E';
    module[2] = 'L';
    module[3] = 'F';
    module[4] = 2;
    module[5] = 1;
    module[6] = 1;
    Write16(&module, 16, 1);
    Write16(&module, 18, 183);
    Write64(&module, 40, kSectionTableOffset);
    Write16(&module, 52, kHeaderSize);
    Write16(&module, 58, kSectionSize);
    Write16(&module, 60, kSectionCount);
    Write16(&module, 62, 4);

    const std::string strtab("\0init_uts_ns\0", 13);
    const std::string modinfo("license=GPL\0vermagic=old magic\0", 31);
    const std::string shstrtab(
        "\0.symtab\0.strtab\0.modinfo\0.shstrtab\0__versions\0", 47);
    const std::size_t symtab_offset = 0x80;
    const std::size_t strtab_offset = Align(symtab_offset + 48, 8);
    const std::size_t modinfo_offset = Align(strtab_offset + strtab.size(), 8);
    const std::size_t shstrtab_offset = Align(modinfo_offset + modinfo.size(), 8);
    assert(shstrtab_offset + shstrtab.size() < kSectionTableOffset);
    Write32(&module, symtab_offset + 24, 1);
    Write64(&module, symtab_offset + 24 + 8, 0);
    Write16(&module, symtab_offset + 24 + 6, 0);
    std::memcpy(module.data() + strtab_offset, strtab.data(), strtab.size());
    std::memcpy(module.data() + modinfo_offset, modinfo.data(), modinfo.size());
    std::memcpy(module.data() + shstrtab_offset, shstrtab.data(), shstrtab.size());

    auto section = [&](std::size_t index, std::uint32_t name, std::uint32_t type,
                       std::uint64_t offset, std::uint64_t size,
                       std::uint32_t link, std::uint64_t alignment,
                       std::uint64_t entry_size) {
        const std::size_t base = kSectionTableOffset + index * kSectionSize;
        Write32(&module, base, name);
        Write32(&module, base + 4, type);
        Write64(&module, base + 24, offset);
        Write64(&module, base + 32, size);
        Write32(&module, base + 40, link);
        Write64(&module, base + 48, alignment);
        Write64(&module, base + 56, entry_size);
    };
    section(1, 1, 2, symtab_offset, 48, 2, 8, 24);
    section(2, 9, 3, strtab_offset, strtab.size(), 0, 1, 0);
    section(3, 17, 1, modinfo_offset, modinfo.size(), 0, 1, 0);
    section(4, 26, 3, shstrtab_offset, shstrtab.size(), 0, 1, 0);
    section(5, 36, 1, shstrtab_offset + shstrtab.size(), 0, 0, 8, 0);
    return module;
}

std::uint16_t Read16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1]) << 8;
}

std::uint64_t Read64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index])
                 << (index * 8);
    }
    return value;
}

}  // namespace

int main() {
    using namespace pathguard::hide::lkm;

    const auto original = BuildModule();
    const KernelSymbolMap symbols{{"init_uts_ns", 0xffff000012345678ULL}};
    const auto adapted = AdaptModuleOffline(original, symbols, "6.12.23-device");
    if (!adapted.ok()) {
        std::cerr << adapted.error.message << '\n';
    }
    assert(adapted.ok());
    assert(original == BuildModule());
    assert(adapted.report.original_vermagic == "old magic");
    assert(adapted.report.effective_vermagic == "6.12.23-device");
    assert(adapted.report.relocated_symbols.size() == 1);
    assert(adapted.report.relocated_symbols.front().name == "init_uts_ns");
    assert(Read16(adapted.image, 0x80 + 24 + 6) == 0xfff1);
    assert(Read64(adapted.image, 0x80 + 24 + 8) == 0xffff000012345678ULL);
    assert(adapted.image != original);

    const auto missing = AdaptModuleOffline(original, {}, "6.12.23-device");
    assert(!missing.ok());
    assert(missing.error.code == AdapterErrorCode::kMissingSymbol);

    const auto malformed = AdaptModuleOffline(
        std::vector<std::uint8_t>{0x7f, 'E', 'L', 'F'}, symbols,
        "6.12.23-device");
    assert(!malformed.ok());
    assert(malformed.error.code == AdapterErrorCode::kInvalidElf);

    auto versioned_module = original;
    Write64(&versioned_module, 0x400 + 5 * 64 + 32, 8);
    const auto versioned =
        AdaptModuleOffline(versioned_module, symbols, "6.12.23-device");
    assert(!versioned.ok());
    assert(versioned.error.code == AdapterErrorCode::kInvalidVersions);

    KernelSymbolMap parsed;
    std::string parse_error;
    assert(ParseKernelSymbols("ffff000012345678 T init_uts_ns\n"
                              "ffff000012345679 T foo.llvm.123\n",
                              &parsed, &parse_error));
    assert(parsed.at("init_uts_ns") == 0xffff000012345678ULL);
    assert(parsed.at("foo") == 0xffff000012345679ULL);
    assert(!ParseKernelSymbols("ffff000000000001 T duplicate\n"
                               "ffff000000000002 T duplicate.llvm.123\n",
                               &parsed, &parse_error));
    assert(parse_error == "ambiguous kallsyms symbol: duplicate");

    std::string required_vermagic;
    assert(ExtractRequiredVermagic(
        "6,1,1;-;pathguard_probe: version magic 'old magic' should be "
        "'6.12.23-device SMP preempt mod_unload aarch64'\n",
        &required_vermagic));
    assert(required_vermagic ==
           "6.12.23-device SMP preempt mod_unload aarch64");
    assert(!ExtractRequiredVermagic("unrelated kernel log", &required_vermagic));
    return 0;
}
