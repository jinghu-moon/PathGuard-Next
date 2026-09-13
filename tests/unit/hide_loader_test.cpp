#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "offline_module_adapter.h"
#include "hide_loader_fixture.h"

namespace {

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
    using pathguard::hide::test::BuildModule;
    using pathguard::hide::test::Write64;

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
    assert(ParseKernelSymbols("ffff000012345678 T init_uts_ns\n"
                              "ffff000012345680 t module_symbol [kernelsu]\n"
                              "ffff000012345681 T after_module\n",
                              &parsed, &parse_error));
    assert(parsed.at("module_symbol") == 0xffff000012345680ULL);
    assert(parsed.at("after_module") == 0xffff000012345681ULL);
    assert(!ParseKernelSymbols("ffff000012345680 t broken [module] trailing\n",
                               &parsed, &parse_error));
    assert(parse_error == "invalid kallsyms fields at line 1");
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
