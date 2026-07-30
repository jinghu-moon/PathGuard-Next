#include "pathguard/provider_route_context.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    ProviderCompositeProbe partial;
    partial.caller_uid = true;
    partial.path_io = true;
    partial.path_operations = kProviderCompositeOperationsV1;
    auto observed = ObserveProviderComposite(partial);
    assert(!observed.active);
    assert((observed.capabilities & kCapabilityProviderCallerUid) != 0);
    assert((observed.capabilities & kCapabilityProviderQueryInsertMapping) == 0);

    partial.query = partial.insert = partial.open_fd = true;
    partial.rename_delete = partial.reverse_mapping = true;
    observed = ObserveProviderComposite(partial);
    assert(observed.active);
    assert((observed.capabilities & kCapabilityProviderQueryInsertMapping) != 0);
    assert((observed.operations & kProviderCompositeOperationsV1)
           == kProviderCompositeOperationsV1);

    ProviderRouteContext context{10358, 0, 1, 7, 41, 2, 3};
    assert(context.valid());
    context.caller_uid = -1;
    assert(!context.valid());
    return 0;
}
