#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pathguard/provider_contract.h"

namespace pathguard {

inline constexpr std::uint16_t kProviderJavaBridgeProbeVersion = 1;
inline constexpr std::size_t kProviderJavaMethodCountV1 = 11;

enum class ProviderJavaMethodId : std::uint8_t {
    kExternalGetFileForDocId = 0,
    kExternalGetDocIdForFile = 1,
    kMediaQuery = 2,
    kMediaInsert = 3,
    kMediaOpenFile = 4,
    kMediaUpdate = 5,
    kMediaDelete = 6,
    kMediaDocumentsQueryDocument = 7,
    kMediaDocumentsQueryChildDocuments = 8,
    kMediaDocumentsOpenDocument = 9,
    kMediaDocumentsDeleteDocument = 10,
};

using ProviderJavaMethodMask = std::uint64_t;

constexpr ProviderJavaMethodMask ProviderJavaMethodBit(
        ProviderJavaMethodId id) noexcept {
    return UINT64_C(1) << static_cast<std::uint8_t>(id);
}

struct ProviderJavaMethodSpecV1 {
    ProviderJavaMethodId id;
    ProviderContractKind kind;
    std::string_view class_descriptor;
    std::string_view name;
    std::string_view descriptor;
};

inline constexpr std::array<ProviderJavaMethodSpecV1, 2>
    kDocumentsProviderJavaMethodsV1{{
        {
            ProviderJavaMethodId::kExternalGetFileForDocId,
            ProviderContractKind::kDocuments,
            "Lcom/android/externalstorage/ExternalStorageProvider;",
            "getFileForDocId",
            "(Ljava/lang/String;Z)Ljava/io/File;",
        },
        {
            ProviderJavaMethodId::kExternalGetDocIdForFile,
            ProviderContractKind::kDocuments,
            "Lcom/android/externalstorage/ExternalStorageProvider;",
            "getDocIdForFile",
            "(Ljava/io/File;)Ljava/lang/String;",
        },
    }};

inline constexpr std::array<ProviderJavaMethodSpecV1, 9>
    kMediaProviderJavaMethodsV1{{
        {
            ProviderJavaMethodId::kMediaQuery,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaProvider;",
            "query",
            "(Landroid/net/Uri;[Ljava/lang/String;Landroid/os/Bundle;"
            "Landroid/os/CancellationSignal;)Landroid/database/Cursor;",
        },
        {
            ProviderJavaMethodId::kMediaInsert,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaProvider;",
            "insert",
            "(Landroid/net/Uri;Landroid/content/ContentValues;Landroid/os/Bundle;)"
            "Landroid/net/Uri;",
        },
        {
            ProviderJavaMethodId::kMediaOpenFile,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaProvider;",
            "openFile",
            "(Landroid/net/Uri;Ljava/lang/String;Landroid/os/CancellationSignal;)"
            "Landroid/os/ParcelFileDescriptor;",
        },
        {
            ProviderJavaMethodId::kMediaUpdate,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaProvider;",
            "update",
            "(Landroid/net/Uri;Landroid/content/ContentValues;Landroid/os/Bundle;)I",
        },
        {
            ProviderJavaMethodId::kMediaDelete,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaProvider;",
            "delete",
            "(Landroid/net/Uri;Landroid/os/Bundle;)I",
        },
        {
            ProviderJavaMethodId::kMediaDocumentsQueryDocument,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaDocumentsProvider;",
            "queryDocument",
            "(Ljava/lang/String;[Ljava/lang/String;)Landroid/database/Cursor;",
        },
        {
            ProviderJavaMethodId::kMediaDocumentsQueryChildDocuments,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaDocumentsProvider;",
            "queryChildDocuments",
            "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)"
            "Landroid/database/Cursor;",
        },
        {
            ProviderJavaMethodId::kMediaDocumentsOpenDocument,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaDocumentsProvider;",
            "openDocument",
            "(Ljava/lang/String;Ljava/lang/String;Landroid/os/CancellationSignal;)"
            "Landroid/os/ParcelFileDescriptor;",
        },
        {
            ProviderJavaMethodId::kMediaDocumentsDeleteDocument,
            ProviderContractKind::kMediaStore,
            "Lcom/android/providers/media/MediaDocumentsProvider;",
            "deleteDocument",
            "(Ljava/lang/String;)V",
        },
    }};

inline constexpr ProviderJavaMethodMask kDocumentsProviderJavaMethodMaskV1 =
    ProviderJavaMethodBit(ProviderJavaMethodId::kExternalGetFileForDocId)
    | ProviderJavaMethodBit(ProviderJavaMethodId::kExternalGetDocIdForFile);

inline constexpr ProviderJavaMethodMask kMediaProviderJavaMethodMaskV1 =
    ProviderJavaMethodBit(ProviderJavaMethodId::kMediaQuery)
    | ProviderJavaMethodBit(ProviderJavaMethodId::kMediaInsert)
    | ProviderJavaMethodBit(ProviderJavaMethodId::kMediaOpenFile)
    | ProviderJavaMethodBit(ProviderJavaMethodId::kMediaUpdate)
    | ProviderJavaMethodBit(ProviderJavaMethodId::kMediaDelete)
    | ProviderJavaMethodBit(
        ProviderJavaMethodId::kMediaDocumentsQueryDocument)
    | ProviderJavaMethodBit(
        ProviderJavaMethodId::kMediaDocumentsQueryChildDocuments)
    | ProviderJavaMethodBit(
        ProviderJavaMethodId::kMediaDocumentsOpenDocument)
    | ProviderJavaMethodBit(
        ProviderJavaMethodId::kMediaDocumentsDeleteDocument);

constexpr ProviderJavaMethodMask RequiredProviderJavaMethodMask(
        ProviderContractKind kind) noexcept {
    if (kind == ProviderContractKind::kDocuments) {
        return kDocumentsProviderJavaMethodMaskV1;
    }
    if (kind == ProviderContractKind::kMediaStore) {
        return kMediaProviderJavaMethodMaskV1;
    }
    return 0;
}

constexpr bool ValidProviderJavaMethodSpec(
        const ProviderJavaMethodSpecV1& spec) noexcept {
    return RequiredProviderJavaMethodMask(spec.kind) != 0
        && (RequiredProviderJavaMethodMask(spec.kind)
            & ProviderJavaMethodBit(spec.id)) != 0
        && spec.class_descriptor.size() > 2
        && spec.class_descriptor.front() == 'L'
        && spec.class_descriptor.back() == ';'
        && !spec.name.empty()
        && spec.descriptor.size() > 2
        && spec.descriptor.front() == '('
        && spec.descriptor.find(')') != std::string_view::npos;
}

constexpr bool ValidateProviderJavaMethodSpecsV1() noexcept {
    ProviderJavaMethodMask observed = 0;
    for (const auto& spec : kDocumentsProviderJavaMethodsV1) {
        const auto bit = ProviderJavaMethodBit(spec.id);
        if (!ValidProviderJavaMethodSpec(spec) || (observed & bit) != 0) {
            return false;
        }
        observed |= bit;
    }
    for (const auto& spec : kMediaProviderJavaMethodsV1) {
        const auto bit = ProviderJavaMethodBit(spec.id);
        if (!ValidProviderJavaMethodSpec(spec) || (observed & bit) != 0) {
            return false;
        }
        observed |= bit;
    }
    return observed
        == ((UINT64_C(1) << kProviderJavaMethodCountV1) - UINT64_C(1));
}

enum class ProviderJavaBridgeReason : std::uint8_t {
    kActive,
    kInvalidProbe,
    kProfileMismatch,
    kLibraryUnavailable,
    kLsplantUnavailable,
    kHookerDexUnavailable,
    kMethodMissing,
    kHookMissing,
    kBackupMissing,
    kSelfTestMissing,
    kRuntimeError,
};

struct ProviderJavaBridgeProbeV1 {
    std::uint16_t version = kProviderJavaBridgeProbeVersion;
    ProviderContractKind kind = ProviderContractKind::kUnknown;
    std::uint64_t deployment_profile_id = 0;
    std::uint64_t provider_profile_id = 0;
    bool build_matched = false;
    bool library_loaded = false;
    bool lsplant_initialized = false;
    bool hooker_dex_loaded = false;
    ProviderJavaMethodMask resolved_methods = 0;
    ProviderJavaMethodMask installed_hooks = 0;
    ProviderJavaMethodMask backup_methods = 0;
    ProviderJavaMethodMask self_tested_hooks = 0;
    std::int32_t bridge_errno = 0;
};

struct ProviderJavaBridgeObservationV1 {
    ProviderJavaMethodMask required_methods = 0;
    ProviderJavaMethodMask missing_methods = 0;
    ProviderJavaMethodMask missing_hooks = 0;
    ProviderJavaMethodMask missing_backups = 0;
    ProviderJavaMethodMask missing_self_tests = 0;
    std::int32_t bridge_errno = 0;
    ProviderJavaBridgeReason reason = ProviderJavaBridgeReason::kInvalidProbe;

    constexpr bool active() const noexcept {
        return reason == ProviderJavaBridgeReason::kActive;
    }
};

constexpr ProviderJavaBridgeObservationV1 ObserveProviderJavaBridge(
        const ProviderJavaBridgeProbeV1& probe) noexcept {
    ProviderJavaBridgeObservationV1 output;
    output.required_methods = RequiredProviderJavaMethodMask(probe.kind);
    output.missing_methods = output.required_methods & ~probe.resolved_methods;
    output.missing_hooks = output.required_methods & ~probe.installed_hooks;
    output.missing_backups = output.required_methods & ~probe.backup_methods;
    output.missing_self_tests =
        output.required_methods & ~probe.self_tested_hooks;
    output.bridge_errno = probe.bridge_errno;
    if (probe.version != kProviderJavaBridgeProbeVersion
        || output.required_methods == 0
        || probe.deployment_profile_id == 0
        || probe.provider_profile_id == 0) {
        return output;
    }
    if (!probe.build_matched) {
        output.reason = ProviderJavaBridgeReason::kProfileMismatch;
    } else if (!probe.library_loaded) {
        output.reason = ProviderJavaBridgeReason::kLibraryUnavailable;
    } else if (!probe.lsplant_initialized) {
        output.reason = ProviderJavaBridgeReason::kLsplantUnavailable;
    } else if (!probe.hooker_dex_loaded) {
        output.reason = ProviderJavaBridgeReason::kHookerDexUnavailable;
    } else if (probe.bridge_errno != 0) {
        output.reason = ProviderJavaBridgeReason::kRuntimeError;
    } else if (output.missing_methods != 0) {
        output.reason = ProviderJavaBridgeReason::kMethodMissing;
    } else if (output.missing_hooks != 0) {
        output.reason = ProviderJavaBridgeReason::kHookMissing;
    } else if (output.missing_backups != 0) {
        output.reason = ProviderJavaBridgeReason::kBackupMissing;
    } else if (output.missing_self_tests != 0) {
        output.reason = ProviderJavaBridgeReason::kSelfTestMissing;
    } else {
        output.reason = ProviderJavaBridgeReason::kActive;
    }
    return output;
}

struct ProviderJavaBridgePairObservationV1 {
    ProviderJavaBridgeObservationV1 documents;
    ProviderJavaBridgeObservationV1 media;
    CapabilityBits capabilities = 0;

    constexpr bool active() const noexcept {
        return documents.active() && media.active()
            && capabilities == kCapabilityProviderQueryInsertMapping;
    }
};

constexpr ProviderJavaBridgePairObservationV1 ObserveProviderJavaBridgePair(
        const ProviderJavaBridgeProbeV1& documents,
        const ProviderJavaBridgeProbeV1& media) noexcept {
    ProviderJavaBridgePairObservationV1 output;
    output.documents = ObserveProviderJavaBridge(documents);
    output.media = ObserveProviderJavaBridge(media);
    if (documents.kind != ProviderContractKind::kDocuments) {
        output.documents.reason = ProviderJavaBridgeReason::kInvalidProbe;
    }
    if (media.kind != ProviderContractKind::kMediaStore
        || media.deployment_profile_id != documents.deployment_profile_id) {
        output.media.reason = ProviderJavaBridgeReason::kInvalidProbe;
    }
    if (output.documents.active() && output.media.active()) {
        output.capabilities = kCapabilityProviderQueryInsertMapping;
    }
    return output;
}

}  // namespace pathguard
