#include <string>

#include "pathguard/rules/semantic.h"
#include "pathguard/rules_contract.h"
#include "test_assert.h"

int main() {
    using namespace pathguard::rules;
    RulesLimits limits;
    CompileStatistics statistics;

    auto path = NormalizeRulePath("Download/Folder", limits, &statistics);
    assert(path.has_value());
    assert(path->bytes == "Download/Folder");
    assert(path->component_offsets.size() == 2);
    assert(statistics.path_normalizations == 1);

    assert(!NormalizeRulePath("", limits, &statistics));
    assert(!NormalizeRulePath("/absolute", limits, &statistics));
    assert(!NormalizeRulePath("A//B", limits, &statistics));
    assert(!NormalizeRulePath("A/./B", limits, &statistics));
    assert(!NormalizeRulePath("A/../B", limits, &statistics));
    assert(!NormalizeRulePath(std::string("A\0B", 3), limits, &statistics));
    assert(!NormalizeRulePath("A\tB", limits, &statistics));

    const auto user_placeholder = NormalizeRulePath("A/{user}/B", limits,
                                                    &statistics);
    const auto package_placeholder = NormalizeRulePath("A/{package}/B", limits,
                                                       &statistics);
    const auto unknown_placeholder = NormalizeRulePath("A/{future}/B", limits,
                                                       &statistics);
    assert(user_placeholder && user_placeholder->bytes == "A/{user}/B");
    assert(package_placeholder && package_placeholder->bytes == "A/{package}/B");
    assert(unknown_placeholder && unknown_placeholder->bytes == "A/{future}/B");

    RulesLimits short_path;
    short_path.max_path_bytes = 3;
    assert(!NormalizeRulePath("ABCD", short_path, &statistics));
    RulesLimits one_component;
    one_component.max_path_components = 1;
    assert(!NormalizeRulePath("A/B", one_component, &statistics));

    const auto parent = NormalizeRulePath("A/B", limits, &statistics);
    const auto child = NormalizeRulePath("A/B/C", limits, &statistics);
    const auto sibling = NormalizeRulePath("A/BC", limits, &statistics);
    assert(parent && child && sibling);
    assert(IsSameOrAncestor(*parent, *child));
    assert(!IsSameOrAncestor(*parent, *sibling));
    return 0;
}
