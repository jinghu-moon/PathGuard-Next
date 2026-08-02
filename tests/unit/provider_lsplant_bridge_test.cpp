#include <array>
#include <string_view>

#include "pathguard/provider_lsplant_bridge.h"
#include "test_assert.h"

namespace {

pathguard::ProviderJavaBridgeProbeV1 CompleteProbe(
        pathguard::ProviderContractKind kind) {
    pathguard::ProviderJavaBridgeProbeV1 probe;
    probe.kind = kind;
    probe.deployment_profile_id = 0x50475636;
    probe.provider_profile_id = kind == pathguard::ProviderContractKind::kDocuments
        ? 0x444f4353 : 0x4d454449;
    probe.build_matched = true;
    probe.library_loaded = true;
    probe.lsplant_initialized = true;
    probe.hooker_dex_loaded = true;
    probe.resolved_methods = pathguard::RequiredProviderJavaMethodMask(kind);
    probe.installed_hooks = probe.resolved_methods;
    probe.backup_methods = probe.resolved_methods;
    probe.self_tested_hooks = probe.resolved_methods;
    return probe;
}

void AssertMethod(
        const pathguard::ProviderJavaMethodSpecV1& method,
        pathguard::ProviderJavaMethodId id,
        pathguard::ProviderContractKind kind,
        std::string_view class_descriptor,
        std::string_view name,
        std::string_view descriptor) {
    assert(method.id == id);
    assert(method.kind == kind);
    assert(method.class_descriptor == class_descriptor);
    assert(method.name == name);
    assert(method.descriptor == descriptor);
}

}  // namespace

int main() {
    using namespace pathguard;

    static_assert(kDocumentsProviderJavaMethodsV1.size() == 2);
    static_assert(kMediaProviderJavaMethodsV1.size() == 9);
    static_assert(kProviderJavaMethodCountV1 == 11);
    static_assert(ValidateProviderJavaMethodSpecsV1());

    AssertMethod(
        kDocumentsProviderJavaMethodsV1[0],
        ProviderJavaMethodId::kExternalGetFileForDocId,
        ProviderContractKind::kDocuments,
        "Lcom/android/externalstorage/ExternalStorageProvider;",
        "getFileForDocId",
        "(Ljava/lang/String;Z)Ljava/io/File;");
    AssertMethod(
        kDocumentsProviderJavaMethodsV1[1],
        ProviderJavaMethodId::kExternalGetDocIdForFile,
        ProviderContractKind::kDocuments,
        "Lcom/android/externalstorage/ExternalStorageProvider;",
        "getDocIdForFile",
        "(Ljava/io/File;)Ljava/lang/String;");

    const std::array<std::string_view, 9> media_names{
        "query", "insert", "openFile", "update", "delete",
        "queryDocument", "queryChildDocuments", "openDocument",
        "deleteDocument",
    };
    for (std::size_t i = 0; i < media_names.size(); ++i) {
        assert(kMediaProviderJavaMethodsV1[i].name == media_names[i]);
        assert(kMediaProviderJavaMethodsV1[i].kind
               == ProviderContractKind::kMediaStore);
    }
    AssertMethod(
        kMediaProviderJavaMethodsV1[0],
        ProviderJavaMethodId::kMediaQuery,
        ProviderContractKind::kMediaStore,
        "Lcom/android/providers/media/MediaProvider;",
        "query",
        "(Landroid/net/Uri;[Ljava/lang/String;Landroid/os/Bundle;"
        "Landroid/os/CancellationSignal;)Landroid/database/Cursor;");
    AssertMethod(
        kMediaProviderJavaMethodsV1[5],
        ProviderJavaMethodId::kMediaDocumentsQueryDocument,
        ProviderContractKind::kMediaStore,
        "Lcom/android/providers/media/MediaDocumentsProvider;",
        "queryDocument",
        "(Ljava/lang/String;[Ljava/lang/String;)Landroid/database/Cursor;");

    const auto documents = ObserveProviderJavaBridge(
        CompleteProbe(ProviderContractKind::kDocuments));
    assert(documents.active());
    assert(documents.required_methods == kDocumentsProviderJavaMethodMaskV1);
    assert(documents.missing_methods == 0);

    const auto media = ObserveProviderJavaBridge(
        CompleteProbe(ProviderContractKind::kMediaStore));
    assert(media.active());
    assert(media.required_methods == kMediaProviderJavaMethodMaskV1);

    auto pair = ObserveProviderJavaBridgePair(
        CompleteProbe(ProviderContractKind::kDocuments),
        CompleteProbe(ProviderContractKind::kMediaStore));
    assert(pair.active());
    assert(pair.capabilities == kCapabilityProviderQueryInsertMapping);

    auto incomplete = CompleteProbe(ProviderContractKind::kDocuments);
    incomplete.self_tested_hooks &= ~ProviderJavaMethodBit(
        ProviderJavaMethodId::kExternalGetDocIdForFile);
    auto observation = ObserveProviderJavaBridge(incomplete);
    assert(!observation.active());
    assert(observation.reason == ProviderJavaBridgeReason::kSelfTestMissing);
    assert(observation.missing_self_tests
           == ProviderJavaMethodBit(
               ProviderJavaMethodId::kExternalGetDocIdForFile));

    incomplete = CompleteProbe(ProviderContractKind::kMediaStore);
    incomplete.backup_methods &= ~ProviderJavaMethodBit(
        ProviderJavaMethodId::kMediaOpenFile);
    observation = ObserveProviderJavaBridge(incomplete);
    assert(observation.reason == ProviderJavaBridgeReason::kBackupMissing);

    incomplete = CompleteProbe(ProviderContractKind::kMediaStore);
    incomplete.installed_hooks &= ~ProviderJavaMethodBit(
        ProviderJavaMethodId::kMediaInsert);
    observation = ObserveProviderJavaBridge(incomplete);
    assert(observation.reason == ProviderJavaBridgeReason::kHookMissing);

    incomplete = CompleteProbe(ProviderContractKind::kMediaStore);
    incomplete.resolved_methods &= ~ProviderJavaMethodBit(
        ProviderJavaMethodId::kMediaQuery);
    observation = ObserveProviderJavaBridge(incomplete);
    assert(observation.reason == ProviderJavaBridgeReason::kMethodMissing);

    incomplete = CompleteProbe(ProviderContractKind::kDocuments);
    incomplete.lsplant_initialized = false;
    observation = ObserveProviderJavaBridge(incomplete);
    assert(observation.reason == ProviderJavaBridgeReason::kLsplantUnavailable);

    incomplete = CompleteProbe(ProviderContractKind::kDocuments);
    incomplete.hooker_dex_loaded = false;
    observation = ObserveProviderJavaBridge(incomplete);
    assert(observation.reason == ProviderJavaBridgeReason::kHookerDexUnavailable);

    incomplete = CompleteProbe(ProviderContractKind::kDocuments);
    incomplete.build_matched = false;
    observation = ObserveProviderJavaBridge(incomplete);
    assert(observation.reason == ProviderJavaBridgeReason::kProfileMismatch);

    pair = ObserveProviderJavaBridgePair(
        CompleteProbe(ProviderContractKind::kDocuments), incomplete);
    assert(!pair.active());
    assert(pair.capabilities == 0);

    ProviderJavaBridgeProbeV1 unknown;
    observation = ObserveProviderJavaBridge(unknown);
    assert(observation.reason == ProviderJavaBridgeReason::kInvalidProbe);
    return 0;
}
