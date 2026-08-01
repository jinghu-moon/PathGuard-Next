#include <string>
#include <string_view>

#include "pathguard/pattern.h"
#include "test_assert.h"

namespace {

using namespace pathguard::pattern;

PatternProgram Compile(std::string_view pattern) {
    auto result = CompilePattern(pattern);
    assert(result.ok());
    return std::move(*result.program);
}

PatternMatchResult Match(std::string_view pattern, std::string_view path) {
    PatternProgram program = Compile(pattern);
    PatternMatchScratch scratch;
    return MatchPattern(program, path, &scratch);
}

void ExpectMatch(std::string_view pattern, std::string_view path) {
    assert(Match(pattern, path) == PatternMatchResult::kMatch);
}

void ExpectNoMatch(std::string_view pattern, std::string_view path) {
    assert(Match(pattern, path) == PatternMatchResult::kNoMatch);
}

}  // namespace

int main() {
    using namespace pathguard::pattern;
    constexpr std::string_view STAR_COMPONENT = "STAR_COMPONENT";
    constexpr std::string_view ONE_COMPONENT_CHAR = "ONE_COMPONENT_CHAR";
    constexpr std::string_view GLOBSTAR_COMPONENT = "GLOBSTAR_COMPONENT";
    constexpr std::string_view CHAR_CLASS = "CHAR_CLASS";
    constexpr std::string_view InvalidPathEncoding = "InvalidPathEncoding";
    constexpr std::string_view BudgetExceeded = "BudgetExceeded";
    assert(PatternTokenName(PatternTokenKind::kStarComponent) == STAR_COMPONENT);
    assert(PatternTokenName(PatternTokenKind::kOneComponentChar)
           == ONE_COMPONENT_CHAR);
    assert(PatternTokenName(PatternTokenKind::kCharacterClass) == CHAR_CLASS);
    assert(!GLOBSTAR_COMPONENT.empty() && !InvalidPathEncoding.empty()
           && !BudgetExceeded.empty());

    ExpectMatch("IMG_*.jpg", "IMG_.jpg");
    ExpectMatch("IMG_*.jpg", "IMG_123.jpg");
    ExpectNoMatch("IMG_*.jpg", "Album/IMG_123.jpg");
    ExpectMatch("?.txt", "a.txt");
    const std::string non_ascii("\xe4\xb8\xad", 3);
    ExpectMatch("?.txt", non_ascii + ".txt");
    ExpectNoMatch("?.txt", "ab.txt");
    ExpectMatch("*", ".hidden");
    ExpectMatch("\\*", "*");
    assert(CompilePattern("!private/**").error
           == PatternCompileError::kUnsupportedSyntax);
    ExpectMatch("\\!private", "!private");
    assert(CompilePattern("tail\\").error == PatternCompileError::kTrailingEscape);

    ExpectMatch("**/*.jpg", "a.jpg");
    ExpectMatch("**/*.jpg", "Album/a.jpg");
    ExpectMatch("**/*.jpg", "A/B/C/a.jpg");
    ExpectNoMatch("*.jpg", "Album/a.jpg");
    ExpectMatch("**", "a");
    ExpectMatch("**", "a/b");
    ExpectNoMatch("a/**", "a");
    ExpectMatch("a/**", "a/b");
    assert(CompilePattern("ab**").error == PatternCompileError::kInvalidGlobstar);
    assert(CompilePattern("**x").error == PatternCompileError::kInvalidGlobstar);
    assert(CompilePattern("***").error == PatternCompileError::kInvalidGlobstar);
    assert(CompilePattern("*(a)").error
           == PatternCompileError::kUnsupportedSyntax);

    ExpectMatch("[abc].txt", "b.txt");
    ExpectMatch("[a-z0-9].txt", "7.txt");
    ExpectNoMatch("[abc].txt", "z.txt");
    ExpectMatch("[!abc].txt", "z.txt");
    ExpectMatch("[^abc].txt", "z.txt");
    const auto bang = CompilePattern("[!abc].txt");
    const auto caret = CompilePattern("[^abc].txt");
    assert(bang.ok() && caret.ok());
    assert(bang.program->canonical == caret.program->canonical);
    ExpectMatch("[!a]", non_ascii);
    ExpectNoMatch("[a]", non_ascii);
    ExpectMatch("[\\]].txt", "].txt");
    ExpectMatch("[\\-].txt", "-.txt");
    assert(CompilePattern("[]").error == PatternCompileError::kInvalidCharacterClass);
    assert(CompilePattern("[z-a]").error == PatternCompileError::kInvalidCharacterClass);
    assert(CompilePattern("[[:alpha:]]").error
           == PatternCompileError::kInvalidCharacterClass);
    assert(CompilePattern("[" + non_ascii + "]").error
           == PatternCompileError::kInvalidCharacterClass);

    PatternProgram any = Compile("*");
    PatternMatchScratch scratch;
    const std::string invalid_utf8("\xc3\x28", 2);
    assert(MatchPattern(any, invalid_utf8, &scratch)
           == PatternMatchResult::kInvalidPathEncoding);
    auto zero_budget = kPatternLimitsProfileV1;
    zero_budget.matcher_transition_budget = 0;
    assert(MatchPattern(any, "a", &scratch, zero_budget)
           == PatternMatchResult::kBudgetExceeded);

    std::string deep = "leaf";
    for (int depth = 0; depth < 100; ++depth) deep = "d/" + deep;
    ExpectMatch("**/leaf", deep);
    return 0;
}
