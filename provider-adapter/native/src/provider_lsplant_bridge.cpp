#include "pathguard/provider_lsplant_bridge_api.h"

#include <android/log.h>
#include <dobby.h>
#include <jni.h>
#include <xdl.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <sys/syscall.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>

#include "lsplant.hpp"
#include "pathguard/provider_lsplant_bridge.h"
#include "pathguard/provider_mapping.h"
#include "pathguard/provider_route_snapshot_registry.h"

namespace {

constexpr char kLogTag[] = "PathGuardLsplant";
constexpr char kHookerClass[] = "dev.pathguard.providerhook.ProviderHooker";
constexpr std::uint32_t kApplicationLoaderPollCount = 350;
constexpr useconds_t kApplicationLoaderPollUs = 10000;

static_assert(static_cast<unsigned>(
    pathguard::ProviderJavaDispatchRole::kDelete)
    == PATHGUARD_LSPLANT_MAPPING_ROLE_DELETE);
static_assert(static_cast<unsigned>(
    pathguard::ProviderJavaResultKind::kVoid)
    == PATHGUARD_LSPLANT_MAPPING_RESULT_VOID);
static_assert(static_cast<unsigned>(
    pathguard::ProviderJavaIdentifierKind::kFilePath)
    == PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_FILE_PATH);

#define PG_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)
#define PG_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)

struct HookState {
    std::mutex mutex;
    std::condition_variable condition;
    void* art_handle = nullptr;
    JavaVM* vm = nullptr;
    bool initialized = false;
    bool install_pending = false;
    bool install_complete = false;
    std::uint8_t pending_kind = PATHGUARD_LSPLANT_PROVIDER_UNKNOWN;
    std::uint8_t installed_kind = PATHGUARD_LSPLANT_PROVIDER_UNKNOWN;
    void* dex_bytes = nullptr;
    jobject hooker_class_loader = nullptr;
    jobject target_class_loader = nullptr;
    jclass hooker_class = nullptr;
    jclass dispatch_result_class = nullptr;
    jmethodID dispatch_result_rewrite = nullptr;
    PathGuardLsplantBridgeResultV1 final_result{};
    std::array<jobject, pathguard::kProviderJavaMethodCountV1> targets{};
    std::array<jobject, pathguard::kProviderJavaMethodCountV1> hookers{};
};

HookState g_state;
PathGuardLsplantMappingResolverV1 g_mapping_resolver = nullptr;
void* g_mapping_user_data = nullptr;
PathGuardLsplantExternalIdentitySinkV1 g_external_identity_sink = nullptr;
void* g_external_identity_user_data = nullptr;

struct ProviderRegistrySnapshot {
    explicit ProviderRegistrySnapshot(
            std::unique_ptr<pathguard::ProviderRouteSnapshotRegistryV1> value)
        : registry(std::move(value)) {}
    std::unique_ptr<pathguard::ProviderRouteSnapshotRegistryV1> registry;
};

class ProviderRegistryDomain {
public:
    class Guard {
    public:
        Guard() = default;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        ~Guard() { Reset(); }
        explicit operator bool() const noexcept { return snapshot_ != nullptr; }
        const ProviderRegistrySnapshot* operator->() const noexcept {
            return snapshot_;
        }
    private:
        friend class ProviderRegistryDomain;
        void Reset() noexcept {
            if (domain_ != nullptr && slot_ < kSlots) {
                domain_->hazards_[slot_].store(nullptr);
                domain_->owners_[slot_].store(nullptr);
            }
            domain_ = nullptr;
            snapshot_ = nullptr;
            slot_ = kSlots;
        }
        ProviderRegistryDomain* domain_ = nullptr;
        ProviderRegistrySnapshot* snapshot_ = nullptr;
        std::size_t slot_ = kSlots;
    };

    bool Acquire(Guard* guard) noexcept {
        if (guard == nullptr) return false;
        guard->Reset();
        std::size_t slot = kSlots;
        for (std::size_t index = 0; index < kSlots; ++index) {
            void* expected = nullptr;
            if (owners_[index].compare_exchange_strong(expected, guard)) {
                slot = index;
                break;
            }
        }
        if (slot == kSlots) return false;
        guard->domain_ = this;
        guard->slot_ = slot;
        for (;;) {
            ProviderRegistrySnapshot* candidate = active_.load();
            hazards_[slot].store(candidate);
            if (active_.load() == candidate) {
                guard->snapshot_ = candidate;
                return candidate != nullptr;
            }
            hazards_[slot].store(nullptr);
        }
    }

    bool Publish(ProviderRegistrySnapshot* candidate) noexcept {
        if (candidate == nullptr || writer_.test_and_set()) return false;
        Reclaim();
        if (retired_count_ >= kRetired) {
            writer_.clear();
            return false;
        }
        ProviderRegistrySnapshot* previous = active_.exchange(candidate);
        if (previous != nullptr) retired_[retired_count_++] = previous;
        Reclaim();
        writer_.clear();
        return true;
    }

private:
    void Reclaim() noexcept {
        bool reader_entering = false;
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            if (owners_[slot].load() != nullptr
                && hazards_[slot].load() == nullptr) {
                reader_entering = true;
                break;
            }
        }
        if (reader_entering) return;
        std::size_t kept = 0;
        for (std::size_t index = 0; index < retired_count_; ++index) {
            bool hazardous = false;
            for (std::size_t slot = 0; slot < kSlots; ++slot) {
                hazardous = hazardous
                    || hazards_[slot].load() == retired_[index];
            }
            if (hazardous) {
                retired_[kept++] = retired_[index];
            } else {
                delete retired_[index];
            }
        }
        retired_count_ = kept;
    }

    static constexpr std::size_t kSlots = 256;
    static constexpr std::size_t kRetired = 8;
    std::array<std::atomic<void*>, kSlots> owners_{};
    std::array<std::atomic<ProviderRegistrySnapshot*>, kSlots> hazards_{};
    std::atomic<ProviderRegistrySnapshot*> active_{nullptr};
    std::atomic_flag writer_ = ATOMIC_FLAG_INIT;
    std::array<ProviderRegistrySnapshot*, kRetired> retired_{};
    std::size_t retired_count_ = 0;
};

ProviderRegistryDomain g_mapping_registry_domain;

int PublishMappingSnapshot(
        const PathGuardProviderRouteSnapshotV1* snapshot) noexcept {
    if (snapshot == nullptr) return EINVAL;
    auto registry = pathguard::DecodeProviderRouteSnapshotV1(*snapshot);
    if (registry == nullptr) return EINVAL;
    auto* publication = new (std::nothrow) ProviderRegistrySnapshot(
        std::move(registry));
    if (publication == nullptr) return ENOMEM;
    if (!g_mapping_registry_domain.Publish(publication)) {
        delete publication;
        return EBUSY;
    }
    return 0;
}

bool ResolveMappingRuntimeFacts(
        const pathguard::ProviderJavaDispatchRequestV1& request,
        pathguard::ProviderMappingRuntimeFactsV1* facts,
        ProviderRegistryDomain::Guard* registry_guard,
        void* user_data) noexcept {
    if (g_mapping_resolver == nullptr || facts == nullptr
        || registry_guard == nullptr
        || request.identifier.size
            >= PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_CAPACITY
        || request.file_path.size
            >= PATHGUARD_LSPLANT_MAPPING_FILE_PATH_CAPACITY) {
        return false;
    }
    PathGuardLsplantMappingRequestV1 abi_request{};
    abi_request.version = PATHGUARD_LSPLANT_BRIDGE_API_VERSION;
    abi_request.size = sizeof(abi_request);
    abi_request.method_id = static_cast<std::uint8_t>(request.method_id);
    abi_request.role = static_cast<std::uint8_t>(request.role);
    abi_request.result_kind = static_cast<std::uint8_t>(request.result);
    abi_request.identifier_kind =
        static_cast<std::uint8_t>(request.identifier.kind);
    abi_request.operations = request.operations;
    abi_request.identifier_size = request.identifier.size;
    abi_request.file_path_size = request.file_path.size;
    std::memcpy(abi_request.identifier, request.identifier.bytes.data(),
                request.identifier.size);
    std::memcpy(abi_request.file_path, request.file_path.bytes.data(),
                request.file_path.size);

    PathGuardLsplantMappingFactsV1 abi_facts{};
    abi_facts.version = PATHGUARD_LSPLANT_BRIDGE_API_VERSION;
    abi_facts.size = sizeof(abi_facts);
    if (g_mapping_resolver(&abi_request, &abi_facts, user_data) == 0
        || abi_facts.version != PATHGUARD_LSPLANT_BRIDGE_API_VERSION
        || abi_facts.size != sizeof(abi_facts)
        || abi_facts.profile_matched > 1
        || abi_facts.runtime_available > 1
        || abi_facts.binding_state > PATHGUARD_LSPLANT_MAPPING_BINDING_SNAPSHOT
        || (abi_facts.supported_operations
            & ~pathguard::kProviderCompositeOperationsV1) != 0
        || (abi_facts.profile_matched != 0 && abi_facts.profile_id == 0)) {
        return false;
    }
    if (abi_facts.binding_state == PATHGUARD_LSPLANT_MAPPING_BINDING_NONE) {
        if (abi_facts.snapshot_generation != 0 || abi_facts.binding_id != 0
            || abi_facts.reverse_record_id != 0) {
            return false;
        }
    } else {
        if (!g_mapping_registry_domain.Acquire(registry_guard)
            || (*registry_guard)->registry == nullptr) {
            return false;
        }
        const auto lookup = (*registry_guard)->registry->Lookup(
            abi_facts.snapshot_generation, abi_facts.binding_id,
            abi_facts.reverse_record_id);
        if (!lookup.resolved()) return false;
        if (lookup.binding->reverse_mode
                == pathguard::ProviderRouteReverseMode::kStaticUnique) {
            if (!pathguard::MaterializeStaticProviderRouteBinding(
                    request, *lookup.binding,
                    &facts->materialized_binding)) {
                return false;
            }
            facts->binding = &facts->materialized_binding;
        } else {
            facts->binding = lookup.binding;
        }
        if (lookup.reverse != nullptr) facts->reverse = *lookup.reverse;
    }
    facts->profile_match = {
        abi_facts.profile_id,
        abi_facts.profile_matched != 0
            ? pathguard::ProviderAdapterProfileReason::kMatched
            : pathguard::ProviderAdapterProfileReason::kNoMatchingProfile,
    };
    facts->supported_operations = abi_facts.supported_operations;
    facts->runtime_available = abi_facts.runtime_available != 0;
    return true;
}

bool IsJavaInstance(JNIEnv* env, jobject value, const char* class_name);
void ClearException(JNIEnv* env, const char* operation);

const char* ProviderJavaResultClassName(
        pathguard::ProviderJavaResultKind kind) noexcept {
    switch (kind) {
        case pathguard::ProviderJavaResultKind::kFile:
            return "java/io/File";
        case pathguard::ProviderJavaResultKind::kDocumentId:
            return "java/lang/String";
        case pathguard::ProviderJavaResultKind::kCursor:
            return "android/database/Cursor";
        case pathguard::ProviderJavaResultKind::kUri:
            return "android/net/Uri";
        case pathguard::ProviderJavaResultKind::kParcelFileDescriptor:
            return "android/os/ParcelFileDescriptor";
        case pathguard::ProviderJavaResultKind::kCount:
            return "java/lang/Integer";
        case pathguard::ProviderJavaResultKind::kVoid:
            return nullptr;
    }
    return nullptr;
}

pathguard::ProviderJavaResultObservationV1 ObserveNativeProviderResult(
        JNIEnv* env, pathguard::ProviderJavaResultKind expected,
        jobject original_result) {
    const bool is_null = original_result == nullptr;
    const char* expected_class = ProviderJavaResultClassName(expected);
    const bool matches = !is_null && expected_class != nullptr
        && IsJavaInstance(env, original_result, expected_class);
    return pathguard::ObserveProviderJavaResult(expected, is_null, matches);
}

pathguard::OperationMask ReadOpenMode(
        JNIEnv* env, jobjectArray arguments, std::uint8_t argument_index) {
    jobject value = env->GetObjectArrayElement(arguments, argument_index);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    if (value == nullptr) return 0;
    jclass string_class = env->FindClass("java/lang/String");
    if (string_class == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(value);
        return 0;
    }
    const bool is_string = env->IsInstanceOf(value, string_class) == JNI_TRUE;
    env->DeleteLocalRef(string_class);
    if (env->ExceptionCheck() || !is_string) {
        env->ExceptionClear();
        env->DeleteLocalRef(value);
        return 0;
    }
    auto mode = static_cast<jstring>(value);
    const jsize length = env->GetStringLength(mode);
    if (env->ExceptionCheck() || length <= 0 || length > 3) {
        env->ExceptionClear();
        env->DeleteLocalRef(value);
        return 0;
    }
    jchar utf16[3]{};
    env->GetStringRegion(mode, 0, length, utf16);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(value);
        return 0;
    }
    char ascii[3]{};
    for (jsize index = 0; index < length; ++index) {
        if (utf16[index] > 0x7f) {
            env->DeleteLocalRef(value);
            return 0;
        }
        ascii[index] = static_cast<char>(utf16[index]);
    }
    env->DeleteLocalRef(value);
    return pathguard::ProviderOpenModeOperations(
        std::string_view(ascii, static_cast<std::size_t>(length)));
}

bool ReadContentValuesText(
        JNIEnv* env, jobject values, jmethodID get_as_string,
        const char* key, pathguard::ProviderJavaFilePathV1* output) {
    if (env == nullptr || values == nullptr || get_as_string == nullptr
        || key == nullptr || output == nullptr) return false;
    jstring java_key = env->NewStringUTF(key);
    jstring text = java_key == nullptr ? nullptr : static_cast<jstring>(
        env->CallObjectMethod(values, get_as_string, java_key));
    env->DeleteLocalRef(java_key);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(text);
        return false;
    }
    if (text == nullptr) return false;
    const jsize length = env->GetStringLength(text);
    if (env->ExceptionCheck() || length <= 0
        || static_cast<std::size_t>(length)
            >= pathguard::kProviderJavaFilePathCapacityV1) {
        env->ExceptionClear();
        env->DeleteLocalRef(text);
        return false;
    }
    jchar utf16[pathguard::kProviderJavaFilePathCapacityV1]{};
    env->GetStringRegion(text, 0, length, utf16);
    env->DeleteLocalRef(text);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return pathguard::EncodeProviderJavaUtf16ToBytes(
        utf16, static_cast<std::size_t>(length), &output->bytes,
        &output->size);
}

bool ValidProviderRelativePath(std::string_view value) {
    if (value.empty() || value.front() == '/' || value.back() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t end = value.find('/', start);
        const std::size_t count =
            (end == std::string_view::npos ? value.size() : end) - start;
        if (count == 0 || count > 255
            || (count == 1 && value[start] == '.')
            || (count == 2 && value[start] == '.'
                && value[start + 1] == '.')) {
            return false;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

pathguard::OperationMask ReadContentValuesOperations(
        JNIEnv* env, jobjectArray arguments, std::uint8_t argument_index,
        pathguard::ProviderJavaFilePathV1* file_path) {
    jobject values = env->GetObjectArrayElement(arguments, argument_index);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    if (values == nullptr
        || !IsJavaInstance(env, values, "android/content/ContentValues")) {
        env->DeleteLocalRef(values);
        return 0;
    }
    jclass values_class = env->FindClass("android/content/ContentValues");
    const jmethodID size_method = values_class == nullptr ? nullptr
        : env->GetMethodID(values_class, "size", "()I");
    const jmethodID contains_key = values_class == nullptr ? nullptr
        : env->GetMethodID(
            values_class, "containsKey", "(Ljava/lang/String;)Z");
    const jmethodID get_as_string = values_class == nullptr ? nullptr
        : env->GetMethodID(
            values_class, "getAsString", "(Ljava/lang/String;)Ljava/lang/String;");
    jstring display_name = env->NewStringUTF("_display_name");
    if (env->ExceptionCheck() || size_method == nullptr
        || contains_key == nullptr || get_as_string == nullptr
        || display_name == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(display_name);
        env->DeleteLocalRef(values_class);
        env->DeleteLocalRef(values);
        return 0;
    }
    const jint size = env->CallIntMethod(values, size_method);
    const jboolean has_display_name = size <= 0 ? JNI_FALSE
        : env->CallBooleanMethod(values, contains_key, display_name);
    const bool failed = env->ExceptionCheck();
    if (failed) env->ExceptionClear();
    if (!failed && file_path != nullptr) {
        pathguard::ProviderJavaFilePathV1 data_path;
        if (ReadContentValuesText(
                env, values, get_as_string, "_data", &data_path)
            && pathguard::ValidProviderJavaFilePath(data_path.value())) {
            *file_path = data_path;
        } else {
            pathguard::ProviderJavaFilePathV1 relative;
            pathguard::ProviderJavaFilePathV1 name;
            if (ReadContentValuesText(
                    env, values, get_as_string, "relative_path", &relative)
                && ReadContentValuesText(
                    env, values, get_as_string, "_display_name", &name)) {
                while (relative.size != 0
                       && relative.bytes[relative.size - 1] == '/') {
                    --relative.size;
                }
                const std::size_t combined =
                    static_cast<std::size_t>(relative.size) + 1 + name.size;
                if (combined < file_path->bytes.size()) {
                    std::memcpy(file_path->bytes.data(), relative.bytes.data(),
                                relative.size);
                    file_path->bytes[relative.size] = '/';
                    std::memcpy(file_path->bytes.data() + relative.size + 1,
                                name.bytes.data(), name.size);
                    file_path->size = static_cast<std::uint16_t>(combined);
                    if (!ValidProviderRelativePath(file_path->value())) {
                        *file_path = {};
                    }
                }
            }
        }
    }
    env->DeleteLocalRef(display_name);
    env->DeleteLocalRef(values_class);
    env->DeleteLocalRef(values);
    if (failed) return 0;
    return pathguard::ProviderContentValuesOperations(
        size > 0, has_display_name == JNI_TRUE);
}

bool IsJavaInstance(JNIEnv* env, jobject value, const char* class_name) {
    jclass expected = env->FindClass(class_name);
    if (expected == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    const bool matches = env->IsInstanceOf(value, expected) == JNI_TRUE;
    env->DeleteLocalRef(expected);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return matches;
}

bool ReadJavaIdentifierString(
        JNIEnv* env, jstring value,
        pathguard::ProviderJavaIdentifierKind kind,
        pathguard::ProviderJavaIdentifierV1* output) {
    const jsize length = env->GetStringLength(value);
    if (env->ExceptionCheck() || length <= 0
        || static_cast<std::size_t>(length)
            >= pathguard::kProviderJavaIdentifierCapacityV1) {
        env->ExceptionClear();
        return false;
    }
    jchar utf16[pathguard::kProviderJavaIdentifierCapacityV1]{};
    env->GetStringRegion(value, 0, length, utf16);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return pathguard::EncodeProviderJavaIdentifierUtf16(
        kind, utf16, static_cast<std::size_t>(length), output);
}

bool ReadJavaFilePathString(
        JNIEnv* env, jstring value,
        pathguard::ProviderJavaFilePathV1* output) {
    const jsize length = env->GetStringLength(value);
    if (env->ExceptionCheck() || length <= 0
        || static_cast<std::size_t>(length)
            >= pathguard::kProviderJavaFilePathCapacityV1) {
        env->ExceptionClear();
        return false;
    }
    jchar utf16[pathguard::kProviderJavaFilePathCapacityV1]{};
    env->GetStringRegion(value, 0, length, utf16);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return pathguard::EncodeProviderJavaFilePathUtf16(
        utf16, static_cast<std::size_t>(length), output);
}

bool ReadProviderIdentifier(
        JNIEnv* env, jobjectArray arguments,
        const pathguard::ProviderJavaDispatchSpecV1& dispatch,
        pathguard::ProviderJavaIdentifierV1* output,
        pathguard::ProviderJavaFilePathV1* file_path) {
    if (dispatch.identifier_kind
        == pathguard::ProviderJavaIdentifierKind::kNone) return true;
    jobject value = env->GetObjectArrayElement(
        arguments, dispatch.identifier_argument_index);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    if (value == nullptr) return false;

    jstring text = nullptr;
    if (dispatch.identifier_kind == pathguard::ProviderJavaIdentifierKind::kFilePath
        && IsJavaInstance(env, value, "java/io/File")) {
        jclass file_class = env->FindClass("java/io/File");
        const jmethodID get_path = file_class == nullptr ? nullptr
            : env->GetMethodID(file_class, "getPath", "()Ljava/lang/String;");
        if (get_path != nullptr && !env->ExceptionCheck()) {
            text = static_cast<jstring>(
                env->CallObjectMethod(value, get_path));
        }
        env->DeleteLocalRef(file_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            text = nullptr;
        }
        if (text != nullptr
            && !IsJavaInstance(env, text, "java/lang/String")) {
            env->DeleteLocalRef(text);
            text = nullptr;
        }
    } else if (dispatch.identifier_kind
               == pathguard::ProviderJavaIdentifierKind::kDocumentId) {
        if (IsJavaInstance(env, value, "java/lang/String")) {
            text = static_cast<jstring>(value);
        }
    } else if (dispatch.identifier_kind
               == pathguard::ProviderJavaIdentifierKind::kContentUri
               && IsJavaInstance(env, value, "android/net/Uri")) {
        jclass uri_class = env->FindClass("android/net/Uri");
        const jmethodID to_string = uri_class == nullptr ? nullptr
            : env->GetMethodID(uri_class, "toString", "()Ljava/lang/String;");
        if (to_string != nullptr && !env->ExceptionCheck()) {
            text = static_cast<jstring>(
                env->CallObjectMethod(value, to_string));
        }
        env->DeleteLocalRef(uri_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            text = nullptr;
        }
        if (text != nullptr
            && !IsJavaInstance(env, text, "java/lang/String")) {
            env->DeleteLocalRef(text);
            text = nullptr;
        }
    }

    const bool borrowed = text == value;
    const bool valid = text != nullptr && (dispatch.identifier_kind
        == pathguard::ProviderJavaIdentifierKind::kFilePath
            ? ReadJavaFilePathString(env, text, file_path)
            : ReadJavaIdentifierString(env, text, dispatch.identifier_kind, output));
    if (!borrowed) env->DeleteLocalRef(text);
    env->DeleteLocalRef(value);
    return valid;
}

bool ResolveProviderMappingDecision(
        const pathguard::ProviderJavaDispatchRequestV1& request,
        pathguard::ProviderMappingRuntimeFactsV1* facts,
        pathguard::ProviderMappingDecisionV1* decision,
        ProviderRegistryDomain::Guard* registry_guard) {
    if (facts == nullptr || decision == nullptr || g_mapping_resolver == nullptr
        || registry_guard == nullptr
        || !ResolveMappingRuntimeFacts(
            request, facts, registry_guard, g_mapping_user_data)) {
        return false;
    }
    *decision = pathguard::EvaluateProviderMapping(
        pathguard::BuildProviderMappingRequest(
            request, facts->profile_match, facts->supported_operations,
            facts->binding, facts->reverse, facts->runtime_available));
    return true;
}

jobject MakeDispatchRewrite(JNIEnv* env, jobject value) {
    if (env == nullptr || g_state.dispatch_result_class == nullptr
        || g_state.dispatch_result_rewrite == nullptr) return nullptr;
    jobject result = env->CallStaticObjectMethod(
        g_state.dispatch_result_class, g_state.dispatch_result_rewrite, value);
    if (env->ExceptionCheck()) {
        ClearException(env, "create DispatchResult");
        return nullptr;
    }
    return result;
}

void EmitInsertExternalIdentity(
        JNIEnv* env, const pathguard::ProviderJavaDispatchRequestV1& request,
        jobject original_result) {
    if (env == nullptr || original_result == nullptr
        || g_external_identity_sink == nullptr
        || request.method_id
            != pathguard::ProviderJavaMethodId::kMediaInsert
        || request.file_path.size == 0
        || !IsJavaInstance(env, original_result, "android/net/Uri")) {
        return;
    }
    jclass uri_class = env->FindClass("android/net/Uri");
    const jmethodID to_string = uri_class == nullptr ? nullptr
        : env->GetMethodID(uri_class, "toString", "()Ljava/lang/String;");
    jstring text = to_string == nullptr ? nullptr : static_cast<jstring>(
        env->CallObjectMethod(original_result, to_string));
    env->DeleteLocalRef(uri_class);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(text);
        return;
    }
    pathguard::ProviderJavaIdentifierV1 identifier;
    const bool valid = text != nullptr && ReadJavaIdentifierString(
        env, text, pathguard::ProviderJavaIdentifierKind::kContentUri,
        &identifier);
    env->DeleteLocalRef(text);
    if (!valid) return;

    PathGuardLsplantExternalIdentityV1 event{};
    event.version = PATHGUARD_LSPLANT_BRIDGE_API_VERSION;
    event.size = sizeof(event);
    event.identifier_kind =
        PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_CONTENT_URI;
    event.path_kind = request.file_path.bytes[0] == '/'
        ? PATHGUARD_LSPLANT_EXTERNAL_PATH_ABSOLUTE
        : PATHGUARD_LSPLANT_EXTERNAL_PATH_RELATIVE;
    event.file_path_size = request.file_path.size;
    event.identifier_size = identifier.size;
    std::memcpy(event.file_path, request.file_path.bytes.data(),
                request.file_path.size);
    std::memcpy(event.identifier, identifier.bytes.data(), identifier.size);
    g_external_identity_sink(&event, g_external_identity_user_data);
}

jobject MakeProviderReplacement(
        JNIEnv* env, const pathguard::ProviderJavaDispatchRequestV1& request,
        const pathguard::ProviderRouteBindingV1& binding) {
    switch (request.result) {
        case pathguard::ProviderJavaResultKind::kFile: {
            jclass file_class = env->FindClass("java/io/File");
            const jmethodID constructor = file_class == nullptr ? nullptr
                : env->GetMethodID(file_class, "<init>",
                                   "(Ljava/lang/String;)V");
            jstring path = constructor == nullptr ? nullptr
                : env->NewStringUTF(binding.backing_target_path.c_str());
            jobject file = constructor == nullptr || path == nullptr
                ? nullptr : env->NewObject(file_class, constructor, path);
            env->DeleteLocalRef(path);
            env->DeleteLocalRef(file_class);
            if (env->ExceptionCheck()) {
                ClearException(env, "create Provider File result");
                env->DeleteLocalRef(file);
                return nullptr;
            }
            return MakeDispatchRewrite(env, file);
        }
        case pathguard::ProviderJavaResultKind::kDocumentId: {
            jstring document_id = env->NewStringUTF(
                binding.stable_document_id.c_str());
            if (document_id == nullptr || env->ExceptionCheck()) {
                ClearException(env, "create Provider document ID result");
                env->DeleteLocalRef(document_id);
                return nullptr;
            }
            return MakeDispatchRewrite(env, document_id);
        }
        case pathguard::ProviderJavaResultKind::kUri: {
            if (binding.provider_uri.empty()) return nullptr;
            jclass uri_class = env->FindClass("android/net/Uri");
            const jmethodID parse = uri_class == nullptr ? nullptr
                : env->GetStaticMethodID(uri_class, "parse",
                                         "(Ljava/lang/String;)Landroid/net/Uri;");
            jstring text = parse == nullptr ? nullptr
                : env->NewStringUTF(binding.provider_uri.c_str());
            jobject uri = parse == nullptr || text == nullptr ? nullptr
                : env->CallStaticObjectMethod(uri_class, parse, text);
            env->DeleteLocalRef(text);
            env->DeleteLocalRef(uri_class);
            if (env->ExceptionCheck()) {
                ClearException(env, "create Provider Uri result");
                env->DeleteLocalRef(uri);
                return nullptr;
            }
            return MakeDispatchRewrite(env, uri);
        }
        case pathguard::ProviderJavaResultKind::kCount: {
            // Count is immutable observation-only unless an operation adapter
            // supplies an explicit replacement. Preserve the original value.
            return nullptr;
        }
        case pathguard::ProviderJavaResultKind::kCursor:
        case pathguard::ProviderJavaResultKind::kParcelFileDescriptor:
        case pathguard::ProviderJavaResultKind::kVoid:
            return nullptr;
    }
    return nullptr;
}

bool JavaStringEquals(JNIEnv* env, jstring value, const char* expected) {
    if (env == nullptr || value == nullptr || expected == nullptr) return false;
    const char* text = env->GetStringUTFChars(value, nullptr);
    if (text == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    const bool equal = std::strcmp(text, expected) == 0;
    env->ReleaseStringUTFChars(value, text);
    return equal;
}

jobject CopyDocumentCursor(
        JNIEnv* env, jobject original,
        const pathguard::ProviderJavaDispatchRequestV1& request,
        const pathguard::ProviderRouteBindingV1* fixed_binding) {
    if (env == nullptr || original == nullptr) return nullptr;
    jclass cursor_class = env->FindClass("android/database/Cursor");
    const jmethodID get_columns = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "getColumnNames", "()[Ljava/lang/String;");
    const jmethodID get_count = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "getCount", "()I");
    const jmethodID get_position = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "getPosition", "()I");
    const jmethodID move_to = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "moveToPosition", "(I)Z");
    const jmethodID get_type = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "getType", "(I)I");
    const jmethodID get_string = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "getString", "(I)Ljava/lang/String;");
    const jmethodID get_long = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "getLong", "(I)J");
    const jmethodID get_double = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "getDouble", "(I)D");
    const jmethodID get_blob = cursor_class == nullptr ? nullptr
        : env->GetMethodID(cursor_class, "getBlob", "(I)[B");
    if (get_columns == nullptr || get_count == nullptr || get_position == nullptr
        || move_to == nullptr || get_type == nullptr || get_string == nullptr
        || get_long == nullptr || get_double == nullptr || get_blob == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(cursor_class);
        return nullptr;
    }
    auto columns = static_cast<jobjectArray>(
        env->CallObjectMethod(original, get_columns));
    const jint count = env->CallIntMethod(original, get_count);
    const jint original_position = env->CallIntMethod(original, get_position);
    if (env->ExceptionCheck() || columns == nullptr || count < 0) {
        env->ExceptionClear();
        env->DeleteLocalRef(columns);
        env->DeleteLocalRef(cursor_class);
        return nullptr;
    }
    const jsize column_count = env->GetArrayLength(columns);
    jsize data_column = -1;
    jsize document_column = -1;
    for (jsize column = 0; column < column_count; ++column) {
        auto name = static_cast<jstring>(
            env->GetObjectArrayElement(columns, column));
        if (JavaStringEquals(env, name, "_data")) data_column = column;
        if (JavaStringEquals(env, name, "document_id")) {
            document_column = column;
        }
        env->DeleteLocalRef(name);
    }
    jclass matrix_class = env->FindClass("android/database/MatrixCursor");
    const jmethodID matrix_constructor = matrix_class == nullptr ? nullptr
        : env->GetMethodID(matrix_class, "<init>", "([Ljava/lang/String;I)V");
    const jmethodID add_row = matrix_class == nullptr ? nullptr
        : env->GetMethodID(matrix_class, "addRow", "([Ljava/lang/Object;)V");
    if (matrix_constructor == nullptr || add_row == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(matrix_class);
        env->DeleteLocalRef(columns);
        env->DeleteLocalRef(cursor_class);
        return nullptr;
    }
    jobject matrix = env->NewObject(
        matrix_class, matrix_constructor, columns, count);
    if (matrix == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(matrix);
        env->DeleteLocalRef(matrix_class);
        env->DeleteLocalRef(columns);
        env->DeleteLocalRef(cursor_class);
        return nullptr;
    }
    jclass long_class = env->FindClass("java/lang/Long");
    jclass double_class = env->FindClass("java/lang/Double");
    const jmethodID long_value_of = long_class == nullptr ? nullptr
        : env->GetStaticMethodID(long_class, "valueOf", "(J)Ljava/lang/Long;");
    const jmethodID double_value_of = double_class == nullptr ? nullptr
        : env->GetStaticMethodID(double_class, "valueOf",
                                 "(D)Ljava/lang/Double;");
    if (long_value_of == nullptr || double_value_of == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(long_class);
        env->DeleteLocalRef(double_class);
        env->DeleteLocalRef(matrix);
        env->DeleteLocalRef(matrix_class);
        env->DeleteLocalRef(columns);
        env->DeleteLocalRef(cursor_class);
        return nullptr;
    }
    jclass object_class = env->FindClass("java/lang/Object");
    if (object_class == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(object_class);
        env->DeleteLocalRef(long_class);
        env->DeleteLocalRef(double_class);
        env->DeleteLocalRef(matrix);
        env->DeleteLocalRef(matrix_class);
        env->DeleteLocalRef(columns);
        env->DeleteLocalRef(cursor_class);
        return nullptr;
    }
    bool failed_adapter = false;
    bool rewritten = false;
    for (jint row = 0; row < count; ++row) {
        if (env->CallBooleanMethod(original, move_to, row) != JNI_TRUE
            || env->ExceptionCheck()) {
            env->ExceptionClear();
            failed_adapter = true;
            env->DeleteLocalRef(matrix);
            env->DeleteLocalRef(object_class);
            env->DeleteLocalRef(long_class);
            env->DeleteLocalRef(double_class);
            env->DeleteLocalRef(columns);
            env->DeleteLocalRef(cursor_class);
            return nullptr;
        }
        const pathguard::ProviderRouteBindingV1* row_binding = fixed_binding;
        pathguard::ProviderMappingRuntimeFactsV1 row_facts;
        pathguard::ProviderMappingDecisionV1 row_decision;
        ProviderRegistryDomain::Guard row_guard;
        if (row_binding == nullptr
            && (data_column >= 0 || document_column >= 0)) {
            auto row_request = request;
            row_request.file_path = {};
            row_request.identifier = {};
            jstring identity = static_cast<jstring>(env->CallObjectMethod(
                original, get_string,
                data_column >= 0 ? data_column : document_column));
            bool identity_ready = false;
            if (!env->ExceptionCheck() && identity != nullptr) {
                if (data_column >= 0) {
                    row_request.identifier.kind =
                        pathguard::ProviderJavaIdentifierKind::kFilePath;
                    identity_ready = ReadJavaFilePathString(
                        env, identity, &row_request.file_path);
                } else {
                    row_request.identifier.kind =
                        pathguard::ProviderJavaIdentifierKind::kDocumentId;
                    identity_ready = ReadJavaIdentifierString(
                        env, identity, row_request.identifier.kind,
                        &row_request.identifier);
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(identity);
            if (identity_ready && ResolveProviderMappingDecision(
                    row_request, &row_facts, &row_decision, &row_guard)
                && row_decision.rewrite()) {
                row_binding = row_facts.binding;
            }
        }
        jobjectArray values = env->NewObjectArray(
            column_count, object_class, nullptr);
        if (values == nullptr || env->ExceptionCheck()) {
            env->ExceptionClear();
            failed_adapter = true;
            env->DeleteLocalRef(values);
            break;
        }
        for (jsize column = 0; column < column_count; ++column) {
            auto name = static_cast<jstring>(
                env->GetObjectArrayElement(columns, column));
            const jint type = env->CallIntMethod(original, get_type, column);
            jobject value = nullptr;
            if (type == 3) {
                auto text = static_cast<jstring>(
                    env->CallObjectMethod(original, get_string, column));
                std::string visible_relative_path;
                std::string visible_display_name;
                const bool visible_parts_ready = row_binding != nullptr
                    && pathguard::BuildProviderVisibleMediaPath(
                        *row_binding, &visible_relative_path,
                        &visible_display_name);
                if (JavaStringEquals(env, name, "_data")
                    && row_binding != nullptr && text != nullptr
                    && JavaStringEquals(env, text,
                        row_binding->backing_target_path.c_str())) {
                    value = env->NewStringUTF(
                        row_binding->visible_source_path.c_str());
                    rewritten = true;
                } else if (JavaStringEquals(env, name, "relative_path")
                           && text != nullptr && visible_parts_ready) {
                    value = env->NewStringUTF(visible_relative_path.c_str());
                    rewritten = true;
                } else if (JavaStringEquals(env, name, "_display_name")
                           && text != nullptr && visible_parts_ready) {
                    value = env->NewStringUTF(visible_display_name.c_str());
                    rewritten = true;
                } else if (JavaStringEquals(env, name, "document_id")
                           && row_binding != nullptr && text != nullptr
                           && !row_binding->stable_document_id.empty()) {
                    value = env->NewStringUTF(
                        row_binding->stable_document_id.c_str());
                    rewritten = true;
                } else {
                    value = text;
                    text = nullptr;
                }
                env->DeleteLocalRef(text);
            } else if (type == 1) {
                const jlong number = env->CallLongMethod(original, get_long, column);
                value = env->CallStaticObjectMethod(
                    long_class, long_value_of, number);
            } else if (type == 2) {
                const jdouble number = env->CallDoubleMethod(
                    original, get_double, column);
                value = env->CallStaticObjectMethod(
                    double_class, double_value_of, number);
            } else if (type == 4) {
                value = env->CallObjectMethod(original, get_blob, column);
            }
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                failed_adapter = true;
                env->DeleteLocalRef(value);
                env->DeleteLocalRef(name);
                env->DeleteLocalRef(values);
                values = nullptr;
                break;
            }
            env->SetObjectArrayElement(values, column, value);
            env->DeleteLocalRef(value);
            env->DeleteLocalRef(name);
        }
        if (values == nullptr) break;
        env->CallVoidMethod(matrix, add_row, values);
        env->DeleteLocalRef(values);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            failed_adapter = true;
            break;
        }
    }
    if (original_position >= -1) {
        env->CallBooleanMethod(original, move_to, original_position);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    const bool failed = failed_adapter || env->ExceptionCheck();
    if (failed) env->ExceptionClear();
    env->DeleteLocalRef(object_class);
    env->DeleteLocalRef(long_class);
    env->DeleteLocalRef(double_class);
    env->DeleteLocalRef(matrix_class);
    env->DeleteLocalRef(columns);
    env->DeleteLocalRef(cursor_class);
    if (failed || !rewritten) {
        env->DeleteLocalRef(matrix);
        return nullptr;
    }
    return MakeDispatchRewrite(env, matrix);
}

jobject MakeAfterResultReplacement(
        JNIEnv* env, const pathguard::ProviderJavaDispatchRequestV1& request,
        jobject original_result,
        const pathguard::ProviderRouteBindingV1& binding) {
    switch (request.result) {
        case pathguard::ProviderJavaResultKind::kFile:
        case pathguard::ProviderJavaResultKind::kDocumentId:
            return MakeProviderReplacement(env, request, binding);
        case pathguard::ProviderJavaResultKind::kCount: {
            if (original_result == nullptr
                || !IsJavaInstance(env, original_result, "java/lang/Integer")) {
                return nullptr;
            }
            jclass integer_class = env->FindClass("java/lang/Integer");
            const jmethodID int_value = integer_class == nullptr ? nullptr
                : env->GetMethodID(integer_class, "intValue", "()I");
            const jint value = int_value == nullptr ? 0
                : env->CallIntMethod(original_result, int_value);
            const bool failed = env->ExceptionCheck();
            if (failed) env->ExceptionClear();
            env->DeleteLocalRef(integer_class);
            if (failed || int_value == nullptr) return nullptr;
            integer_class = env->FindClass("java/lang/Integer");
            const jmethodID value_of = integer_class == nullptr ? nullptr
                : env->GetStaticMethodID(integer_class, "valueOf",
                                         "(I)Ljava/lang/Integer;");
            jobject boxed = value_of == nullptr ? nullptr
                : env->CallStaticObjectMethod(integer_class, value_of, value);
            env->DeleteLocalRef(integer_class);
            if (env->ExceptionCheck()) {
                ClearException(env, "create Provider count result");
                env->DeleteLocalRef(boxed);
                return nullptr;
            }
            return MakeDispatchRewrite(env, boxed);
        }
        case pathguard::ProviderJavaResultKind::kCursor:
            return CopyDocumentCursor(
                env, original_result, request, &binding);
        case pathguard::ProviderJavaResultKind::kParcelFileDescriptor:
            return nullptr;
        case pathguard::ProviderJavaResultKind::kUri:
            return MakeProviderReplacement(env, request, binding);
        case pathguard::ProviderJavaResultKind::kVoid:
            return nullptr;
    }
    return nullptr;
}

int ReadParcelFileDescriptorFd(JNIEnv* env, jobject descriptor) {
    if (env == nullptr || descriptor == nullptr) return -1;
    jclass descriptor_class = env->FindClass("android/os/ParcelFileDescriptor");
    const jmethodID get_fd = descriptor_class == nullptr ? nullptr
        : env->GetMethodID(descriptor_class, "getFd", "()I");
    const jint fd = get_fd == nullptr ? -1
        : env->CallIntMethod(descriptor, get_fd);
    env->DeleteLocalRef(descriptor_class);
    if (env->ExceptionCheck() || fd < 0) {
        env->ExceptionClear();
        return -1;
    }
    return fd;
}

bool ReadParcelFileDescriptorPath(
        JNIEnv* env, jobject descriptor,
        pathguard::ProviderJavaFilePathV1* output) {
    if (output == nullptr) return false;
    *output = {};
    const int fd = ReadParcelFileDescriptorFd(env, descriptor);
    if (fd < 0) return false;
    char link[64]{};
    const int link_size = std::snprintf(
        link, sizeof(link), "/proc/self/fd/%d", fd);
    char path[pathguard::kProviderJavaFilePathCapacityV1]{};
    const ssize_t path_size = link_size <= 0 ? -1
        : readlink(link, path, sizeof(path) - 1);
    if (path_size <= 0
        || static_cast<std::size_t>(path_size) >= output->bytes.size()) {
        return false;
    }
    std::memcpy(output->bytes.data(), path, static_cast<std::size_t>(path_size));
    output->size = static_cast<std::uint16_t>(path_size);
    if (!pathguard::ValidProviderJavaFilePath(output->value())) {
        *output = {};
        return false;
    }
    return true;
}

struct KernelFileHandle {
    std::uint32_t handle_bytes;
    std::int32_t handle_type;
    std::uint8_t bytes[PATHGUARD_PROVIDER_ROUTE_IDENTITY_HANDLE_MAX];
};

bool VerifyParcelFileDescriptorIdentity(
        JNIEnv* env, jobject descriptor,
        const pathguard::ProviderRouteBindingV1& binding) {
    if (env == nullptr || descriptor == nullptr
        || !binding.fd_identity.Strong()) {
        return false;
    }
    const int fd = ReadParcelFileDescriptorFd(env, descriptor);
    if (fd < 0) return false;
    struct statx identity {};
    if (syscall(SYS_statx, fd, "", AT_EMPTY_PATH | AT_STATX_SYNC_AS_STAT,
                STATX_INO | STATX_BTIME | STATX_TYPE, &identity) != 0) {
        return false;
    }
    char volume[64]{};
    const int volume_size = std::snprintf(
        volume, sizeof(volume), "dev:%u:%u",
        identity.stx_dev_major, identity.stx_dev_minor);
    const std::uint8_t object_type = S_ISDIR(identity.stx_mode) ? 2 : 1;
    if (volume_size <= 0
        || static_cast<std::size_t>(volume_size) >= sizeof(volume)
        || binding.fd_identity.volume != volume
        || binding.fd_identity.object_type != object_type) {
        return false;
    }
    if (binding.fd_identity.kind
            == pathguard::provenance::IdentityKind::kStatxBirthTime) {
        return (identity.stx_mask & (STATX_INO | STATX_BTIME))
                == (STATX_INO | STATX_BTIME)
            && binding.fd_identity.inode == identity.stx_ino
            && binding.fd_identity.birth_seconds == identity.stx_btime.tv_sec
            && binding.fd_identity.birth_nanoseconds
                == identity.stx_btime.tv_nsec;
    }
    if (binding.fd_identity.kind
            != pathguard::provenance::IdentityKind::kFileHandle) {
        return false;
    }
    char descriptor_path[64]{};
    const int descriptor_size = std::snprintf(
        descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d", fd);
    KernelFileHandle handle{};
    handle.handle_bytes = sizeof(handle.bytes);
    int mount_id = 0;
    return descriptor_size > 0
        && static_cast<std::size_t>(descriptor_size) < sizeof(descriptor_path)
        && syscall(SYS_name_to_handle_at, AT_FDCWD, descriptor_path,
                   &handle, &mount_id, AT_SYMLINK_FOLLOW) == 0
        && handle.handle_bytes == binding.fd_identity.handle.size()
        && handle.handle_type == binding.fd_identity.handle_type
        && std::memcmp(handle.bytes, binding.fd_identity.handle.data(),
                       handle.handle_bytes) == 0;
}

void EmitResolvedExternalIdentity(
        const pathguard::ProviderJavaDispatchRequestV1& request,
        const pathguard::ProviderJavaFilePathV1& backing_path) {
    if (g_external_identity_sink == nullptr || backing_path.size == 0
        || (request.identifier.kind
                != pathguard::ProviderJavaIdentifierKind::kContentUri
            && request.identifier.kind
                != pathguard::ProviderJavaIdentifierKind::kDocumentId)) {
        return;
    }
    PathGuardLsplantExternalIdentityV1 event{};
    event.version = PATHGUARD_LSPLANT_BRIDGE_API_VERSION;
    event.size = sizeof(event);
    event.identifier_kind = static_cast<std::uint8_t>(request.identifier.kind);
    event.path_kind = PATHGUARD_LSPLANT_EXTERNAL_PATH_ABSOLUTE;
    event.file_path_size = backing_path.size;
    event.identifier_size = request.identifier.size;
    std::memcpy(event.file_path, backing_path.bytes.data(), backing_path.size);
    std::memcpy(event.identifier, request.identifier.bytes.data(),
                request.identifier.size);
    g_external_identity_sink(&event, g_external_identity_user_data);
}

void EmitExternalFileIdentity(
        JNIEnv* env, const pathguard::ProviderJavaDispatchRequestV1& request,
        jobject original_result) {
    if (env == nullptr || original_result == nullptr
        || g_external_identity_sink == nullptr) {
        return;
    }
    pathguard::ProviderJavaIdentifierV1 identifier;
    pathguard::ProviderJavaFilePathV1 file_path;
    if (request.method_id
            == pathguard::ProviderJavaMethodId::kExternalGetFileForDocId
        && request.identifier.kind
            == pathguard::ProviderJavaIdentifierKind::kDocumentId
        && IsJavaInstance(env, original_result, "java/io/File")) {
        identifier = request.identifier;
        jclass file_class = env->FindClass("java/io/File");
        const jmethodID get_path = file_class == nullptr ? nullptr
            : env->GetMethodID(
                file_class, "getAbsolutePath", "()Ljava/lang/String;");
        jstring path = get_path == nullptr ? nullptr : static_cast<jstring>(
            env->CallObjectMethod(original_result, get_path));
        env->DeleteLocalRef(file_class);
        const bool ready = !env->ExceptionCheck() && path != nullptr
            && ReadJavaFilePathString(env, path, &file_path);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(path);
        if (!ready) return;
    } else if (request.method_id
                   == pathguard::ProviderJavaMethodId::kExternalGetDocIdForFile
               && request.file_path.size != 0
               && IsJavaInstance(env, original_result, "java/lang/String")) {
        file_path = request.file_path;
        if (!ReadJavaIdentifierString(
                env, static_cast<jstring>(original_result),
                pathguard::ProviderJavaIdentifierKind::kDocumentId,
                &identifier)) {
            return;
        }
    } else {
        return;
    }
    PathGuardLsplantExternalIdentityV1 event{};
    event.version = PATHGUARD_LSPLANT_BRIDGE_API_VERSION;
    event.size = sizeof(event);
    event.identifier_kind =
        PATHGUARD_LSPLANT_MAPPING_IDENTIFIER_DOCUMENT_ID;
    event.path_kind = PATHGUARD_LSPLANT_EXTERNAL_PATH_ABSOLUTE;
    event.file_path_size = file_path.size;
    event.identifier_size = identifier.size;
    std::memcpy(event.file_path, file_path.bytes.data(), file_path.size);
    std::memcpy(event.identifier, identifier.bytes.data(), identifier.size);
    g_external_identity_sink(&event, g_external_identity_user_data);
}

jobject DispatchProviderRequest(
        JNIEnv* env, const pathguard::ProviderJavaDispatchRequestV1& request) {
    pathguard::ProviderMappingRuntimeFactsV1 facts;
    pathguard::ProviderMappingDecisionV1 decision;
    ProviderRegistryDomain::Guard registry_guard;
    if (!ResolveProviderMappingDecision(
            request, &facts, &decision, &registry_guard)
        || !decision.rewrite() || facts.binding == nullptr) {
        return nullptr;
    }
    if (request.result != pathguard::ProviderJavaResultKind::kFile
        && request.result != pathguard::ProviderJavaResultKind::kDocumentId) {
        return nullptr;
    }
    return MakeProviderReplacement(env, request, *facts.binding);
}

bool ReadNativeDispatchRequest(
        JNIEnv* env, jint method_id, jobjectArray arguments,
        pathguard::ProviderJavaDispatchRequestV1* output) {
    if (method_id < 0 || arguments == nullptr || output == nullptr) return false;
    const auto* dispatch = pathguard::ProviderJavaDispatchSpec(
        static_cast<pathguard::ProviderJavaMethodId>(method_id));
    if (dispatch == nullptr) return false;
    const jsize argument_count = env->GetArrayLength(arguments);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    if (argument_count
        != static_cast<jsize>(dispatch->callback_argument_count)) return false;
    pathguard::ProviderJavaIdentifierV1 identifier;
    pathguard::ProviderJavaFilePathV1 file_path;
    if (!ReadProviderIdentifier(
            env, arguments, *dispatch, &identifier, &file_path)) {
        return false;
    }
    pathguard::OperationMask operations = dispatch->minimum_operations;
    bool dynamic_operations_ready = !dispatch->arguments_determine_operation;
    switch (dispatch->dynamic_kind) {
        case pathguard::ProviderJavaDynamicKind::kOpenMode:
            operations = ReadOpenMode(
                env, arguments, dispatch->dynamic_argument_index);
            dynamic_operations_ready = operations != 0;
            break;
        case pathguard::ProviderJavaDynamicKind::kContentValues:
            operations = ReadContentValuesOperations(
                env, arguments, dispatch->dynamic_argument_index,
                &file_path);
            dynamic_operations_ready = operations != 0;
            break;
        case pathguard::ProviderJavaDynamicKind::kDeleteTarget:
            // Only a uniquely bound item URI reaches rewrite evaluation.
            // The binding object type then constrains the actual path hook;
            // collection/selection deletes have no binding and fail open.
            operations = dispatch->minimum_operations;
            dynamic_operations_ready = true;
            break;
        case pathguard::ProviderJavaDynamicKind::kNone:
            break;
    }
    const auto request = pathguard::BuildProviderJavaDispatchRequest(
        *dispatch, operations, dynamic_operations_ready, identifier, file_path);
    if (!request.ready()) return false;
    *output = request.request;
    return true;
}

jobject NativeDispatch(
        JNIEnv* env, jclass, jint method_id, jobjectArray arguments) {
    // The native seam is deliberately pass-through until a validated route
    // binding and provenance response are available for this callback.
    pathguard::ProviderJavaDispatchRequestV1 request;
    return ReadNativeDispatchRequest(env, method_id, arguments, &request)
        ? DispatchProviderRequest(env, request) : nullptr;
}

jobject NativeAfterDispatch(
        JNIEnv* env, jclass, jint method_id, jobjectArray arguments,
        jobject original_result) {
    if (method_id < 0) return nullptr;
    const auto* dispatch = pathguard::ProviderJavaDispatchSpec(
        static_cast<pathguard::ProviderJavaMethodId>(method_id));
    if (dispatch == nullptr) return nullptr;
    const auto observation = ObserveNativeProviderResult(
        env, dispatch->result, original_result);
    if (!observation.compatible()) {
        PG_LOGE("Provider result observation rejected: method=%d kind=%u state=%u",
                method_id, static_cast<unsigned>(dispatch->result),
                static_cast<unsigned>(observation.state));
        return nullptr;
    }
    pathguard::ProviderJavaDispatchRequestV1 request;
    if (!ReadNativeDispatchRequest(env, method_id, arguments, &request)) {
        return nullptr;
    }
    EmitInsertExternalIdentity(env, request, original_result);
    EmitExternalFileIdentity(env, request, original_result);
    if (request.result == pathguard::ProviderJavaResultKind::kCursor) {
        return CopyDocumentCursor(env, original_result, request, nullptr);
    }
    pathguard::ProviderMappingRuntimeFactsV1 facts;
    pathguard::ProviderMappingDecisionV1 decision;
    ProviderRegistryDomain::Guard registry_guard;
    if (request.result
            == pathguard::ProviderJavaResultKind::kParcelFileDescriptor) {
        bool resolved = ResolveProviderMappingDecision(
                request, &facts, &decision, &registry_guard)
            && decision.rewrite() && facts.binding != nullptr;
        pathguard::ProviderJavaFilePathV1 backing_path;
        if (!resolved && ReadParcelFileDescriptorPath(
                env, original_result, &backing_path)) {
            auto path_request = request;
            path_request.identifier = {};
            path_request.identifier.kind =
                pathguard::ProviderJavaIdentifierKind::kFilePath;
            path_request.file_path = backing_path;
            resolved = ResolveProviderMappingDecision(
                    path_request, &facts, &decision, &registry_guard)
                && decision.rewrite() && facts.binding != nullptr;
        }
        if (!resolved || !VerifyParcelFileDescriptorIdentity(
                env, original_result, *facts.binding)) {
            return nullptr;
        }
        if (backing_path.size == 0) {
            ReadParcelFileDescriptorPath(env, original_result, &backing_path);
        }
        EmitResolvedExternalIdentity(request, backing_path);
        return MakeDispatchRewrite(env, original_result);
    }
    if (!ResolveProviderMappingDecision(
            request, &facts, &decision, &registry_guard)
        || !decision.rewrite() || facts.binding == nullptr) {
        return nullptr;
    }
    return MakeAfterResultReplacement(
        env, request, original_result, *facts.binding);
}

bool RegisterHookerNatives(JNIEnv* env) {
    static const JNINativeMethod methods[] = {
        {
            const_cast<char*>("nativeDispatch"),
            const_cast<char*>("(I[Ljava/lang/Object;)Ljava/lang/Object;"),
            reinterpret_cast<void*>(NativeDispatch),
        },
        {
            const_cast<char*>("nativeAfterDispatch"),
            const_cast<char*>(
                "(I[Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            reinterpret_cast<void*>(NativeAfterDispatch),
        },
    };
    return env->RegisterNatives(
        g_state.hooker_class, methods,
        static_cast<jint>(sizeof(methods) / sizeof(methods[0]))) == JNI_OK;
}

void ClearException(JNIEnv* env, const char* operation) {
    if (!env->ExceptionCheck()) return;
    env->ExceptionDescribe();
    env->ExceptionClear();
    PG_LOGE("JNI exception: %s", operation);
}

void ResetResult(PathGuardLsplantBridgeResultV1* result) {
    if (result == nullptr) return;
    std::memset(result, 0, sizeof(*result));
    result->version = PATHGUARD_LSPLANT_BRIDGE_API_VERSION;
    result->size = sizeof(*result);
    result->library_loaded = 1;
}

void* InlineHook(void* target, void* replacement) {
    void* backup = nullptr;
    return DobbyHook(target, replacement, &backup) == RS_SUCCESS
        ? backup : nullptr;
}

bool InlineUnhook(void* target) {
    return DobbyDestroy(target) == RS_SUCCESS;
}

void* ResolveArtSymbol(std::string_view symbol) {
    if (g_state.art_handle == nullptr || symbol.empty()) return nullptr;
    const std::string name(symbol);
    void* address = xdl_sym(g_state.art_handle, name.c_str(), nullptr);
    return address != nullptr
        ? address : xdl_dsym(g_state.art_handle, name.c_str(), nullptr);
}

void* ResolveArtSymbolPrefix(std::string_view prefix) {
    if (g_state.art_handle == nullptr || prefix.empty()) return nullptr;
    const std::string name(prefix);
    return xdl_dsym(g_state.art_handle, name.c_str(), nullptr);
}

std::string JavaClassName(std::string_view descriptor) {
    if (descriptor.size() < 3 || descriptor.front() != 'L'
        || descriptor.back() != ';') {
        return {};
    }
    std::string name(descriptor.substr(1, descriptor.size() - 2));
    for (char& value : name) {
        if (value == '/') value = '.';
    }
    return name;
}

jobject ContextClassLoader(JNIEnv* env) {
    jclass thread_class = env->FindClass("java/lang/Thread");
    if (thread_class == nullptr) return nullptr;
    const jmethodID current = env->GetStaticMethodID(
        thread_class, "currentThread", "()Ljava/lang/Thread;");
    const jmethodID get_loader = env->GetMethodID(
        thread_class, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    jobject thread = current == nullptr
        ? nullptr : env->CallStaticObjectMethod(thread_class, current);
    jobject loader = thread == nullptr || get_loader == nullptr
        ? nullptr : env->CallObjectMethod(thread, get_loader);
    env->DeleteLocalRef(thread);
    env->DeleteLocalRef(thread_class);
    if (env->ExceptionCheck()) {
        ClearException(env, "context class loader");
        return nullptr;
    }
    return loader;
}

jobject ApplicationClassLoader(JNIEnv* env) {
    jclass activity_thread = env->FindClass("android/app/ActivityThread");
    const jmethodID current_application = activity_thread == nullptr ? nullptr
        : env->GetStaticMethodID(
            activity_thread, "currentApplication", "()Landroid/app/Application;");
    jobject application = current_application == nullptr ? nullptr
        : env->CallStaticObjectMethod(activity_thread, current_application);
    jclass application_class = application == nullptr ? nullptr
        : env->GetObjectClass(application);
    const jmethodID get_class_loader = application_class == nullptr ? nullptr
        : env->GetMethodID(
            application_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = get_class_loader == nullptr ? nullptr
        : env->CallObjectMethod(application, get_class_loader);
    env->DeleteLocalRef(application_class);
    env->DeleteLocalRef(application);
    env->DeleteLocalRef(activity_thread);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(loader);
        return nullptr;
    }
    return loader;
}

bool LoadHookerDex(JNIEnv* env, const std::uint8_t* dex, std::size_t dex_size) {
    if (dex == nullptr || dex_size < 64 || dex_size > 1024 * 1024) return false;
    if (std::memcmp(dex, "dex\n", 4) != 0) return false;

    void* dex_copy = std::malloc(dex_size);
    if (dex_copy == nullptr) return false;
    std::memcpy(dex_copy, dex, dex_size);
    jobject buffer = env->NewDirectByteBuffer(dex_copy, dex_size);
    jobject parent = ContextClassLoader(env);
    jclass loader_class = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    const jmethodID constructor = loader_class == nullptr ? nullptr
        : env->GetMethodID(loader_class, "<init>",
            "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    jobject loader = constructor == nullptr ? nullptr
        : env->NewObject(loader_class, constructor, buffer, parent);
    jclass class_loader_class = env->FindClass("java/lang/ClassLoader");
    const jmethodID load_class = class_loader_class == nullptr ? nullptr
        : env->GetMethodID(class_loader_class, "loadClass",
            "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring hooker_name = env->NewStringUTF(kHookerClass);
    auto hooker = static_cast<jclass>(loader == nullptr || load_class == nullptr
        ? nullptr : env->CallObjectMethod(loader, load_class, hooker_name));
    if (env->ExceptionCheck() || hooker == nullptr) {
        ClearException(env, "load hooker DEX");
        std::free(dex_copy);
        hooker = nullptr;
    } else {
        g_state.dex_bytes = dex_copy;
        g_state.hooker_class_loader = env->NewGlobalRef(loader);
        g_state.hooker_class =
            static_cast<jclass>(env->NewGlobalRef(hooker));
    }
    if (g_state.hooker_class != nullptr && !RegisterHookerNatives(env)) {
        PG_LOGE("register Provider Hooker natives failed");
        env->DeleteGlobalRef(g_state.hooker_class);
        env->DeleteGlobalRef(g_state.hooker_class_loader);
        g_state.hooker_class = nullptr;
        g_state.hooker_class_loader = nullptr;
    }
    if (g_state.hooker_class != nullptr && loader != nullptr
        && load_class != nullptr) {
        jstring result_name = env->NewStringUTF(
            "dev.pathguard.providerhook.ProviderHooker$DispatchResult");
        jobject result_class = env->CallObjectMethod(
            loader, load_class, result_name);
        env->DeleteLocalRef(result_name);
        if (env->ExceptionCheck() || result_class == nullptr) {
            ClearException(env, "load DispatchResult class");
        } else {
            g_state.dispatch_result_class = static_cast<jclass>(
                env->NewGlobalRef(result_class));
            g_state.dispatch_result_rewrite = env->GetStaticMethodID(
                g_state.dispatch_result_class, "rewrite",
                "(Ljava/lang/Object;)Ldev/pathguard/providerhook/"
                "ProviderHooker$DispatchResult;");
            if (g_state.dispatch_result_rewrite == nullptr
                || env->ExceptionCheck()) {
                ClearException(env, "resolve DispatchResult factory");
                env->DeleteGlobalRef(g_state.dispatch_result_class);
                g_state.dispatch_result_class = nullptr;
                g_state.dispatch_result_rewrite = nullptr;
            }
        }
        env->DeleteLocalRef(result_class);
    }
    env->DeleteLocalRef(hooker);
    env->DeleteLocalRef(hooker_name);
    env->DeleteLocalRef(class_loader_class);
    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(loader_class);
    env->DeleteLocalRef(parent);
    env->DeleteLocalRef(buffer);
    if (g_state.hooker_class == nullptr
        || g_state.hooker_class_loader == nullptr
        || g_state.dispatch_result_class == nullptr
        || g_state.dispatch_result_rewrite == nullptr) return false;
    const jmethodID install_dispatcher = env->GetStaticMethodID(
        g_state.hooker_class, "installNativeDispatcher", "()V");
    if (install_dispatcher == nullptr) {
        ClearException(env, "resolve native dispatcher installer");
        return false;
    }
    env->CallStaticVoidMethod(g_state.hooker_class, install_dispatcher);
    if (env->ExceptionCheck()) {
        ClearException(env, "install native dispatcher");
        return false;
    }
    PG_LOGI("Provider native dispatcher installed as pass-through");
    return true;
}

template <std::size_t Size>
bool InstallMethods(
        JNIEnv* env,
        const std::array<pathguard::ProviderJavaMethodSpecV1, Size>& methods,
        PathGuardLsplantBridgeResultV1* result) {
    const jmethodID constructor = env->GetMethodID(
        g_state.hooker_class, "<init>", "(ILjava/lang/reflect/Method;)V");
    const jmethodID resolve = env->GetStaticMethodID(
        g_state.hooker_class, "resolve",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/ClassLoader;)Ljava/lang/reflect/Method;");
    const jmethodID callback = env->GetMethodID(
        g_state.hooker_class, "callback", "([Ljava/lang/Object;)Ljava/lang/Object;");
    const jmethodID set_backup = env->GetMethodID(
        g_state.hooker_class, "setBackup", "(Ljava/lang/reflect/Method;)V");
    if (constructor == nullptr || resolve == nullptr || callback == nullptr
        || set_backup == nullptr) {
        ClearException(env, "resolve hooker methods");
        result->bridge_errno = ENOSYS;
        return false;
    }
    jobject callback_method = env->ToReflectedMethod(
        g_state.hooker_class, callback, JNI_FALSE);
    if (callback_method == nullptr) {
        ClearException(env, "reflect callback");
        result->bridge_errno = ENOSYS;
        return false;
    }

    bool complete = true;
    for (const auto& spec : methods) {
        const auto bit = pathguard::ProviderJavaMethodBit(spec.id);
        const std::string class_name = JavaClassName(spec.class_descriptor);
        const std::string method_name(spec.name);
        const std::string descriptor(spec.descriptor);
        jstring java_class = env->NewStringUTF(class_name.c_str());
        jstring java_method = env->NewStringUTF(method_name.c_str());
        jstring java_descriptor = env->NewStringUTF(descriptor.c_str());
        jobject target = env->CallStaticObjectMethod(
            g_state.hooker_class, resolve, java_class, java_method,
            java_descriptor, g_state.target_class_loader);
        env->DeleteLocalRef(java_descriptor);
        env->DeleteLocalRef(java_method);
        env->DeleteLocalRef(java_class);
        if (env->ExceptionCheck() || target == nullptr) {
            PG_LOGE("resolve Provider target failed: %s.%s%s target_loader=%s",
                    class_name.c_str(), method_name.c_str(), descriptor.c_str(),
                    g_state.target_class_loader == nullptr ? "null" : "present");
            ClearException(env, "resolve Provider target");
            result->bridge_errno = ENOENT;
            complete = false;
            break;
        }
        result->resolved_methods |= bit;
        jobject hooker = env->NewObject(
            g_state.hooker_class, constructor, static_cast<jint>(spec.id), target);
        jobject backup = hooker == nullptr ? nullptr
            : lsplant::Hook(env, target, hooker, callback_method);
        if (backup == nullptr) {
            ClearException(env, "install LSPlant hook");
            result->bridge_errno = EIO;
            env->DeleteLocalRef(hooker);
            env->DeleteLocalRef(target);
            complete = false;
            break;
        }
        result->installed_hooks |= bit;
        env->CallVoidMethod(hooker, set_backup, backup);
        if (env->ExceptionCheck()) {
            ClearException(env, "set LSPlant backup");
            result->bridge_errno = EIO;
            if (!lsplant::UnHook(env, target)) {
                PG_LOGE("rollback current hook failed: method=%u",
                        static_cast<unsigned>(spec.id));
            }
            env->DeleteLocalRef(hooker);
            env->DeleteLocalRef(target);
            complete = false;
            break;
        }
        result->backup_methods |= bit;
        if (!lsplant::IsHooked(env, target)) {
            result->bridge_errno = EIO;
            env->DeleteLocalRef(hooker);
            env->DeleteLocalRef(target);
            complete = false;
            break;
        }
        result->self_tested_hooks |= bit;
        const std::size_t index = static_cast<std::uint8_t>(spec.id);
        g_state.targets[index] = env->NewGlobalRef(target);
        g_state.hookers[index] = env->NewGlobalRef(hooker);
        env->DeleteLocalRef(hooker);
        env->DeleteLocalRef(target);
    }
    env->DeleteLocalRef(callback_method);
    return complete;
}

void RollbackHooks(JNIEnv* env) {
    for (std::size_t index = g_state.targets.size(); index > 0; --index) {
        jobject target = g_state.targets[index - 1];
        if (target != nullptr) {
            if (lsplant::IsHooked(env, target)) {
                const bool unhooked = lsplant::UnHook(env, target);
                if (!unhooked) PG_LOGE("rollback unhook failed: index=%zu", index - 1);
            }
            env->DeleteGlobalRef(target);
            g_state.targets[index - 1] = nullptr;
        }
        if (g_state.hookers[index - 1] != nullptr) {
            env->DeleteGlobalRef(g_state.hookers[index - 1]);
            g_state.hookers[index - 1] = nullptr;
        }
    }
}

void FinishInstall(const PathGuardLsplantBridgeResultV1& result) {
    {
        std::lock_guard lock(g_state.mutex);
        g_state.final_result = result;
        g_state.install_pending = false;
        g_state.install_complete = true;
    }
    g_state.condition.notify_all();
}

void* InstallWorker(void*) {
    JavaVM* vm = nullptr;
    std::uint8_t provider_kind = PATHGUARD_LSPLANT_PROVIDER_UNKNOWN;
    {
        std::lock_guard lock(g_state.mutex);
        vm = g_state.vm;
        provider_kind = g_state.pending_kind;
    }
    PathGuardLsplantBridgeResultV1 result{};
    ResetResult(&result);
    result.provider_kind = provider_kind;
    result.lsplant_initialized = 1;
    result.hooker_dex_loaded = 1;
    JNIEnv* env = nullptr;
    if (vm == nullptr || vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        result.bridge_errno = ENODEV;
        FinishInstall(result);
        return nullptr;
    }

    jobject target_loader = nullptr;
    for (std::uint32_t attempt = 0;
         attempt < kApplicationLoaderPollCount && target_loader == nullptr;
         ++attempt) {
        target_loader = ApplicationClassLoader(env);
        if (target_loader == nullptr) usleep(kApplicationLoaderPollUs);
    }
    if (target_loader == nullptr) {
        result.bridge_errno = ETIMEDOUT;
        PG_LOGE("Application ClassLoader unavailable: kind=%u", provider_kind);
        vm->DetachCurrentThread();
        FinishInstall(result);
        return nullptr;
    }

    bool complete = false;
    {
        std::lock_guard lock(g_state.mutex);
        if (g_state.target_class_loader != nullptr) {
            env->DeleteGlobalRef(g_state.target_class_loader);
        }
        g_state.target_class_loader = env->NewGlobalRef(target_loader);
        complete = provider_kind == PATHGUARD_LSPLANT_PROVIDER_DOCUMENTS
            ? InstallMethods(
                env, pathguard::kDocumentsProviderJavaMethodsV1, &result)
            : provider_kind == PATHGUARD_LSPLANT_PROVIDER_MEDIA
                ? InstallMethods(
                    env, pathguard::kMediaProviderJavaMethodsV1, &result)
                : false;
        if (!complete) {
            if (result.bridge_errno == 0) result.bridge_errno = EINVAL;
            RollbackHooks(env);
            result.installed_hooks = 0;
            result.backup_methods = 0;
            result.self_tested_hooks = 0;
        } else {
            g_state.installed_kind = provider_kind;
        }
    }
    env->DeleteLocalRef(target_loader);
    if (complete) {
        PG_LOGI("deferred passthrough hook group active: kind=%u methods=%llx",
                provider_kind,
                static_cast<unsigned long long>(result.self_tested_hooks));
    } else {
        PG_LOGE("deferred passthrough hook group rejected: kind=%u errno=%d",
                provider_kind, result.bridge_errno);
    }
    vm->DetachCurrentThread();
    FinishInstall(result);
    return nullptr;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int
pathguard_lsplant_initialize_v1(
        void* jni_env, PathGuardLsplantBridgeResultV1* result) {
    ResetResult(result);
    if (jni_env == nullptr || result == nullptr) return EINVAL;
    auto* env = static_cast<JNIEnv*>(jni_env);
    std::lock_guard lock(g_state.mutex);
    if (g_state.initialized) {
        result->lsplant_initialized = 1;
        return 0;
    }
    g_state.art_handle = xdl_open("libart.so", XDL_DEFAULT);
    if (g_state.art_handle == nullptr) {
        result->bridge_errno = ENOENT;
        return result->bridge_errno;
    }
    const lsplant::InitInfo info{
        .inline_hooker = InlineHook,
        .inline_unhooker = InlineUnhook,
        .art_symbol_resolver = ResolveArtSymbol,
        .art_symbol_prefix_resolver = ResolveArtSymbolPrefix,
        .generated_class_name = "PathGuardHooker_",
        .generated_source_name = "PathGuard",
        .generated_field_name = "hooker",
        .generated_method_name = "{target}",
        .executable_memory_allocator = {},
        .executable_memory_recycler = {},
    };
    g_state.initialized = lsplant::Init(env, info);
    result->lsplant_initialized = g_state.initialized ? 1 : 0;
    result->bridge_errno = g_state.initialized ? 0 : ENOTSUP;
    PG_LOGI("initialize result=%d", g_state.initialized ? 1 : 0);
    return result->bridge_errno;
}

extern "C" __attribute__((visibility("default"))) int
pathguard_lsplant_configure_mapping_v1(
        const PathGuardLsplantMappingRuntimeV1* config) {
    std::lock_guard lock(g_state.mutex);
    if (g_state.installed_kind != PATHGUARD_LSPLANT_PROVIDER_UNKNOWN
        || g_state.install_pending) {
        return EBUSY;
    }
    if (config == nullptr) {
        g_mapping_resolver = nullptr;
        g_mapping_user_data = nullptr;
        g_external_identity_sink = nullptr;
        g_external_identity_user_data = nullptr;
        return 0;
    }
    if (config->version != PATHGUARD_LSPLANT_BRIDGE_API_VERSION
        || config->size < sizeof(PathGuardLsplantMappingRuntimeV1)
        || config->resolver == nullptr) {
        return EINVAL;
    }
    if (config->snapshot != nullptr) {
        const int publish_result = PublishMappingSnapshot(config->snapshot);
        if (publish_result != 0) return publish_result;
    }
    g_mapping_resolver = config->resolver;
    g_mapping_user_data = config->user_data;
    g_external_identity_sink = config->external_identity_sink;
    g_external_identity_user_data = config->external_identity_user_data;
    return 0;
}

extern "C" __attribute__((visibility("default"))) int
pathguard_lsplant_publish_mapping_v1(
        const PathGuardProviderRouteSnapshotV1* snapshot) {
    return PublishMappingSnapshot(snapshot);
}

extern "C" __attribute__((visibility("default"))) int
pathguard_lsplant_install_passthrough_v1(
        void* jni_env, std::uint8_t provider_kind,
        const std::uint8_t* hooker_dex, std::size_t hooker_dex_size,
        PathGuardLsplantBridgeResultV1* result) {
    ResetResult(result);
    if (jni_env == nullptr || result == nullptr) return EINVAL;
    result->provider_kind = provider_kind;
    auto* env = static_cast<JNIEnv*>(jni_env);
    std::unique_lock lock(g_state.mutex);
    result->lsplant_initialized = g_state.initialized ? 1 : 0;
    if (!g_state.initialized) {
        result->bridge_errno = ENOTSUP;
        return result->bridge_errno;
    }
    if (g_state.installed_kind != PATHGUARD_LSPLANT_PROVIDER_UNKNOWN
        || g_state.install_pending) {
        result->bridge_errno = EALREADY;
        return result->bridge_errno;
    }
    if (!LoadHookerDex(env, hooker_dex, hooker_dex_size)) {
        result->bridge_errno = ENOEXEC;
        return result->bridge_errno;
    }
    result->hooker_dex_loaded = 1;
    if (provider_kind != PATHGUARD_LSPLANT_PROVIDER_DOCUMENTS
        && provider_kind != PATHGUARD_LSPLANT_PROVIDER_MEDIA) {
        result->bridge_errno = EINVAL;
        return result->bridge_errno;
    }
    if (env->GetJavaVM(&g_state.vm) != JNI_OK || g_state.vm == nullptr) {
        result->bridge_errno = ENODEV;
        return result->bridge_errno;
    }
    g_state.pending_kind = provider_kind;
    g_state.install_pending = true;
    g_state.install_complete = false;
    result->bridge_errno = EINPROGRESS;
    g_state.final_result = *result;
    pthread_t worker{};
    const int thread_result = pthread_create(&worker, nullptr, InstallWorker, nullptr);
    if (thread_result != 0) {
        g_state.install_pending = false;
        g_state.install_complete = true;
        result->bridge_errno = thread_result;
        g_state.final_result = *result;
        return result->bridge_errno;
    }
    pthread_detach(worker);
    PG_LOGI("deferred passthrough hook scheduled: kind=%u", provider_kind);
    return EINPROGRESS;
}

extern "C" __attribute__((visibility("default"))) int
pathguard_lsplant_wait_passthrough_v1(
        std::uint32_t timeout_ms, PathGuardLsplantBridgeResultV1* result) {
    ResetResult(result);
    if (result == nullptr || timeout_ms == 0) return EINVAL;
    std::unique_lock lock(g_state.mutex);
    if (!g_state.install_pending && !g_state.install_complete) {
        result->bridge_errno = ENOENT;
        return result->bridge_errno;
    }
    const bool complete = g_state.condition.wait_for(
        lock, std::chrono::milliseconds(timeout_ms), [] {
            return g_state.install_complete;
        });
    if (!complete) {
        result->bridge_errno = ETIMEDOUT;
        return result->bridge_errno;
    }
    *result = g_state.final_result;
    return result->bridge_errno;
}
