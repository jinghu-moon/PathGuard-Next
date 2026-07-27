#include "pathguard/media_query_filter.h"

#include <string.h>

namespace pathguard::media_query {
namespace {

bool Append(char* output, size_t capacity, const char* value) {
    const size_t current = strlen(output);
    const size_t addition = strlen(value);
    if (current + addition + 1 > capacity) return false;
    memcpy(output + current, value, addition + 1);
    return true;
}

bool Copy(char* output, size_t capacity, const char* value) {
    const size_t length = strlen(value);
    if (length + 1 > capacity) return false;
    memcpy(output, value, length + 1);
    return true;
}

bool CopyLikePrefix(char* output, size_t capacity, const char* path) {
    size_t written = 0;
    for (const char* current = path; *current != '\0'; ++current) {
        if (*current == '%' || *current == '_' || *current == '\\') {
            if (written + 1 >= capacity) return false;
            output[written++] = '\\';
        }
        if (written + 1 >= capacity) return false;
        output[written++] = *current;
    }
    if (written + 3 > capacity) return false;
    output[written++] = '/';
    output[written++] = '%';
    output[written] = '\0';
    return true;
}

}  // namespace

bool BuildDenySelection(const char* const* deny_paths, size_t deny_path_count,
                        char* selection, size_t selection_capacity,
                        char* arguments, size_t argument_capacity,
                        size_t argument_stride, size_t* argument_count) {
    if (deny_paths == nullptr || deny_path_count == 0 || selection == nullptr
        || selection_capacity == 0 || arguments == nullptr
        || argument_stride == 0 || argument_count == nullptr
        || deny_path_count > argument_capacity / 2) {
        return false;
    }

    selection[0] = '\0';
    *argument_count = 0;
    constexpr char kClause[] =
        "(_data IS NULL OR (_data != ? AND _data NOT LIKE ? ESCAPE '\\'))";
    for (size_t index = 0; index < deny_path_count; ++index) {
        const char* path = deny_paths[index];
        if (path == nullptr || path[0] != '/') return false;
        const size_t path_length = strlen(path);
        if (path_length == 1 || path[path_length - 1] == '/') return false;
        if (index > 0 && !Append(selection, selection_capacity, " AND ")) return false;
        if (!Append(selection, selection_capacity, kClause)) return false;

        char* exact = arguments + *argument_count * argument_stride;
        if (!Copy(exact, argument_stride, path)) return false;
        ++*argument_count;
        char* descendants = arguments + *argument_count * argument_stride;
        if (!CopyLikePrefix(descendants, argument_stride, path)) return false;
        ++*argument_count;
    }
    return true;
}

}  // namespace pathguard::media_query
