#pragma once

#include <string>
#include <string_view>

namespace pathguard {

bool NormalizePath(std::string_view input, std::string* output);
bool ExpandPackagePlaceholder(std::string_view input, std::string_view package, std::string* output);
bool IsPathOrDescendant(std::string_view path, std::string_view directory);

}  // namespace pathguard
