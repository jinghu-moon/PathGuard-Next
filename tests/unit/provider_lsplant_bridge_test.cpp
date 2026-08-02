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
    static_assert(ValidateProviderJavaDispatchSpecsV1());

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

    const auto* dispatch = ProviderJavaDispatchSpec(
        ProviderJavaMethodId::kExternalGetFileForDocId);
    assert(dispatch != nullptr);
    assert(dispatch->role == ProviderJavaDispatchRole::kForwardDocumentPath);
    assert(dispatch->result == ProviderJavaResultKind::kFile);
    assert(dispatch->minimum_operations == kOperationProviderQuery);
    assert(dispatch->callback_argument_count == 3);
    assert(dispatch->identifier_kind == ProviderJavaIdentifierKind::kDocumentId);
    assert(dispatch->identifier_argument_index == 1);

    dispatch = ProviderJavaDispatchSpec(
        ProviderJavaMethodId::kExternalGetDocIdForFile);
    assert(dispatch->role == ProviderJavaDispatchRole::kReverseDocumentId);
    assert(dispatch->result == ProviderJavaResultKind::kDocumentId);
    assert(dispatch->minimum_operations == kOperationReverseMapping);
    assert(dispatch->identifier_kind == ProviderJavaIdentifierKind::kFilePath);
    assert(dispatch->identifier_argument_index == 1);

    dispatch = ProviderJavaDispatchSpec(ProviderJavaMethodId::kMediaOpenFile);
    assert(dispatch->role == ProviderJavaDispatchRole::kOpen);
    assert(dispatch->result == ProviderJavaResultKind::kParcelFileDescriptor);
    assert(dispatch->arguments_determine_operation);
    assert(dispatch->callback_argument_count == 4);
    assert(dispatch->identifier_kind == ProviderJavaIdentifierKind::kContentUri);
    assert(dispatch->identifier_argument_index == 1);
    assert(dispatch->dynamic_kind == ProviderJavaDynamicKind::kOpenMode);
    assert(dispatch->dynamic_argument_index == 2);

    dispatch = ProviderJavaDispatchSpec(ProviderJavaMethodId::kMediaUpdate);
    assert(dispatch->role == ProviderJavaDispatchRole::kUpdate);
    assert((dispatch->minimum_operations & kOperationRename) != 0);
    assert(dispatch->arguments_determine_operation);
    assert(dispatch->dynamic_kind == ProviderJavaDynamicKind::kContentValues);
    assert(dispatch->dynamic_argument_index == 2);

    dispatch = ProviderJavaDispatchSpec(ProviderJavaMethodId::kMediaDelete);
    assert(dispatch->dynamic_kind == ProviderJavaDynamicKind::kDeleteTarget);
    assert(dispatch->dynamic_argument_index == 1);

    dispatch = ProviderJavaDispatchSpec(
        ProviderJavaMethodId::kMediaDocumentsQueryChildDocuments);
    assert(dispatch->role == ProviderJavaDispatchRole::kQuery);
    assert((dispatch->minimum_operations & kOperationDirectoryIterate) != 0);

    assert(ProviderJavaDispatchSpec(
        static_cast<ProviderJavaMethodId>(kProviderJavaMethodCountV1)) == nullptr);

    assert(ProviderOpenModeOperations("r") == kOperationOpenRead);
    assert(ProviderOpenModeOperations("w") == kOperationOpenWrite);
    assert(ProviderOpenModeOperations("wt") == kOperationOpenWrite);
    assert(ProviderOpenModeOperations("wa") == kOperationOpenWrite);
    assert(ProviderOpenModeOperations("rw")
           == (kOperationOpenRead | kOperationOpenWrite));
    assert(ProviderOpenModeOperations("rwt")
           == (kOperationOpenRead | kOperationOpenWrite));
    assert(ProviderOpenModeOperations("") == 0);
    assert(ProviderOpenModeOperations("invalid") == 0);
    assert(ProviderContentValuesOperations(false, false) == 0);
    assert(ProviderContentValuesOperations(true, false)
           == kOperationMetadataMutation);
    assert(ProviderContentValuesOperations(true, true)
           == (kOperationMetadataMutation | kOperationRename));

    for (const auto& item : kProviderJavaDispatchSpecsV1) {
        assert(item.identifier_argument_index == 1);
        if (item.id == ProviderJavaMethodId::kExternalGetDocIdForFile) {
            assert(item.identifier_kind == ProviderJavaIdentifierKind::kFilePath);
            continue;
        }
        const bool media_uri = item.id >= ProviderJavaMethodId::kMediaQuery
            && item.id <= ProviderJavaMethodId::kMediaDelete;
        assert(item.identifier_kind == (media_uri
            ? ProviderJavaIdentifierKind::kContentUri
            : ProviderJavaIdentifierKind::kDocumentId));
    }

    ProviderJavaIdentifierV1 identifier;
    const std::uint16_t document_id[]{
        'p', 'r', 'i', 'm', 'a', 'r', 'y', ':', 0x56fe, 0x7247,
    };
    assert(EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind::kDocumentId, document_id,
        std::size(document_id), &identifier));
    assert(identifier.kind == ProviderJavaIdentifierKind::kDocumentId);
    assert(identifier.value() == "primary:\xe5\x9b\xbe\xe7\x89\x87");

    const std::uint16_t content_uri[]{
        'c', 'o', 'n', 't', 'e', 'n', 't', ':', '/', '/', 'm', 'e', 'd', 'i', 'a',
    };
    assert(EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind::kContentUri, content_uri,
        std::size(content_uri), &identifier));
    assert(identifier.value() == "content://media");

    const std::uint16_t supplementary[]{0xd83d, 0xdcc4};
    assert(EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind::kDocumentId, supplementary,
        std::size(supplementary), &identifier));
    assert(identifier.value() == "\xf0\x9f\x93\x84");

    const std::uint16_t invalid_high[]{0xd83d};
    assert(!EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind::kDocumentId, invalid_high,
        std::size(invalid_high), &identifier));
    assert(identifier.kind == ProviderJavaIdentifierKind::kNone);
    const std::uint16_t invalid_low[]{0xdcc4};
    assert(!EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind::kDocumentId, invalid_low,
        std::size(invalid_low), &identifier));
    const std::uint16_t control[]{'a', '\n'};
    assert(!EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind::kDocumentId, control,
        std::size(control), &identifier));
    std::array<std::uint16_t, kProviderJavaIdentifierCapacityV1> oversized{};
    oversized.fill('a');
    assert(!EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind::kDocumentId, oversized.data(),
        oversized.size(), &identifier));
    assert(!ValidProviderJavaIdentifier(
        ProviderJavaIdentifierKind::kContentUri, "file:///tmp/a"));

    const std::uint16_t file_path[]{
        '/', 's', 't', 'o', 'r', 'a', 'g', 'e', '/', 'e', 0x56fe, 0x7247,
    };
    ProviderJavaFilePathV1 path;
    assert(EncodeProviderJavaFilePathUtf16(
        file_path, std::size(file_path), &path));
    assert(path.value() == "/storage/e\xe5\x9b\xbe\xe7\x89\x87");
    const std::uint16_t relative_path[]{'t', 'm', 'p', '/', 'a'};
    assert(!EncodeProviderJavaFilePathUtf16(
        relative_path, std::size(relative_path), &path));
    const std::uint16_t dot_path[]{'/', '.', '/', 'a'};
    assert(!EncodeProviderJavaFilePathUtf16(
        dot_path, std::size(dot_path), &path));
    const std::uint16_t parent_path[]{'/', '.', '.', '/', 'a'};
    assert(!EncodeProviderJavaFilePathUtf16(
        parent_path, std::size(parent_path), &path));
    const std::uint16_t empty_path[]{'/', '/', 'a'};
    assert(!EncodeProviderJavaFilePathUtf16(
        empty_path, std::size(empty_path), &path));

    const std::uint16_t uri_text[]{
        'c', 'o', 'n', 't', 'e', 'n', 't', ':', '/', '/', 'm', 'e', 'd', 'i', 'a',
    };
    ProviderJavaIdentifierV1 uri;
    assert(EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind::kContentUri, uri_text,
        std::size(uri_text), &uri));
    auto request = BuildProviderJavaDispatchRequest(
        kProviderJavaDispatchSpecsV1[2], kOperationProviderQuery, false,
        uri, {});
    assert(request.ready());
    assert(request.request.method_id == ProviderJavaMethodId::kMediaQuery);
    assert(request.request.operations == kOperationProviderQuery);
    assert(request.request.identifier.value() == "content://media");

    request = BuildProviderJavaDispatchRequest(
        kProviderJavaDispatchSpecsV1[4], kOperationOpenRead, true, uri, {});
    assert(request.ready());
    assert(request.request.operations == kOperationOpenRead);
    request = BuildProviderJavaDispatchRequest(
        kProviderJavaDispatchSpecsV1[4], kOperationOpenRead, false, uri, {});
    assert(!request.ready());
    assert(request.reason
           == ProviderJavaDispatchRequestReason::kDynamicOperationUnavailable);

    request = BuildProviderJavaDispatchRequest(
        kProviderJavaDispatchSpecsV1[5], kOperationRename, false, uri, {});
    assert(!request.ready());
    assert(request.reason
           == ProviderJavaDispatchRequestReason::kDynamicOperationUnavailable);

    request = BuildProviderJavaDispatchRequest(
        kProviderJavaDispatchSpecsV1[2], kOperationProviderQuery, false,
        {}, {});
    assert(!request.ready());
    assert(request.reason == ProviderJavaDispatchRequestReason::kIdentifierMissing);

    assert(EncodeProviderJavaFilePathUtf16(
        file_path, std::size(file_path), &path));
    request = BuildProviderJavaDispatchRequest(
        kProviderJavaDispatchSpecsV1[1], kOperationReverseMapping, true,
        {}, path);
    assert(request.ready());
    assert(request.request.file_path.value() == path.value());

    request = BuildProviderJavaDispatchRequest(
        kProviderJavaDispatchSpecsV1[1], kOperationReverseMapping, true,
        uri, {});
    assert(!request.ready());
    assert(request.reason == ProviderJavaDispatchRequestReason::kFilePathMissing);

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
