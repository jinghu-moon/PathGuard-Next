#include "pathguard/provider_lsplant_bridge_api.h"

#include <android/log.h>
#include <dobby.h>
#include <jni.h>
#include <xdl.h>
#include <pthread.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

#include "lsplant.hpp"
#include "pathguard/provider_lsplant_bridge.h"
#include "pathguard/provider_mapping.h"

namespace {

constexpr char kLogTag[] = "PathGuardLsplant";
constexpr char kHookerClass[] = "dev.pathguard.providerhook.ProviderHooker";
constexpr std::uint32_t kApplicationLoaderPollCount = 350;
constexpr useconds_t kApplicationLoaderPollUs = 10000;

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
    PathGuardLsplantBridgeResultV1 final_result{};
    std::array<jobject, pathguard::kProviderJavaMethodCountV1> targets{};
    std::array<jobject, pathguard::kProviderJavaMethodCountV1> hookers{};
};

HookState g_state;

bool IsJavaInstance(JNIEnv* env, jobject value, const char* class_name);

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

pathguard::OperationMask ReadContentValuesOperations(
        JNIEnv* env, jobjectArray arguments, std::uint8_t argument_index) {
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
    jstring display_name = env->NewStringUTF("_display_name");
    if (env->ExceptionCheck() || size_method == nullptr
        || contains_key == nullptr || display_name == nullptr) {
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

jobject DispatchProviderRequest(
        JNIEnv*, const pathguard::ProviderJavaDispatchRequestV1& request) {
    // A validated request is available, but route/provenance wiring and Java
    // result construction remain disabled until their contracts are complete.
    const auto mapping_request = pathguard::BuildProviderMappingRequest(
        request, {}, 0, nullptr, {}, false);
    const auto decision = pathguard::EvaluateProviderMapping(mapping_request);
    if (decision.disposition != pathguard::ProviderMappingDisposition::kPass) {
        return nullptr;
    }
    return nullptr;
}

jobject NativeDispatch(
        JNIEnv* env, jclass, jint method_id, jobjectArray arguments) {
    // The native seam is deliberately pass-through until a validated route
    // binding and provenance response are available for this callback.
    if (method_id < 0 || arguments == nullptr) return nullptr;
    const auto* dispatch = pathguard::ProviderJavaDispatchSpec(
        static_cast<pathguard::ProviderJavaMethodId>(method_id));
    if (dispatch == nullptr) return nullptr;
    const jsize argument_count = env->GetArrayLength(arguments);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    if (argument_count
        != static_cast<jsize>(dispatch->callback_argument_count)) return nullptr;
    pathguard::ProviderJavaIdentifierV1 identifier;
    pathguard::ProviderJavaFilePathV1 file_path;
    if (!ReadProviderIdentifier(
            env, arguments, *dispatch, &identifier, &file_path)) {
        return nullptr;
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
                env, arguments, dispatch->dynamic_argument_index);
            dynamic_operations_ready = operations != 0;
            break;
        case pathguard::ProviderJavaDynamicKind::kDeleteTarget:
            dynamic_operations_ready = false;
            break;
        case pathguard::ProviderJavaDynamicKind::kNone:
            break;
    }
    const auto request = pathguard::BuildProviderJavaDispatchRequest(
        *dispatch, operations, dynamic_operations_ready, identifier, file_path);
    if (!request.ready()) return nullptr;
    return DispatchProviderRequest(env, request.request);
}

bool RegisterHookerNatives(JNIEnv* env) {
    static const JNINativeMethod methods[] = {
        {
            const_cast<char*>("nativeDispatch"),
            const_cast<char*>("(I[Ljava/lang/Object;)Ljava/lang/Object;"),
            reinterpret_cast<void*>(NativeDispatch),
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
    env->DeleteLocalRef(hooker);
    env->DeleteLocalRef(hooker_name);
    env->DeleteLocalRef(class_loader_class);
    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(loader_class);
    env->DeleteLocalRef(parent);
    env->DeleteLocalRef(buffer);
    if (g_state.hooker_class == nullptr
        || g_state.hooker_class_loader == nullptr) return false;
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
