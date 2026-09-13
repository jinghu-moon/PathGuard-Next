#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pathguard::hide::lkm {

enum class AdapterErrorCode {
    kNone,
    kInvalidElf,
    kUnsupportedElf,
    kMalformedElf,
    kMissingSection,
    kInvalidVersions,
    kMissingSymbol,
    kInvalidSymbolAddress,
    kInvalidVermagic,
};

struct AdapterError {
    AdapterErrorCode code = AdapterErrorCode::kNone;
    std::string message;
};

struct RelocatedSymbol {
    std::string name;
    std::size_t symbol_index = 0;
    std::uint64_t address = 0;
};

struct AdapterReport {
    std::string original_vermagic;
    std::string effective_vermagic;
    std::vector<RelocatedSymbol> relocated_symbols;
};

struct AdapterResult {
    std::vector<std::uint8_t> image;
    AdapterReport report;
    AdapterError error;

    [[nodiscard]] bool ok() const noexcept {
        return error.code == AdapterErrorCode::kNone;
    }
};

using KernelSymbolMap = std::unordered_map<std::string, std::uint64_t>;

[[nodiscard]] AdapterResult AdaptModuleOffline(
    std::span<const std::uint8_t> module,
    const KernelSymbolMap& kernel_symbols,
    std::string_view required_vermagic);

[[nodiscard]] bool ParseKernelSymbols(std::string_view text,
                                      KernelSymbolMap* symbols,
                                      std::string* error);

[[nodiscard]] bool ExtractRequiredVermagic(std::string_view kernel_log,
                                           std::string* vermagic);

}  // namespace pathguard::hide::lkm
