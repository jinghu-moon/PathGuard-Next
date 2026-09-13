#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pathguard::hide::test {

inline void Write16(std::vector<std::uint8_t>* bytes, std::size_t offset,
                    std::uint16_t value) {
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

inline void Write32(std::vector<std::uint8_t>* bytes, std::size_t offset,
                    std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        (*bytes)[offset + index] =
            static_cast<std::uint8_t>(value >> (index * 8));
    }
}

inline void Write64(std::vector<std::uint8_t>* bytes, std::size_t offset,
                    std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        (*bytes)[offset + index] =
            static_cast<std::uint8_t>(value >> (index * 8));
    }
}

inline std::size_t Align(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

inline std::vector<std::uint8_t> BuildModule() {
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
    std::memcpy(module.data() + shstrtab_offset, shstrtab.data(),
                shstrtab.size());

    auto section = [&](std::size_t index, std::uint32_t name,
                       std::uint32_t type, std::uint64_t offset,
                       std::uint64_t size, std::uint32_t link,
                       std::uint64_t alignment, std::uint64_t entry_size) {
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

}  // namespace pathguard::hide::test
