#include "pathguard/version.h"
#include "test_assert.h"

int main() {
    assert(pathguard::kPolicySchemaVersion == 1);
    return 0;
}
