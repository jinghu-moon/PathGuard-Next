#include "restricted_loader.h"
#include "hide_loader_fixture.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path TempPath(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string("pathguard-restricted-loader-") +
            std::to_string(stamp) + suffix);
}

void WriteText(const std::filesystem::path& path, const char* value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << value;
    assert(output.good());
}

void WriteBinary(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
}

}  // namespace

int main() {
    using namespace pathguard::hide::lkm;

    {
        char module[] = "probe.ko";
        char* argv[] = {const_cast<char*>("loader"),
                        const_cast<char*>("--prepare-only"),
                        const_cast<char*>("--module"), module,
                        const_cast<char*>("--kmsg"),
                        const_cast<char*>("/tmp/a"),
                        const_cast<char*>("--kmsg"),
                        const_cast<char*>("/tmp/b")};
        LoaderOptions options;
        std::string error;
        assert(ParseLoaderArguments(static_cast<int>(std::size(argv)), argv,
                                    &options, &error));
        assert(options.mode == LoaderMode::kPrepareOnly);
        assert(options.module_path == "probe.ko");
        const std::vector<std::string> expected_paths = {"/tmp/a", "/tmp/b"};
        assert(options.kmsg_paths == expected_paths);
    }

    {
        char module[] = "probe.ko";
        char* argv[] = {const_cast<char*>("loader"),
                        const_cast<char*>("--load"),
                        const_cast<char*>("--module"), module};
        LoaderOptions options;
        std::string error;
        assert(ParseLoaderArguments(static_cast<int>(std::size(argv)), argv,
                                    &options, &error));
        assert(options.mode == LoaderMode::kLoad);
        const auto result = PrepareModule(options);
        assert(!result.ok());
        assert(result.error.code == AdapterErrorCode::kUnsupportedOperation);
        assert(result.error.message.find("disabled") != std::string::npos);
    }

    const auto symbols_path = TempPath("-kallsyms");
    const auto first_log_path = TempPath("-missing-kmsg");
    const auto second_log_path = TempPath("-kmsg");
    WriteText(symbols_path, "ffff000012345678 T init_uts_ns\n");
    WriteText(second_log_path,
              "6,1,1;-;probe: version magic 'old' should be "
              "'6.12.23-device SMP preempt mod_unload aarch64'\n");

    std::string symbols_text;
    std::string error;
    assert(ReadTextFile(symbols_path.string(), &symbols_text, &error));
    KernelSymbolMap symbols;
    assert(ParseKernelSymbols(symbols_text, &symbols, &error));
    assert(symbols.at("init_uts_ns") == 0xffff000012345678ULL);

    std::string log;
    std::string source;
    const std::vector<std::string> log_paths = {first_log_path.string(),
                                                second_log_path.string()};
    assert(ReadKernelLog(log_paths, &log, &source, &error));
    assert(source == second_log_path.string());
    std::string vermagic;
    assert(ExtractRequiredVermagic(log, &vermagic));
    assert(vermagic == "6.12.23-device SMP preempt mod_unload aarch64");

    const auto module_path = TempPath("-probe.ko");
    WriteBinary(module_path, pathguard::hide::test::BuildModule());
    LoaderOptions prepare_options;
    prepare_options.module_path = module_path.string();
    prepare_options.kallsyms_path = symbols_path.string();
    prepare_options.required_vermagic = vermagic;
    const auto prepared = PrepareModule(prepare_options);
    assert(prepared.ok());
    assert(prepared.report.module_size ==
           pathguard::hide::test::BuildModule().size());
    assert(prepared.report.kernel_symbol_count == 1);
    assert(prepared.report.relocated_symbol_count == 1);
    assert(prepared.report.original_vermagic == "old magic");
    assert(prepared.report.effective_vermagic == vermagic);
    assert(prepared.report.vermagic_source == "--vermagic");

    {
        KernelLogCursor cursor;
        std::string cursor_source;
        assert(KernelLogCursor::Open(
            std::vector<std::string>{second_log_path.string()}, &cursor,
            &cursor_source, &error));
        assert(cursor_source == second_log_path.string());
    }

    std::filesystem::remove(symbols_path);
    std::filesystem::remove(second_log_path);
    std::filesystem::remove(module_path);
    return 0;
}
