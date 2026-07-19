#include "pathguard/version.h"

namespace pathguard {

// Keeps the initial host library target linkable while policy components are added.
int pathguard_core_anchor() {
    return kPolicySchemaVersion;
}

}  // namespace pathguard
