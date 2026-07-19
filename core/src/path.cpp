#include "pathguard/path.h"

#include <vector>

namespace pathguard {
namespace {

bool SplitAndValidate(std::string_view input, std::vector<std::string>* parts) {
    if (input.empty() || input.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t start = input.front() == '/' ? 1 : 0;
    while (start <= input.size()) {
        const std::size_t end = input.find('/', start);
        const std::string_view part = input.substr(start, end == std::string_view::npos ? input.size() - start : end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        parts->emplace_back(part);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return !parts->empty();
}

}  // namespace

bool NormalizePath(std::string_view input, std::string* output) {
    if (output == nullptr || input.empty()) {
        return false;
    }
    std::string value(input);
    if (value.rfind("/sdcard/", 0) == 0) {
        value.replace(0, 8, "/storage/emulated/0/");
    } else if (value == "/sdcard") {
        value = "/storage/emulated/0";
    } else if (value.rfind("/storage/self/primary/", 0) == 0) {
        value.replace(0, 22, "/storage/emulated/0/");
    } else if (value == "/storage/self/primary") {
        value = "/storage/emulated/0";
    } else if (value.front() != '/') {
        value = "/storage/emulated/0/" + value;
    }

    while (value.size() > 1 && value.back() == '/') {
        value.pop_back();
    }

    std::vector<std::string> parts;
    if (!SplitAndValidate(value, &parts)) {
        return false;
    }
    output->clear();
    output->reserve(value.size());
    for (const std::string& part : parts) {
        output->push_back('/');
        output->append(part);
    }
    return true;
}

bool ExpandPackagePlaceholder(std::string_view input, std::string_view package, std::string* output) {
    if (output == nullptr || package.empty() || package.find_first_of("/{} \t\r\n") != std::string_view::npos) {
        return false;
    }
    output->assign(input);
    std::size_t offset = 0;
    while ((offset = output->find("{package}", offset)) != std::string::npos) {
        output->replace(offset, 9, package);
        offset += package.size();
    }
    return output->find('{') == std::string::npos && output->find('}') == std::string::npos;
}

bool IsPathOrDescendant(std::string_view path, std::string_view directory) {
    return path == directory || (path.size() > directory.size()
        && path.rfind(std::string(directory) + "/", 0) == 0);
}

}  // namespace pathguard
