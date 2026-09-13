#include "restricted_loader.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace pathguard::hide::lkm {
namespace {

constexpr std::size_t kReadChunkSize = 16U * 1024U;
constexpr std::size_t kMaxInputSize = 128U * 1024U * 1024U;

#if defined(_WIN32)
[[nodiscard]] int OpenReadOnly(const char* path) {
    return _open(path, _O_RDONLY | _O_BINARY);
}

[[nodiscard]] int ReadBytes(int fd, char* buffer, std::size_t size) {
    return _read(fd, buffer, static_cast<unsigned int>(size));
}

void CloseFile(int fd) {
    _close(fd);
}
#else
[[nodiscard]] int OpenReadOnly(const char* path) {
    return open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
}

[[nodiscard]] ssize_t ReadBytes(int fd, char* buffer, std::size_t size) {
    return read(fd, buffer, size);
}

void CloseFile(int fd) {
    close(fd);
}
#endif

[[nodiscard]] AdapterError MakeError(AdapterErrorCode code,
                                     std::string message) {
    return AdapterError{code, std::move(message)};
}

[[nodiscard]] bool IsOption(std::string_view value, std::string_view option) {
    return value == option;
}

[[nodiscard]] bool ReadFd(int fd, std::string* output, std::string* error) {
    output->clear();
    std::vector<char> buffer(kReadChunkSize);
    for (;;) {
        const auto count = ReadBytes(fd, buffer.data(), buffer.size());
        if (count > 0) {
            if (output->size() > kMaxInputSize -
                                  static_cast<std::size_t>(count)) {
                *error = "kernel log exceeds size limit";
                return false;
            }
            output->append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        *error = "read failed: " + std::string(std::strerror(errno));
        return false;
    }
}

[[nodiscard]] bool ReadKernelLogPath(std::string_view path, std::string* log,
                                     std::string* error) {
    const std::string path_string(path);
    const int fd = OpenReadOnly(path_string.c_str());
    if (fd < 0) {
        *error = "open failed: " + std::string(std::strerror(errno));
        return false;
    }
    const bool ok = ReadFd(fd, log, error);
    CloseFile(fd);
    return ok;
}

}  // namespace

KernelLogCursor::KernelLogCursor() = default;

KernelLogCursor::KernelLogCursor(int fd) : fd_(fd) {}

KernelLogCursor::~KernelLogCursor() {
    if (fd_ >= 0) {
        CloseFile(fd_);
    }
}

KernelLogCursor::KernelLogCursor(KernelLogCursor&& other) noexcept
    : fd_(other.fd_) {
    other.fd_ = -1;
}

KernelLogCursor& KernelLogCursor::operator=(KernelLogCursor&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            CloseFile(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool KernelLogCursor::Open(std::span<const std::string> paths,
                           KernelLogCursor* cursor, std::string* source,
                           std::string* error) {
    if (cursor == nullptr || source == nullptr || error == nullptr) {
        return false;
    }
    source->clear();
    error->clear();
    std::string last_error;
    for (const std::string& path : paths) {
        const int fd = OpenReadOnly(path.c_str());
        if (fd < 0) {
            last_error = "open failed: " + std::string(std::strerror(errno));
            continue;
        }
        std::string discarded;
        if (!ReadFd(fd, &discarded, &last_error)) {
            CloseFile(fd);
            continue;
        }
        *cursor = KernelLogCursor(fd);
        *source = path;
        return true;
    }
    *error = last_error.empty() ? "no kernel log path was provided" : last_error;
    return false;
}

bool KernelLogCursor::ReadNew(std::string* log, std::string* error) {
    if (log == nullptr || error == nullptr) {
        return false;
    }
    if (fd_ < 0) {
        *error = "kernel log cursor is not open";
        return false;
    }
    return ReadFd(fd_, log, error);
}

bool ParseLoaderArguments(int argc, char* const argv[], LoaderOptions* options,
                          std::string* error) {
    if (options == nullptr || error == nullptr || argc < 1) {
        return false;
    }
    *options = LoaderOptions{};
    error->clear();
    bool custom_kmsg_path = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (IsOption(argument, "--prepare-only")) {
            options->mode = LoaderMode::kPrepareOnly;
            continue;
        }
        if (IsOption(argument, "--load")) {
            options->mode = LoaderMode::kLoad;
            continue;
        }
        auto consume_value = [&](std::string_view option,
                                 std::string* destination) -> bool {
            if (index + 1 >= argc || argv[index + 1][0] == '-') {
                *error = "missing value for " + std::string(option);
                return false;
            }
            *destination = argv[++index];
            return true;
        };
        if (IsOption(argument, "--module")) {
            if (!consume_value(argument, &options->module_path)) {
                return false;
            }
            continue;
        }
        if (IsOption(argument, "--kallsyms")) {
            if (!consume_value(argument, &options->kallsyms_path)) {
                return false;
            }
            continue;
        }
        if (IsOption(argument, "--kmsg")) {
            std::string path;
            if (!consume_value(argument, &path)) {
                return false;
            }
            if (!custom_kmsg_path) {
                options->kmsg_paths.clear();
                custom_kmsg_path = true;
            }
            options->kmsg_paths.push_back(std::move(path));
            continue;
        }
        if (IsOption(argument, "--vermagic")) {
            if (!consume_value(argument, &options->required_vermagic)) {
                return false;
            }
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            *error = "help";
            return false;
        }
        if (error->empty()) {
            *error = "unknown argument: " + std::string(argument);
        }
        return false;
    }
    if (options->module_path.empty()) {
        *error = "--module is required";
        return false;
    }
    return true;
}

bool ReadBinaryFile(std::string_view path, std::vector<std::uint8_t>* bytes,
                    std::string* error) {
    if (bytes == nullptr || error == nullptr) {
        return false;
    }
    error->clear();
    std::ifstream input(std::string(path), std::ios::binary);
    if (!input) {
        *error = "failed to open module: " + std::string(path);
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) > kMaxInputSize) {
        *error = "module is missing or exceeds size limit";
        return false;
    }
    input.seekg(0, std::ios::beg);
    bytes->assign(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        *error = "failed to read module: " + std::string(path);
        bytes->clear();
        return false;
    }
    return true;
}

bool ReadTextFile(std::string_view path, std::string* text, std::string* error) {
    if (text == nullptr || error == nullptr) {
        return false;
    }
    error->clear();
    std::ifstream input(std::string(path), std::ios::binary);
    if (!input) {
        *error = "failed to open text file: " + std::string(path);
        return false;
    }
    text->clear();
    std::vector<char> buffer(kReadChunkSize);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            if (text->size() >
                kMaxInputSize - static_cast<std::size_t>(count)) {
                *error = "text file exceeds size limit: " + std::string(path);
                text->clear();
                return false;
            }
            text->append(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    if (!input.eof()) {
        *error = "failed to read text file: " + std::string(path);
        text->clear();
        return false;
    }
    return true;
}

bool ReadKernelLog(std::span<const std::string> paths, std::string* log,
                   std::string* source, std::string* error) {
    if (log == nullptr || source == nullptr || error == nullptr) {
        return false;
    }
    log->clear();
    source->clear();
    error->clear();
    std::string last_error;
    for (const std::string& path : paths) {
        std::string candidate;
        if (ReadKernelLogPath(path, &candidate, &last_error)) {
            *log = std::move(candidate);
            *source = path;
            return true;
        }
    }
    *error = last_error.empty() ? "no kernel log path was provided" : last_error;
    return false;
}

LoaderResult PrepareModule(const LoaderOptions& options) {
    LoaderResult result;
    if (options.mode == LoaderMode::kLoad) {
        result.error = MakeError(AdapterErrorCode::kUnsupportedOperation,
                                 "module loading is disabled in this build; "
                                 "use --prepare-only");
        return result;
    }
    if (options.required_vermagic.empty()) {
        result.error = MakeError(
            AdapterErrorCode::kInvalidVermagic,
            "--prepare-only requires an explicitly recorded --vermagic; "
            "historical kernel logs are not accepted");
        return result;
    }

    std::vector<std::uint8_t> module;
    std::string error;
    if (!ReadBinaryFile(options.module_path, &module, &error)) {
        result.error = MakeError(AdapterErrorCode::kIo, error);
        return result;
    }
    result.report.module_size = module.size();

    std::string symbol_text;
    if (!ReadTextFile(options.kallsyms_path, &symbol_text, &error)) {
        result.error = MakeError(AdapterErrorCode::kIo, error);
        return result;
    }
    KernelSymbolMap symbols;
    if (!ParseKernelSymbols(symbol_text, &symbols, &error)) {
        result.error = MakeError(AdapterErrorCode::kMissingSymbol, error);
        return result;
    }
    result.report.kernel_symbol_count = symbols.size();

    const std::string& vermagic = options.required_vermagic;
    result.report.vermagic_source = "--vermagic";

    const AdapterResult adapted = AdaptModuleOffline(module, symbols, vermagic);
    if (!adapted.ok()) {
        result.error = adapted.error;
        return result;
    }
    result.report.relocated_symbol_count = adapted.report.relocated_symbols.size();
    result.report.original_vermagic = adapted.report.original_vermagic;
    result.report.effective_vermagic = adapted.report.effective_vermagic;
    return result;
}

}  // namespace pathguard::hide::lkm
