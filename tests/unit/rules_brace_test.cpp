#include <algorithm>
#include <string>

#include "pathguard/pattern.h"
#include "test_assert.h"

int main() {
    using namespace pathguard::pattern;
    static_assert(kPatternLimitsProfileV1
                  .max_brace_expanded_bytes == 64 * 1024);

    const auto extensions = ExpandPatternBraces("**/*.{jpg,jpeg,png}");
    assert(extensions.ok());
    assert(extensions.patterns.size() == 3);
    assert(std::find(extensions.patterns.begin(), extensions.patterns.end(),
                     "**/*.jpeg") != extensions.patterns.end());

    const auto cartesian = ExpandPatternBraces("{a,b}{1,2}");
    assert(cartesian.ok() && cartesian.patterns.size() == 4);
    const auto exactly_32 = ExpandPatternBraces(
        "{a,b,c,d,e,f,g,h}{0,1,2,3}");
    assert(exactly_32.ok() && exactly_32.patterns.size() == 32);

    const auto nested = ExpandPatternBraces("{a,{b,c}}");
    assert(!nested.ok() && nested.error == BraceExpandError::kNested);
    assert(nested.patterns.empty());
    const auto range = ExpandPatternBraces("{1..10,x}");
    assert(!range.ok() && range.patterns.empty());
    const auto empty = ExpandPatternBraces("{a,}");
    assert(!empty.ok() && empty.error == BraceExpandError::kEmptyAlternative);
    const auto slash = ExpandPatternBraces("{a/b,c}");
    assert(!slash.ok() && slash.patterns.empty());
    const auto metacharacter = ExpandPatternBraces("{*.jpg,png}");
    assert(!metacharacter.ok() && metacharacter.patterns.empty());

    auto small_count = kPatternLimitsProfileV1;
    small_count.max_brace_expansions = 3;
    const auto over_count = ExpandPatternBraces("{a,b}{1,2}", small_count);
    assert(!over_count.ok());
    assert(over_count.error == BraceExpandError::kResultLimit);
    assert(over_count.patterns.empty());  // atomic failure

    auto small_bytes = kPatternLimitsProfileV1;
    small_bytes.max_brace_expanded_bytes = 4;
    const auto over_bytes = ExpandPatternBraces("{abc,def}", small_bytes);
    assert(!over_bytes.ok());
    assert(over_bytes.error == BraceExpandError::kByteLimit);
    assert(over_bytes.patterns.empty());  // atomic failure
    return 0;
}
