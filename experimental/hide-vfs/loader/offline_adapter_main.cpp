#include "offline_module_adapter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool ReadBinary(const std::filesystem::path& path,
                std::vector<std::uint8_t>* bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    bytes->assign(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool ReadText(const std::filesystem::path& path, std::string* text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    text->assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool WriteBinary(const std::filesystem::path& path,
                 std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: pathguard_lkm_offline_adapter "
                     "<module.ko> <kallsyms.txt> <required-vermagic> <output.ko>\n";
        return 2;
    }

    std::vector<std::uint8_t> module;
    std::string symbol_text;
    if (!ReadBinary(argv[1], &module) || !ReadText(argv[2], &symbol_text)) {
        std::cerr << "failed to read input file\n";
        return 2;
    }

    pathguard::hide::lkm::KernelSymbolMap symbols;
    std::string parse_error;
    if (!pathguard::hide::lkm::ParseKernelSymbols(symbol_text, &symbols,
                                                   &parse_error)) {
        std::cerr << parse_error << '\n';
        return 2;
    }

    auto result = pathguard::hide::lkm::AdaptModuleOffline(
        module, symbols, argv[3]);
    if (!result.ok()) {
        std::cerr << result.error.message << '\n';
        return 1;
    }
    if (!WriteBinary(argv[4], result.image)) {
        std::cerr << "failed to write adapted module\n";
        return 2;
    }

    std::cout << "vermagic_before=" << result.report.original_vermagic << '\n'
              << "vermagic_after=" << result.report.effective_vermagic << '\n'
              << "relocated=" << result.report.relocated_symbols.size() << '\n';
    for (const auto& symbol : result.report.relocated_symbols) {
        std::cout << "symbol=" << symbol.name << " index=" << symbol.symbol_index
                  << " address=0x" << std::hex << symbol.address << std::dec
                  << '\n';
    }
    return 0;
}
