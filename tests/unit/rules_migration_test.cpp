#include <filesystem>
#include <fstream>
#include <string>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "test_assert.h"

namespace fs = std::filesystem;

namespace {

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    using namespace pathguard::rules;
    const fs::path root(PATHGUARD_SOURCE_DIR);
    Diagnostic error;
    auto source = SourceBuffer::Create(
        "migrated-valid.toml",
        Read(root / "tests/fixtures/rules/migrated-valid.toml"),
        RulesLimits{}, &error);
    assert(source.has_value());
    const RulesBuildResult migrated = CompileRules(*source, RulesLimits{});
    assert(migrated.ok());
    assert(migrated.blob->bytes.size() == 371);
    assert(migrated.blob->content_generation == UINT64_C(14074591159032461277));

    const std::string placeholder = Read(
        root / "tests/fixtures/legacy-rules/legacy-placeholders.ini");
    const std::string migration = Read(root / "docs/rules-ini-migration.md");
    assert(placeholder.find("{package}") != std::string::npos);
    assert(migration.find("{user}") != std::string::npos);
    assert(migration.find("{package}") != std::string::npos);
    assert(migration.find("pathguardctl validate") != std::string::npos);
    return 0;
}
