#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "hide_vfs_probe_runner.h"

namespace {

class UtfChars {
public:
    UtfChars(JNIEnv* env, jstring value)
        : env_(env), value_(value), chars_(env->GetStringUTFChars(value, nullptr)) {}

    ~UtfChars() {
        if (chars_ != nullptr) env_->ReleaseStringUTFChars(value_, chars_);
    }

    const char* get() const { return chars_; }

private:
    JNIEnv* env_;
    jstring value_;
    const char* chars_;
};

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_dev_pathguard_hideprobe_NativeProbe_run(
    JNIEnv* env, jclass, jstring sandbox, jobjectArray observed_paths) {
    if (sandbox == nullptr || observed_paths == nullptr) return nullptr;

    UtfChars sandbox_chars(env, sandbox);
    if (sandbox_chars.get() == nullptr) return nullptr;

    std::vector<std::string> arguments = {
        "pathguard_hide_app_probe", "--sandbox", sandbox_chars.get()};
    const jsize count = env->GetArrayLength(observed_paths);
    arguments.reserve(3 + static_cast<size_t>(count) * 2);
    for (jsize index = 0; index < count; ++index) {
        auto* value = static_cast<jstring>(
            env->GetObjectArrayElement(observed_paths, index));
        if (value == nullptr) return nullptr;
        {
            UtfChars path_chars(env, value);
            if (path_chars.get() == nullptr) {
                env->DeleteLocalRef(value);
                return nullptr;
            }
            arguments.emplace_back("--observe");
            arguments.emplace_back(path_chars.get());
        }
        env->DeleteLocalRef(value);
    }

    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) argv.push_back(argument.data());

    std::string output;
    pathguard::hide_probe::RunHideVfsProbe(
        static_cast<int>(argv.size()), argv.data(), &output);
    rmdir(sandbox_chars.get());
    return env->NewStringUTF(output.c_str());
}
