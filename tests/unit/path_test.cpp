#include <string>

#include "pathguard/path.h"
#include "test_assert.h"

int main() {
    std::string output;
    assert(pathguard::NormalizeLogicalPath("DCIM/Camera", &output));
    assert(output == "DCIM/Camera");
    assert(!pathguard::NormalizeLogicalPath("/storage/emulated/0/DCIM", &output));
    assert(!pathguard::NormalizeLogicalPath("../Secret", &output));
    assert(!pathguard::NormalizeLogicalPath("DCIM//Camera", &output));
    assert(!pathguard::NormalizeLogicalPath(std::string(256, 'a'), &output));
    std::string oversized = "a";
    for (int index = 0; index < 2048; ++index) oversized += "/a";
    assert(oversized.size() > 4095);
    assert(!pathguard::NormalizeLogicalPath(oversized, &output));

    assert(pathguard::ExpandPathPlaceholders(
        "PathGuard/{package}/{user}", "com.example.app", &output));
    assert(output == "PathGuard/com.example.app/{user}");
    assert(!pathguard::ExpandPathPlaceholders(
        "PathGuard/{unknown}", "com.example.app", &output));

    assert(pathguard::IsPathOrDescendant("Download", "Download"));
    assert(pathguard::IsPathOrDescendant("Download/file", "Download"));
    assert(!pathguard::IsPathOrDescendant("Downloads/file", "Download"));
    return 0;
}
