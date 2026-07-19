#include <cassert>

#include "pathguard/version.h"

int main() {
    assert(pathguard::kPolicySchemaVersion == 1);
    return 0;
}
