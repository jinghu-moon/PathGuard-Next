#include "pathguard/media_query_hook.hpp"

#include <android/log.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits.h>

#include "zygisk.hpp"

namespace pathguard::media_query {
namespace {

constexpr char kLogTag[] = "PathGuard";
constexpr char kProviderDescriptor[] = "android.content.IContentProvider";
constexpr char kMediaAuthority[] = "media";
constexpr char kStorageRootPrefix[] = "/storage/emulated/";
constexpr char kSelectionKey[] = "android:query-arg-sql-selection";
constexpr char kSelectionArgsKey[] = "android:query-arg-sql-selection-args";
constexpr std::size_t kMaxDenyPaths = 8;
constexpr std::size_t kMaxSelectionSize = 8192;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

using TransactNative = jboolean (*)(JNIEnv*, jobject, jint, jobject, jobject, jint);

TransactNative g_original_transact = nullptr;
constexpr jint kQueryTransaction = 1;
char g_deny_paths[kMaxDenyPaths][PATH_MAX]{};
std::size_t g_deny_path_count = 0;
unsigned g_user_id = 0;
std::atomic_uint g_rewrite_log_count{0};

jclass g_string_class = nullptr;
jclass g_parcel_class = nullptr;
jclass g_bundle_class = nullptr;
jobject g_uri_creator = nullptr;
jobject g_attribution_source_creator = nullptr;
jstring g_provider_descriptor = nullptr;
jstring g_selection_key = nullptr;
jstring g_selection_args_key = nullptr;

jmethodID g_binder_get_interface_descriptor = nullptr;
jmethodID g_parcel_obtain = nullptr;
jmethodID g_parcel_recycle = nullptr;
jmethodID g_parcel_data_position = nullptr;
jmethodID g_parcel_data_size = nullptr;
jmethodID g_parcel_set_data_position = nullptr;
jmethodID g_parcel_enforce_interface = nullptr;
jmethodID g_parcel_write_interface_token = nullptr;
jmethodID g_parcel_create_string_array = nullptr;
jmethodID g_parcel_write_string_array = nullptr;
jmethodID g_parcel_read_bundle = nullptr;
jmethodID g_parcel_write_bundle = nullptr;
jmethodID g_parcel_read_strong_binder = nullptr;
jmethodID g_parcel_write_strong_binder = nullptr;
jmethodID g_creator_create_from_parcel = nullptr;
jmethodID g_attribution_write_to_parcel = nullptr;
jmethodID g_uri_write_to_parcel = nullptr;
jmethodID g_uri_get_authority = nullptr;
jmethodID g_bundle_constructor = nullptr;
jmethodID g_bundle_get_string = nullptr;
jmethodID g_bundle_put_string = nullptr;
jmethodID g_bundle_get_string_array = nullptr;
jmethodID g_bundle_put_string_array = nullptr;

bool ClearException(JNIEnv* env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    return true;
}

bool Append(char* output, std::size_t capacity, const char* value) {
    const std::size_t current = strlen(output);
    const std::size_t addition = strlen(value);
    if (current + addition + 1 > capacity) return false;
    memcpy(output + current, value, addition + 1);
    return true;
}

bool BuildFilter(char* selection, std::size_t selection_capacity,
                 char arguments[][PATH_MAX + 4], std::size_t* argument_count) {
    selection[0] = '\0';
    *argument_count = 0;
    constexpr char kClause[] = "(_data IS NULL OR _data NOT LIKE ?)";

    char storage_prefix[64]{};
    if (snprintf(storage_prefix, sizeof(storage_prefix), "/storage/emulated/%u/",
                 g_user_id) <= 0) {
        return false;
    }
    for (std::size_t index = 0; index < g_deny_path_count; ++index) {
        const char* path = g_deny_paths[index];
        if (strncmp(path, storage_prefix, strlen(storage_prefix)) != 0) return false;
        if (index > 0 && !Append(selection, selection_capacity, " AND ")) return false;
        if (!Append(selection, selection_capacity, kClause)) return false;
        if (snprintf(arguments[*argument_count], PATH_MAX + 4, "%s/%%", path) <= 0) {
            return false;
        }
        ++*argument_count;
    }
    return *argument_count > 0;
}

bool IsMediaUri(JNIEnv* env, jobject uri) {
    if (uri == nullptr) return false;
    auto authority = static_cast<jstring>(env->CallObjectMethod(uri, g_uri_get_authority));
    if (ClearException(env) || authority == nullptr) return false;
    const char* value = env->GetStringUTFChars(authority, nullptr);
    const bool matches = value != nullptr && strcmp(value, kMediaAuthority) == 0;
    if (value != nullptr) env->ReleaseStringUTFChars(authority, value);
    env->DeleteLocalRef(authority);
    return matches;
}

jobjectArray MergeArguments(JNIEnv* env, jobjectArray original,
                            char extra[][PATH_MAX + 4], std::size_t extra_count) {
    const jsize original_count = original == nullptr ? 0 : env->GetArrayLength(original);
    auto merged = env->NewObjectArray(
        original_count + static_cast<jsize>(extra_count), g_string_class, nullptr);
    if (merged == nullptr || ClearException(env)) return nullptr;

    for (jsize index = 0; index < original_count; ++index) {
        jobject value = env->GetObjectArrayElement(original, index);
        env->SetObjectArrayElement(merged, index, value);
        if (value != nullptr) env->DeleteLocalRef(value);
    }
    for (std::size_t index = 0; index < extra_count; ++index) {
        jstring value = env->NewStringUTF(extra[index]);
        if (value == nullptr || ClearException(env)) {
            if (value != nullptr) env->DeleteLocalRef(value);
            env->DeleteLocalRef(merged);
            return nullptr;
        }
        env->SetObjectArrayElement(
            merged, original_count + static_cast<jsize>(index), value);
        env->DeleteLocalRef(value);
    }
    if (ClearException(env)) {
        env->DeleteLocalRef(merged);
        return nullptr;
    }
    return merged;
}

bool ApplyFilterToBundle(JNIEnv* env, jobject bundle) {
    char filter[kMaxSelectionSize]{};
    char extra_arguments[kMaxDenyPaths][PATH_MAX + 4]{};
    std::size_t extra_count = 0;
    if (!BuildFilter(filter, sizeof(filter), extra_arguments, &extra_count)) return false;

    auto original_selection = static_cast<jstring>(
        env->CallObjectMethod(bundle, g_bundle_get_string, g_selection_key));
    auto original_arguments = static_cast<jobjectArray>(
        env->CallObjectMethod(bundle, g_bundle_get_string_array, g_selection_args_key));
    if (ClearException(env)) {
        if (original_selection != nullptr) env->DeleteLocalRef(original_selection);
        if (original_arguments != nullptr) env->DeleteLocalRef(original_arguments);
        return false;
    }

    const char* original_text = original_selection == nullptr
        ? nullptr
        : env->GetStringUTFChars(original_selection, nullptr);
    char merged_selection[kMaxSelectionSize]{};
    if (original_text != nullptr && original_text[0] != '\0') {
        const int length = snprintf(
            merged_selection, sizeof(merged_selection), "(%s) AND (%s)",
            original_text, filter);
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(merged_selection)) {
            env->ReleaseStringUTFChars(original_selection, original_text);
            env->DeleteLocalRef(original_selection);
            if (original_arguments != nullptr) env->DeleteLocalRef(original_arguments);
            return false;
        }
    } else {
        strcpy(merged_selection, filter);
    }

    auto merged_arguments = MergeArguments(env, original_arguments, extra_arguments, extra_count);
    auto merged_selection_string = env->NewStringUTF(merged_selection);
    const bool valid = merged_arguments != nullptr
        && merged_selection_string != nullptr
        && !ClearException(env);
    if (valid) {
        env->CallVoidMethod(
            bundle, g_bundle_put_string, g_selection_key, merged_selection_string);
        env->CallVoidMethod(
            bundle, g_bundle_put_string_array, g_selection_args_key, merged_arguments);
    }
    const bool applied = valid && !ClearException(env);

    if (original_text != nullptr) {
        env->ReleaseStringUTFChars(original_selection, original_text);
    }
    if (original_selection != nullptr) env->DeleteLocalRef(original_selection);
    if (original_arguments != nullptr) env->DeleteLocalRef(original_arguments);
    if (merged_arguments != nullptr) env->DeleteLocalRef(merged_arguments);
    if (merged_selection_string != nullptr) env->DeleteLocalRef(merged_selection_string);
    return applied;
}

void DeleteLocal(JNIEnv* env, jobject value) {
    if (value != nullptr) env->DeleteLocalRef(value);
}

jboolean CallOriginal(JNIEnv* env, jobject binder, jint code, jobject data,
                      jobject reply, jint flags) {
    if (g_original_transact == nullptr) return JNI_FALSE;
    return g_original_transact(env, binder, code, data, reply, flags);
}

jboolean TransactHook(JNIEnv* env, jobject binder, jint code, jobject data,
                       jobject reply, jint flags) {
    if (g_original_transact == nullptr || code != kQueryTransaction || data == nullptr) {
        return CallOriginal(env, binder, code, data, reply, flags);
    }

    auto descriptor = static_cast<jstring>(
        env->CallObjectMethod(binder, g_binder_get_interface_descriptor));
    if (ClearException(env) || descriptor == nullptr) {
        DeleteLocal(env, descriptor);
        return CallOriginal(env, binder, code, data, reply, flags);
    }
    const char* descriptor_text = env->GetStringUTFChars(descriptor, nullptr);
    const bool is_provider = descriptor_text != nullptr
        && strcmp(descriptor_text, kProviderDescriptor) == 0;
    if (descriptor_text != nullptr) {
        env->ReleaseStringUTFChars(descriptor, descriptor_text);
    }
    env->DeleteLocalRef(descriptor);
    if (!is_provider) return CallOriginal(env, binder, code, data, reply, flags);

    const jint original_position = env->CallIntMethod(data, g_parcel_data_position);
    env->CallVoidMethod(data, g_parcel_set_data_position, 0);
    env->CallVoidMethod(data, g_parcel_enforce_interface, g_provider_descriptor);
    jobject attribution = env->CallObjectMethod(
        g_attribution_source_creator, g_creator_create_from_parcel, data);
    jobject uri = env->CallObjectMethod(g_uri_creator, g_creator_create_from_parcel, data);
    auto projection = static_cast<jobjectArray>(
        env->CallObjectMethod(data, g_parcel_create_string_array));
    jobject query_args = env->CallObjectMethod(data, g_parcel_read_bundle, nullptr);
    jobject observer = env->CallObjectMethod(data, g_parcel_read_strong_binder);
    jobject cancellation = env->CallObjectMethod(data, g_parcel_read_strong_binder);
    const jint consumed = env->CallIntMethod(data, g_parcel_data_position);
    const jint data_size = env->CallIntMethod(data, g_parcel_data_size);
    env->CallVoidMethod(data, g_parcel_set_data_position, original_position);

    if (ClearException(env) || consumed != data_size || !IsMediaUri(env, uri)) {
        DeleteLocal(env, attribution);
        DeleteLocal(env, uri);
        DeleteLocal(env, projection);
        DeleteLocal(env, query_args);
        DeleteLocal(env, observer);
        DeleteLocal(env, cancellation);
        return CallOriginal(env, binder, code, data, reply, flags);
    }

    if (query_args == nullptr) {
        query_args = env->NewObject(g_bundle_class, g_bundle_constructor);
    }
    if (query_args == nullptr || ClearException(env) || !ApplyFilterToBundle(env, query_args)) {
        DeleteLocal(env, attribution);
        DeleteLocal(env, uri);
        DeleteLocal(env, projection);
        DeleteLocal(env, query_args);
        DeleteLocal(env, observer);
        DeleteLocal(env, cancellation);
        return CallOriginal(env, binder, code, data, reply, flags);
    }

    jobject rewritten = env->CallStaticObjectMethod(g_parcel_class, g_parcel_obtain);
    if (rewritten == nullptr || ClearException(env)) {
        DeleteLocal(env, rewritten);
        DeleteLocal(env, attribution);
        DeleteLocal(env, uri);
        DeleteLocal(env, projection);
        DeleteLocal(env, query_args);
        DeleteLocal(env, observer);
        DeleteLocal(env, cancellation);
        return CallOriginal(env, binder, code, data, reply, flags);
    }

    env->CallVoidMethod(rewritten, g_parcel_write_interface_token, g_provider_descriptor);
    env->CallVoidMethod(attribution, g_attribution_write_to_parcel, rewritten, 0);
    env->CallVoidMethod(uri, g_uri_write_to_parcel, rewritten, 0);
    env->CallVoidMethod(rewritten, g_parcel_write_string_array, projection);
    env->CallVoidMethod(rewritten, g_parcel_write_bundle, query_args);
    env->CallVoidMethod(rewritten, g_parcel_write_strong_binder, observer);
    env->CallVoidMethod(rewritten, g_parcel_write_strong_binder, cancellation);
    if (ClearException(env)) {
        env->CallVoidMethod(rewritten, g_parcel_recycle);
        DeleteLocal(env, rewritten);
        DeleteLocal(env, attribution);
        DeleteLocal(env, uri);
        DeleteLocal(env, projection);
        DeleteLocal(env, query_args);
        DeleteLocal(env, observer);
        DeleteLocal(env, cancellation);
        return CallOriginal(env, binder, code, data, reply, flags);
    }

    if (g_rewrite_log_count.fetch_add(1, std::memory_order_relaxed) < 20) {
        LOGI("media query constrained: deny_paths=%zu", g_deny_path_count);
    }
    const jboolean result = CallOriginal(env, binder, code, rewritten, reply, flags);
    env->CallVoidMethod(rewritten, g_parcel_recycle);
    ClearException(env);
    DeleteLocal(env, rewritten);
    DeleteLocal(env, attribution);
    DeleteLocal(env, uri);
    DeleteLocal(env, projection);
    DeleteLocal(env, query_args);
    DeleteLocal(env, observer);
    DeleteLocal(env, cancellation);
    return result;
}

jobject GlobalRef(JNIEnv* env, jobject value) {
    if (value == nullptr) return nullptr;
    jobject global = env->NewGlobalRef(value);
    env->DeleteLocalRef(value);
    return global;
}

bool InitializeJni(JNIEnv* env) {
    jclass binder_class = env->FindClass("android/os/BinderProxy");
    jclass parcel_class = env->FindClass("android/os/Parcel");
    jclass uri_class = env->FindClass("android/net/Uri");
    jclass bundle_class = env->FindClass("android/os/Bundle");
    jclass attribution_class = env->FindClass("android/content/AttributionSource");
    jclass string_class = env->FindClass("java/lang/String");
    jclass creator_class = env->FindClass("android/os/Parcelable$Creator");
    if (ClearException(env) || binder_class == nullptr || parcel_class == nullptr
        || uri_class == nullptr || bundle_class == nullptr
        || attribution_class == nullptr || string_class == nullptr
        || creator_class == nullptr) {
        return false;
    }

    g_binder_get_interface_descriptor = env->GetMethodID(
        binder_class, "getInterfaceDescriptor", "()Ljava/lang/String;");
    g_parcel_obtain = env->GetStaticMethodID(
        parcel_class, "obtain", "()Landroid/os/Parcel;");
    g_parcel_recycle = env->GetMethodID(parcel_class, "recycle", "()V");
    g_parcel_data_position = env->GetMethodID(parcel_class, "dataPosition", "()I");
    g_parcel_data_size = env->GetMethodID(parcel_class, "dataSize", "()I");
    g_parcel_set_data_position = env->GetMethodID(parcel_class, "setDataPosition", "(I)V");
    g_parcel_enforce_interface = env->GetMethodID(
        parcel_class, "enforceInterface", "(Ljava/lang/String;)V");
    g_parcel_write_interface_token = env->GetMethodID(
        parcel_class, "writeInterfaceToken", "(Ljava/lang/String;)V");
    g_parcel_create_string_array = env->GetMethodID(
        parcel_class, "createStringArray", "()[Ljava/lang/String;");
    g_parcel_write_string_array = env->GetMethodID(
        parcel_class, "writeStringArray", "([Ljava/lang/String;)V");
    g_parcel_read_bundle = env->GetMethodID(
        parcel_class, "readBundle", "(Ljava/lang/ClassLoader;)Landroid/os/Bundle;");
    g_parcel_write_bundle = env->GetMethodID(
        parcel_class, "writeBundle", "(Landroid/os/Bundle;)V");
    g_parcel_read_strong_binder = env->GetMethodID(
        parcel_class, "readStrongBinder", "()Landroid/os/IBinder;");
    g_parcel_write_strong_binder = env->GetMethodID(
        parcel_class, "writeStrongBinder", "(Landroid/os/IBinder;)V");
    g_creator_create_from_parcel = env->GetMethodID(
        creator_class, "createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;");
    g_attribution_write_to_parcel = env->GetMethodID(
        attribution_class, "writeToParcel", "(Landroid/os/Parcel;I)V");
    g_uri_write_to_parcel = env->GetMethodID(
        uri_class, "writeToParcel", "(Landroid/os/Parcel;I)V");
    g_uri_get_authority = env->GetMethodID(
        uri_class, "getAuthority", "()Ljava/lang/String;");
    g_bundle_constructor = env->GetMethodID(bundle_class, "<init>", "()V");
    g_bundle_get_string = env->GetMethodID(
        bundle_class, "getString", "(Ljava/lang/String;)Ljava/lang/String;");
    g_bundle_put_string = env->GetMethodID(
        bundle_class, "putString", "(Ljava/lang/String;Ljava/lang/String;)V");
    g_bundle_get_string_array = env->GetMethodID(
        bundle_class, "getStringArray", "(Ljava/lang/String;)[Ljava/lang/String;");
    g_bundle_put_string_array = env->GetMethodID(
        bundle_class, "putStringArray", "(Ljava/lang/String;[Ljava/lang/String;)V");

    jfieldID uri_creator_field = env->GetStaticFieldID(
        uri_class, "CREATOR", "Landroid/os/Parcelable$Creator;");
    jfieldID attribution_creator_field = env->GetStaticFieldID(
        attribution_class, "CREATOR", "Landroid/os/Parcelable$Creator;");
    if (ClearException(env) || uri_creator_field == nullptr
        || attribution_creator_field == nullptr) {
        return false;
    }
    g_uri_creator = GlobalRef(env, env->GetStaticObjectField(uri_class, uri_creator_field));
    g_attribution_source_creator = GlobalRef(
        env, env->GetStaticObjectField(attribution_class, attribution_creator_field));
    g_string_class = static_cast<jclass>(env->NewGlobalRef(string_class));
    g_parcel_class = static_cast<jclass>(env->NewGlobalRef(parcel_class));
    g_bundle_class = static_cast<jclass>(env->NewGlobalRef(bundle_class));
    g_provider_descriptor = static_cast<jstring>(
        GlobalRef(env, env->NewStringUTF(kProviderDescriptor)));
    g_selection_key = static_cast<jstring>(GlobalRef(env, env->NewStringUTF(kSelectionKey)));
    g_selection_args_key = static_cast<jstring>(
        GlobalRef(env, env->NewStringUTF(kSelectionArgsKey)));

    env->DeleteLocalRef(binder_class);
    env->DeleteLocalRef(parcel_class);
    env->DeleteLocalRef(uri_class);
    env->DeleteLocalRef(bundle_class);
    env->DeleteLocalRef(attribution_class);
    env->DeleteLocalRef(string_class);
    env->DeleteLocalRef(creator_class);

    return !ClearException(env)
        && g_uri_creator != nullptr
        && g_attribution_source_creator != nullptr
        && g_string_class != nullptr
        && g_parcel_class != nullptr
        && g_bundle_class != nullptr
        && g_provider_descriptor != nullptr
        && g_selection_key != nullptr
        && g_selection_args_key != nullptr
        && g_binder_get_interface_descriptor != nullptr
        && g_parcel_obtain != nullptr
        && g_parcel_recycle != nullptr
        && g_parcel_data_position != nullptr
        && g_parcel_data_size != nullptr
        && g_parcel_set_data_position != nullptr
        && g_parcel_enforce_interface != nullptr
        && g_parcel_write_interface_token != nullptr
        && g_parcel_create_string_array != nullptr
        && g_parcel_write_string_array != nullptr
        && g_parcel_read_bundle != nullptr
        && g_parcel_write_bundle != nullptr
        && g_parcel_read_strong_binder != nullptr
        && g_parcel_write_strong_binder != nullptr
        && g_creator_create_from_parcel != nullptr
        && g_attribution_write_to_parcel != nullptr
        && g_uri_write_to_parcel != nullptr
        && g_uri_get_authority != nullptr
        && g_bundle_constructor != nullptr
        && g_bundle_get_string != nullptr
        && g_bundle_put_string != nullptr
        && g_bundle_get_string_array != nullptr
        && g_bundle_put_string_array != nullptr;
}

}  // namespace

bool Install(zygisk::Api* api, JNIEnv* env, const char* const* deny_paths,
             std::size_t deny_path_count, int uid) {
    if (api == nullptr || env == nullptr || deny_paths == nullptr
        || deny_path_count == 0 || deny_path_count > kMaxDenyPaths || uid < 0) {
        LOGE("media query hook invalid arguments: paths=%zu uid=%d",
             deny_path_count, uid);
        return false;
    }
    g_original_transact = nullptr;
    g_deny_path_count = 0;
    g_user_id = static_cast<unsigned>(uid) / 100000u;
    char user_prefix[64]{};
    if (snprintf(user_prefix, sizeof(user_prefix), "/storage/emulated/0/") <= 0) {
        LOGE("media query hook user prefix formatting failed");
        return false;
    }
    for (std::size_t index = 0; index < deny_path_count; ++index) {
        if (deny_paths[index] == nullptr || strlen(deny_paths[index]) >= PATH_MAX
            || strncmp(deny_paths[index], user_prefix, strlen(user_prefix)) != 0) {
            LOGE("media query hook invalid deny path: index=%zu path=%s", index,
                 deny_paths[index] == nullptr ? "<null>" : deny_paths[index]);
            return false;
        }
        const int written = snprintf(g_deny_paths[index], PATH_MAX,
                                     "/storage/emulated/%u%s", g_user_id,
                                     deny_paths[index] + strlen(user_prefix) - 1);
        if (written <= 0 || written >= PATH_MAX) {
            LOGE("media query hook path conversion failed: index=%zu", index);
            return false;
        }
        ++g_deny_path_count;
    }

    if (!InitializeJni(env)) {
        LOGE("media query hook JNI initialization failed");
        return false;
    }
    JNINativeMethod methods[] = {
        {const_cast<char*>("transactNative"),
         const_cast<char*>("(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z"),
         reinterpret_cast<void*>(TransactHook)},
    };
    api->hookJniNativeMethods(env, "android/os/BinderProxy", methods, 1);
    g_original_transact = reinterpret_cast<TransactNative>(methods[0].fnPtr);
    if (g_original_transact == nullptr) {
        LOGE("media query hook registration failed");
        return false;
    }
    LOGI("media query hook installed: deny_paths=%zu user=%u", g_deny_path_count, g_user_id);
    return true;
}

}  // namespace pathguard::media_query
