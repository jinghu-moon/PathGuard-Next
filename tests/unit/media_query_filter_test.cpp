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

    const char* maximum_paths[] = {
        "/storage/emulated/0/A", "/storage/emulated/0/B",
        "/storage/emulated/0/C", "/storage/emulated/0/D",
        "/storage/emulated/0/E", "/storage/emulated/0/F",
        "/storage/emulated/0/G", "/storage/emulated/0/H",
    };
    char maximum_selection[8192]{};
    char maximum_arguments[16][kStride]{};
    assert(pathguard::media_query::BuildDenySelection(
        maximum_paths, 8, maximum_selection, sizeof(maximum_selection),
        maximum_arguments[0], 16, kStride, &argument_count));
    assert(argument_count == 16);
    assert(strcmp(maximum_arguments[0], maximum_paths[0]) == 0);
    assert(strcmp(maximum_arguments[15],
                  "/storage/emulated/0/H/%") == 0);
    return 0;
}
