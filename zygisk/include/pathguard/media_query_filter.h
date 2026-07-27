#pragma once

#include <stddef.h>

namespace pathguard::media_query {

bool BuildDenySelection(const char* const* deny_paths, size_t deny_path_count,
                        char* selection, size_t selection_capacity,
                        char* arguments, size_t argument_capacity,
                        size_t argument_stride, size_t* argument_count);

}  // namespace pathguard::media_query
