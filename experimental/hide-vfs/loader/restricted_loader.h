#pragma once

#include "offline_module_adapter.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pathguard::hide::lkm {

enum class LoaderMode {
    kPrepareOnly,
    kLoad,
};

struct LoaderOptions {
    LoaderMode mode = LoaderMode::kPrepareOnly;
    std::string module_path;
    std::string kallsyms_path = "/proc/kallsyms";
    std::vector<std::string> kmsg_paths = {"/dev/kmsg", "/kmsg"};
    std::string required_vermagic;
};

struct LoaderReport {
    std::size_t module_size = 0;
    std::size_t kernel_symbol_count = 0;
    std::size_t relocated_symbol_count = 0;
    std::string vermagic_source;
    std::string original_vermagic;
    std::string effective_vermagic;
};

class KernelLogCursor {
public:
    KernelLogCursor();
    ~KernelLogCursor();
    KernelLogCursor(KernelLogCursor&&) noexcept;
    KernelLogCursor& operator=(KernelLogCursor&&) noexcept;
    KernelLogCursor(const KernelLogCursor&) = delete;
    KernelLogCursor& operator=(const KernelLogCursor&) = delete;

    [[nodiscard]] static bool Open(std::span<const std::string> paths,
                                   KernelLogCursor* cursor,
                                   std::string* source, std::string* error);
    [[nodiscard]] bool ReadNew(std::string* log, std::string* error);

private:
    explicit KernelLogCursor(int fd);
    int fd_ = -1;
};

struct LoaderResult {
    LoaderReport report;
    AdapterError error;

    [[nodiscard]] bool ok() const noexcept {
        return error.code == AdapterErrorCode::kNone;
    }
};

[[nodiscard]] bool ParseLoaderArguments(int argc, char* const argv[],
                                        LoaderOptions* options,
                                        std::string* error);

[[nodiscard]] bool ReadBinaryFile(std::string_view path,
                                  std::vector<std::uint8_t>* bytes,
                                  std::string* error);

[[nodiscard]] bool ReadTextFile(std::string_view path, std::string* text,
                                std::string* error);

[[nodiscard]] bool ReadKernelLog(std::span<const std::string> paths,
                                 std::string* log, std::string* source,
                                 std::string* error);

[[nodiscard]] LoaderResult PrepareModule(const LoaderOptions& options);

}  // namespace pathguard::hide::lkm
