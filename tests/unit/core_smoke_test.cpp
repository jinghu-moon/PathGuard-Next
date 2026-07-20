#include "pathguard/capabilities.h"
#include "pathguard/version.h"
#include "test_assert.h"

int main() {
    assert(pathguard::kPolicySchemaVersion == 2);
    static_assert(pathguard::kCapabilityFanotifyFid == (UINT64_C(1) << 8));
    static_assert(pathguard::kCapabilityFanotifyDfidName == (UINT64_C(1) << 9));
    static_assert(pathguard::kCapabilityFanotifyPidfd == (UINT64_C(1) << 10));
    static_assert(pathguard::kCapabilityFanotifyRenameTarget == (UINT64_C(1) << 11));
    static_assert((pathguard::kCapabilityFanotifyDfidName
        & pathguard::kCapabilityFanotifyPidfd) == 0);
    return 0;
}
