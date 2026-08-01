#include "pathguard/provider_contract.h"
#include "test_assert.h"

namespace {

pathguard::ProviderContractProbeV1 CompleteProbe(
        pathguard::ProviderContractKind kind) {
    pathguard::ProviderContractProbeV1 probe;
    probe.kind = kind;
    probe.provider_build_id = 0x1234;
    probe.adapter_profile_id = 0x5678;
    probe.passed_checks = pathguard::kProviderContractRequiredChecksV1;
    probe.observed_operations = pathguard::kProviderCompositeOperationsV1;
    return probe;
}

}  // namespace

int main() {
    using namespace pathguard;

    auto documents = ObserveProviderContract(
        CompleteProbe(ProviderContractKind::kDocuments));
    assert(documents.active());
    assert((documents.capabilities & kCapabilityProviderCallerUid) != 0);
    assert((documents.capabilities
            & kCapabilityProviderQueryInsertMapping) != 0);
    assert(documents.missing_checks == 0);
    assert(documents.missing_operations == 0);

    auto media = ObserveProviderContract(
        CompleteProbe(ProviderContractKind::kMediaStore));
    assert(media.active());

    auto pair = ObserveProviderContractPair(
        CompleteProbe(ProviderContractKind::kDocuments),
        CompleteProbe(ProviderContractKind::kMediaStore));
    assert(pair.active());
    assert(pair.capabilities
           == (kCapabilityProviderCallerUid
               | kCapabilityProviderQueryInsertMapping));

    auto incomplete_media = CompleteProbe(ProviderContractKind::kMediaStore);
    incomplete_media.passed_checks &= ~kProviderCheckReverseMapping;
    pair = ObserveProviderContractPair(
        CompleteProbe(ProviderContractKind::kDocuments), incomplete_media);
    assert(!pair.active());
    assert(pair.media.reason == ProviderContractReason::kCheckMissing);

    pair = ObserveProviderContractPair(
        CompleteProbe(ProviderContractKind::kMediaStore),
        CompleteProbe(ProviderContractKind::kDocuments));
    assert(!pair.active());
    assert(pair.documents.reason == ProviderContractReason::kInvalidProbe);
    assert(pair.media.reason == ProviderContractReason::kInvalidProbe);

    auto missing_profile = CompleteProbe(ProviderContractKind::kDocuments);
    missing_profile.adapter_profile_id = 0;
    auto observed = ObserveProviderContract(missing_profile);
    assert(!observed.active());
    assert(observed.reason == ProviderContractReason::kAbiProfileMissing);
    assert((observed.capabilities
            & kCapabilityProviderQueryInsertMapping) == 0);

    auto failed = CompleteProbe(ProviderContractKind::kMediaStore);
    failed.passed_checks &= ~kProviderCheckOpenFdIdentity;
    failed.failed_checks |= kProviderCheckOpenFdIdentity;
    failed.probe_errno = 5;
    observed = ObserveProviderContract(failed);
    assert(observed.reason == ProviderContractReason::kCheckFailed);
    assert(observed.failed_checks == kProviderCheckOpenFdIdentity);
    assert(observed.probe_errno == 5);

    auto missing_check = CompleteProbe(ProviderContractKind::kDocuments);
    missing_check.passed_checks &= ~kProviderCheckRestartRecovery;
    observed = ObserveProviderContract(missing_check);
    assert(observed.reason == ProviderContractReason::kCheckMissing);
    assert(observed.missing_checks == kProviderCheckRestartRecovery);

    auto missing_operation = CompleteProbe(ProviderContractKind::kDocuments);
    missing_operation.observed_operations &= ~kOperationProviderQuery;
    observed = ObserveProviderContract(missing_operation);
    assert(observed.reason == ProviderContractReason::kOperationMissing);
    assert(observed.missing_operations == kOperationProviderQuery);

    auto overlap = CompleteProbe(ProviderContractKind::kMediaStore);
    overlap.failed_checks = kProviderCheckQueryMapping;
    observed = ObserveProviderContract(overlap);
    assert(observed.reason == ProviderContractReason::kInvalidProbe);

    ProviderContractProbeV1 unknown;
    unknown.kind = ProviderContractKind::kUnknown;
    observed = ObserveProviderContract(unknown);
    assert(observed.reason == ProviderContractReason::kInvalidProbe);
    return 0;
}
