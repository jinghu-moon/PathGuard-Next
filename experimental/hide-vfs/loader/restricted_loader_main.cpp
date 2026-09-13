#include "restricted_loader.h"

#include <iostream>

namespace {

void PrintUsage() {
    std::cerr
        << "usage: pathguard_lkm_loader --prepare-only --module <path> "
           "[--kallsyms <path>] [--kmsg <path>] [--vermagic <value>]\n"
        << "       pathguard_lkm_loader --load --module <path> ...\n"
        << "--load is intentionally disabled in this build.\n";
}

}  // namespace

int main(int argc, char** argv) {
    pathguard::hide::lkm::LoaderOptions options;
    std::string error;
    if (!pathguard::hide::lkm::ParseLoaderArguments(argc, argv, &options,
                                                     &error)) {
        if (error == "help") {
            PrintUsage();
            return 0;
        }
        std::cerr << error << '\n';
        PrintUsage();
        return 2;
    }

    const auto result = pathguard::hide::lkm::PrepareModule(options);
    if (!result.ok()) {
        std::cerr << "prepare_failed=" << result.error.message << '\n';
        return 1;
    }

    std::cout << "mode=prepare-only\n"
              << "module_size=" << result.report.module_size << '\n'
              << "kernel_symbol_count=" << result.report.kernel_symbol_count
              << '\n'
              << "relocated_symbol_count="
              << result.report.relocated_symbol_count << '\n'
              << "vermagic_source=" << result.report.vermagic_source << '\n'
              << "vermagic_before=" << result.report.original_vermagic << '\n'
              << "vermagic_after=" << result.report.effective_vermagic << '\n'
              << "adapted_image_persisted=no\n";
    return 0;
}
