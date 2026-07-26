#include "pathguard/runtime_status.h"
#include "test_assert.h"

int main() {
    pathguard::RuntimeStatusRecord status;
    assert(status.version == 1);
    assert(status.enforcement == pathguard::EnforcementState::kInactive);
    assert(status.transaction == pathguard::TransactionOutcome::kNone);
    assert(status.security == pathguard::SecurityLevel::kNone);
    assert(status.reason == pathguard::RuntimeReason::kNone);

    status.enforcement = pathguard::EnforcementState::kFailed;
    status.transaction = pathguard::TransactionOutcome::kNamespaceTainted;
    status.reason = pathguard::RuntimeReason::kOwnerDeath;
    assert(status.enforcement != pathguard::EnforcementState::kActive);
    assert(status.transaction != pathguard::TransactionOutcome::kRollbackComplete);
    return 0;
}
