#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pathguard/provider_contract.h"

namespace pathguard {

inline constexpr std::uint16_t kProviderJavaBridgeProbeVersion = 1;
inline constexpr std::size_t kProviderJavaMethodCountV1 = 11;
inline constexpr std::size_t kProviderJavaIdentifierCapacityV1 = 1024;
inline constexpr std::size_t kProviderJavaFilePathCapacityV1 = 4096;

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

enum class ProviderJavaDispatchRole : std::uint8_t {
    kForwardDocumentPath,
    kReverseDocumentId,
    kQuery,
    kInsert,
    kOpen,
    kUpdate,
    kDelete,
};

enum class ProviderJavaResultKind : std::uint8_t {
    kFile,
    kDocumentId,
    kCursor,
    kUri,
    kParcelFileDescriptor,
    kCount,
    kVoid,
};

enum class ProviderJavaIdentifierKind : std::uint8_t {
    kNone,
    kContentUri,
    kDocumentId,
    kFilePath,
};

enum class ProviderJavaDynamicKind : std::uint8_t {
    kNone,
    kOpenMode,
    kContentValues,
    kDeleteTarget,
};

struct ProviderJavaIdentifierV1 {
    ProviderJavaIdentifierKind kind = ProviderJavaIdentifierKind::kNone;
    std::array<char, kProviderJavaIdentifierCapacityV1> bytes{};
    std::uint16_t size = 0;

    constexpr std::string_view value() const noexcept {
        return std::string_view(bytes.data(), size);
    }
};

constexpr bool ValidProviderJavaIdentifier(
        ProviderJavaIdentifierKind kind, std::string_view value) noexcept {
    if (kind == ProviderJavaIdentifierKind::kNone) return value.empty();
    if (value.empty() || value.size() >= kProviderJavaIdentifierCapacityV1) {
        return false;
    }
    for (const unsigned char byte : value) {
        if (byte == 0 || byte < 0x20 || byte == 0x7f) return false;
    }
    return kind != ProviderJavaIdentifierKind::kContentUri
        || value.starts_with("content://");
}

template <typename CodeUnit, std::size_t Capacity>
constexpr bool EncodeProviderJavaUtf16ToBytes(
        const CodeUnit* input, std::size_t input_size,
        std::array<char, Capacity>* bytes, std::uint16_t* output_size) noexcept {
    if (bytes == nullptr || output_size == nullptr) return false;
    bytes->fill(0);
    *output_size = 0;
    if (input == nullptr || input_size == 0
        || Capacity == 0 || Capacity > UINT16_MAX) return false;
    std::size_t source = 0;
    std::size_t target = 0;
    while (source < input_size) {
        std::uint32_t code_point = static_cast<std::uint16_t>(input[source++]);
        if (code_point >= 0xd800 && code_point <= 0xdbff) {
            if (source >= input_size) {
                return false;
            }
            const std::uint32_t low =
                static_cast<std::uint16_t>(input[source++]);
            if (low < 0xdc00 || low > 0xdfff) {
                return false;
            }
            code_point = UINT32_C(0x10000)
                + ((code_point - UINT32_C(0xd800)) << 10)
                + (low - UINT32_C(0xdc00));
        } else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
            return false;
        }
        if (code_point == 0 || code_point < 0x20 || code_point == 0x7f) {
            return false;
        }

        std::size_t width = 1;
        if (code_point > 0x7f) width = code_point > 0x7ff ? 3 : 2;
        if (code_point > 0xffff) width = 4;
        if (target + width >= bytes->size()) return false;
        if (width == 1) {
            (*bytes)[target++] = static_cast<char>(code_point);
        } else if (width == 2) {
            (*bytes)[target++] = static_cast<char>(
                0xc0 | (code_point >> 6));
            (*bytes)[target++] = static_cast<char>(
                0x80 | (code_point & 0x3f));
        } else if (width == 3) {
            (*bytes)[target++] = static_cast<char>(
                0xe0 | (code_point >> 12));
            (*bytes)[target++] = static_cast<char>(
                0x80 | ((code_point >> 6) & 0x3f));
            (*bytes)[target++] = static_cast<char>(
                0x80 | (code_point & 0x3f));
        } else {
            (*bytes)[target++] = static_cast<char>(
                0xf0 | (code_point >> 18));
            (*bytes)[target++] = static_cast<char>(
                0x80 | ((code_point >> 12) & 0x3f));
            (*bytes)[target++] = static_cast<char>(
                0x80 | ((code_point >> 6) & 0x3f));
            (*bytes)[target++] = static_cast<char>(
                0x80 | (code_point & 0x3f));
        }
    }
    *output_size = static_cast<std::uint16_t>(target);
    return true;
}

template <typename CodeUnit>
constexpr bool EncodeProviderJavaIdentifierUtf16(
        ProviderJavaIdentifierKind kind, const CodeUnit* input,
        std::size_t input_size, ProviderJavaIdentifierV1* output) noexcept {
    if (output == nullptr) return false;
    *output = {};
    if (kind == ProviderJavaIdentifierKind::kNone
        || kind == ProviderJavaIdentifierKind::kFilePath
        || !EncodeProviderJavaUtf16ToBytes(
            input, input_size, &output->bytes, &output->size)) {
        return false;
    }
    output->kind = kind;
    if (!ValidProviderJavaIdentifier(kind, output->value())) {
        *output = {};
        return false;
    }
    return true;
}

struct ProviderJavaFilePathV1 {
    std::array<char, kProviderJavaFilePathCapacityV1> bytes{};
    std::uint16_t size = 0;

    constexpr std::string_view value() const noexcept {
        return std::string_view(bytes.data(), size);
    }
};

constexpr bool ValidProviderJavaFilePath(std::string_view value) noexcept {
    if (value.empty() || value.size() >= kProviderJavaFilePathCapacityV1
        || value.front() != '/') return false;
    std::size_t component_begin = 1;
    for (std::size_t index = 1; index <= value.size(); ++index) {
        if (index != value.size() && value[index] != '/') continue;
        const std::size_t component_size = index - component_begin;
        if (component_size == 0 || component_size > 255
            || (component_size == 1 && value[component_begin] == '.')
            || (component_size == 2 && value[component_begin] == '.'
                && value[component_begin + 1] == '.')) return false;
        component_begin = index + 1;
    }
    return true;
}

template <typename CodeUnit>
constexpr bool EncodeProviderJavaFilePathUtf16(
        const CodeUnit* input, std::size_t input_size,
        ProviderJavaFilePathV1* output) noexcept {
    if (output == nullptr) return false;
    *output = {};
    if (!EncodeProviderJavaUtf16ToBytes(
            input, input_size, &output->bytes, &output->size)
        || !ValidProviderJavaFilePath(output->value())) {
        *output = {};
        return false;
    }
    return true;
}

struct ProviderJavaDispatchSpecV1 {
    ProviderJavaMethodId id;
    ProviderJavaDispatchRole role;
    ProviderJavaResultKind result;
    OperationMask minimum_operations = 0;
    bool arguments_determine_operation = false;
    std::uint8_t callback_argument_count = 0;
    ProviderJavaIdentifierKind identifier_kind =
        ProviderJavaIdentifierKind::kNone;
    std::uint8_t identifier_argument_index = 0;
    ProviderJavaDynamicKind dynamic_kind = ProviderJavaDynamicKind::kNone;
    std::uint8_t dynamic_argument_index = 0;
};

constexpr OperationMask ProviderOpenModeOperations(
        std::string_view mode) noexcept {
    if (mode == "r") return kOperationOpenRead;
    if (mode == "w" || mode == "wt" || mode == "wa") {
        return kOperationOpenWrite;
    }
    if (mode == "rw" || mode == "rwt") {
        return kOperationOpenRead | kOperationOpenWrite;
    }
    return 0;
}

constexpr OperationMask ProviderContentValuesOperations(
        bool has_values, bool has_display_name) noexcept {
    if (!has_values) return 0;
    return kOperationMetadataMutation
        | (has_display_name ? kOperationRename : 0);
}

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

inline constexpr std::array<ProviderJavaDispatchSpecV1,
    kProviderJavaMethodCountV1> kProviderJavaDispatchSpecsV1{{
        {
            ProviderJavaMethodId::kExternalGetFileForDocId,
            ProviderJavaDispatchRole::kForwardDocumentPath,
            ProviderJavaResultKind::kFile,
            kOperationProviderQuery,
            false,
            3,
            ProviderJavaIdentifierKind::kDocumentId,
            1,
        },
        {
            ProviderJavaMethodId::kExternalGetDocIdForFile,
            ProviderJavaDispatchRole::kReverseDocumentId,
            ProviderJavaResultKind::kDocumentId,
            kOperationReverseMapping,
            false,
            2,
            ProviderJavaIdentifierKind::kFilePath,
            1,
        },
        {
            ProviderJavaMethodId::kMediaQuery,
            ProviderJavaDispatchRole::kQuery,
            ProviderJavaResultKind::kCursor,
            kOperationProviderQuery,
            false,
            5,
            ProviderJavaIdentifierKind::kContentUri,
            1,
        },
        {
            ProviderJavaMethodId::kMediaInsert,
            ProviderJavaDispatchRole::kInsert,
            ProviderJavaResultKind::kUri,
            kOperationProviderInsert | kOperationCreate,
            false,
            4,
            ProviderJavaIdentifierKind::kContentUri,
            1,
        },
        {
            ProviderJavaMethodId::kMediaOpenFile,
            ProviderJavaDispatchRole::kOpen,
            ProviderJavaResultKind::kParcelFileDescriptor,
            kOperationOpenRead | kOperationOpenWrite,
            true,
            4,
            ProviderJavaIdentifierKind::kContentUri,
            1,
            ProviderJavaDynamicKind::kOpenMode,
            2,
        },
        {
            ProviderJavaMethodId::kMediaUpdate,
            ProviderJavaDispatchRole::kUpdate,
            ProviderJavaResultKind::kCount,
            kOperationMetadataMutation | kOperationRename,
            true,
            4,
            ProviderJavaIdentifierKind::kContentUri,
            1,
            ProviderJavaDynamicKind::kContentValues,
            2,
        },
        {
            ProviderJavaMethodId::kMediaDelete,
            ProviderJavaDispatchRole::kDelete,
            ProviderJavaResultKind::kCount,
            kOperationUnlink | kOperationRmdir,
            true,
            3,
            ProviderJavaIdentifierKind::kContentUri,
            1,
            ProviderJavaDynamicKind::kDeleteTarget,
            1,
        },
        {
            ProviderJavaMethodId::kMediaDocumentsQueryDocument,
            ProviderJavaDispatchRole::kQuery,
            ProviderJavaResultKind::kCursor,
            kOperationProviderQuery,
            false,
            3,
            ProviderJavaIdentifierKind::kDocumentId,
            1,
        },
        {
            ProviderJavaMethodId::kMediaDocumentsQueryChildDocuments,
            ProviderJavaDispatchRole::kQuery,
            ProviderJavaResultKind::kCursor,
            kOperationProviderQuery | kOperationDirectoryIterate,
            false,
            4,
            ProviderJavaIdentifierKind::kDocumentId,
            1,
        },
        {
            ProviderJavaMethodId::kMediaDocumentsOpenDocument,
            ProviderJavaDispatchRole::kOpen,
            ProviderJavaResultKind::kParcelFileDescriptor,
            kOperationOpenRead | kOperationOpenWrite,
            true,
            4,
            ProviderJavaIdentifierKind::kDocumentId,
            1,
            ProviderJavaDynamicKind::kOpenMode,
            2,
        },
        {
            ProviderJavaMethodId::kMediaDocumentsDeleteDocument,
            ProviderJavaDispatchRole::kDelete,
            ProviderJavaResultKind::kVoid,
            kOperationUnlink,
            false,
            2,
            ProviderJavaIdentifierKind::kDocumentId,
            1,
        },
    }};

constexpr const ProviderJavaDispatchSpecV1* ProviderJavaDispatchSpec(
        ProviderJavaMethodId id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index >= kProviderJavaDispatchSpecsV1.size()) return nullptr;
    const auto& spec = kProviderJavaDispatchSpecsV1[index];
    return spec.id == id ? &spec : nullptr;
}

constexpr bool ValidateProviderJavaDispatchSpecsV1() noexcept {
    for (std::size_t index = 0;
         index < kProviderJavaDispatchSpecsV1.size(); ++index) {
        const auto& spec = kProviderJavaDispatchSpecsV1[index];
        if (static_cast<std::size_t>(spec.id) != index
            || spec.minimum_operations == 0
            || spec.callback_argument_count == 0
            || (spec.identifier_kind == ProviderJavaIdentifierKind::kNone
                ? spec.identifier_argument_index != 0
                : spec.identifier_argument_index == 0
                    || spec.identifier_argument_index
                        >= spec.callback_argument_count)
            || (spec.arguments_determine_operation
                ? spec.dynamic_kind == ProviderJavaDynamicKind::kNone
                    || spec.dynamic_argument_index == 0
                    || spec.dynamic_argument_index
                        >= spec.callback_argument_count
                : spec.dynamic_kind != ProviderJavaDynamicKind::kNone
                    || spec.dynamic_argument_index != 0)
            || (spec.minimum_operations & ~kProviderCompositeOperationsV1) != 0) {
            return false;
        }
    }
    return true;
}

inline constexpr std::uint16_t kProviderJavaDispatchRequestVersionV1 = 1;

struct ProviderJavaDispatchRequestV1 {
    std::uint16_t version = kProviderJavaDispatchRequestVersionV1;
    ProviderJavaMethodId method_id =
        ProviderJavaMethodId::kExternalGetFileForDocId;
    ProviderJavaDispatchRole role =
        ProviderJavaDispatchRole::kForwardDocumentPath;
    ProviderJavaResultKind result = ProviderJavaResultKind::kFile;
    OperationMask operations = 0;
    ProviderJavaIdentifierV1 identifier;
    ProviderJavaFilePathV1 file_path;
};

enum class ProviderJavaDispatchRequestReason : std::uint8_t {
    kReady,
    kInvalidSpec,
    kDynamicOperationUnavailable,
    kInvalidOperations,
    kIdentifierMissing,
    kFilePathMissing,
};

struct ProviderJavaDispatchRequestBuildV1 {
    ProviderJavaDispatchRequestV1 request;
    ProviderJavaDispatchRequestReason reason =
        ProviderJavaDispatchRequestReason::kInvalidSpec;

    constexpr bool ready() const noexcept {
        return reason == ProviderJavaDispatchRequestReason::kReady;
    }
};

constexpr ProviderJavaDispatchRequestBuildV1 BuildProviderJavaDispatchRequest(
        const ProviderJavaDispatchSpecV1& spec, OperationMask operations,
        bool dynamic_operations_ready,
        const ProviderJavaIdentifierV1& identifier,
        const ProviderJavaFilePathV1& file_path) noexcept {
    ProviderJavaDispatchRequestBuildV1 output;
    if (ProviderJavaDispatchSpec(spec.id) != &spec) return output;
    if (spec.arguments_determine_operation && !dynamic_operations_ready) {
        output.reason =
            ProviderJavaDispatchRequestReason::kDynamicOperationUnavailable;
        return output;
    }
    const bool valid_operations = operations != 0
        && (operations & ~kProviderCompositeOperationsV1) == 0
        && (spec.arguments_determine_operation
            ? (operations & ~spec.minimum_operations) == 0
            : operations == spec.minimum_operations);
    if (!valid_operations) {
        output.reason = ProviderJavaDispatchRequestReason::kInvalidOperations;
        return output;
    }
    if (spec.identifier_kind == ProviderJavaIdentifierKind::kFilePath) {
        if (!ValidProviderJavaFilePath(file_path.value())) {
            output.reason = ProviderJavaDispatchRequestReason::kFilePathMissing;
            return output;
        }
    } else if (identifier.kind != spec.identifier_kind
               || !ValidProviderJavaIdentifier(
                    identifier.kind, identifier.value())) {
        output.reason = ProviderJavaDispatchRequestReason::kIdentifierMissing;
        return output;
    }
    output.request.method_id = spec.id;
    output.request.role = spec.role;
    output.request.result = spec.result;
    output.request.operations = operations;
    output.request.identifier = identifier;
    output.request.file_path = file_path;
    output.reason = ProviderJavaDispatchRequestReason::kReady;
    return output;
}

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
