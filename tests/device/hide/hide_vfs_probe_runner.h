#pragma once

#include <string>

namespace pathguard::hide_probe {

// Runs the shared probe implementation. When output is non-null, JSONL is
// appended to it instead of stdout.
int RunHideVfsProbe(int argc, char** argv, std::string* output);

}  // namespace pathguard::hide_probe
