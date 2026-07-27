#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "pathguard/media_query_filter.h"

int main() {
    constexpr size_t kStride = 256;
    char selection[1024]{};
    char arguments[8][kStride]{};
    size_t argument_count = 0;

    const char* paths[] = {
        "/storage/emulated/0/Pictures/Nagram",
        "/storage/emulated/0/DCIM/Screen_shots%raw",
    };
    assert(pathguard::media_query::BuildDenySelection(
        paths, 2, selection, sizeof(selection), arguments[0], 8, kStride,
        &argument_count));
    assert(strcmp(
        selection,
        "(_data IS NULL OR (_data != ? AND _data NOT LIKE ? ESCAPE '\\')) AND "
        "(_data IS NULL OR (_data != ? AND _data NOT LIKE ? ESCAPE '\\'))") == 0);
    assert(argument_count == 4);
    assert(strcmp(arguments[0], paths[0]) == 0);
    assert(strcmp(arguments[1],
                  "/storage/emulated/0/Pictures/Nagram/%") == 0);
    assert(strcmp(arguments[2], paths[1]) == 0);
    assert(strcmp(arguments[3],
                  "/storage/emulated/0/DCIM/Screen\\_shots\\%raw/%") == 0);

    const char* trailing_slash[] = {"/storage/emulated/0/DCIM/"};
    assert(!pathguard::media_query::BuildDenySelection(
        trailing_slash, 1, selection, sizeof(selection), arguments[0], 8,
        kStride, &argument_count));
    assert(!pathguard::media_query::BuildDenySelection(
        paths, 2, selection, 8, arguments[0], 8, kStride, &argument_count));
    return 0;
}
