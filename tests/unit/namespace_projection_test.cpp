#include "pathguard/namespace_projection.h"
#include "test_assert.h"

int main() {
    using namespace pathguard::namespace_projection;

    const std::string id = ComputeNamespaceIdV1("abc");
    assert(id == "xj4bnp4pahh6uqkbidpf3lrcem");
    assert(ValidNamespaceIdV1(id));
    assert(!ValidNamespaceIdV1("Xj4bnp4pahh6uqkbidpf3lrcem"));
    assert(!ValidNamespaceIdV1("xj4bnp4pahh6uqkbidpf3lrce1"));

    const std::string target = BuildNamespaceTargetV1("Download/shared", id);
    assert(target == "Download/shared/_pg/v1/ns_" + id);
    assert(NamespaceTargetMatchesV1(target, id));
    assert(!NamespaceTargetMatchesV1(
        "Download/shared/_pg/v2/ns_" + id, id));
    assert(SameRelativeTail(
        "/storage/emulated/0/Pictures/a.jpg",
        "/storage/emulated/0/Pictures",
        "/storage/emulated/0/Download/shared/_pg/v1/ns_" + id + "/a.jpg",
        "/storage/emulated/0/Download/shared/_pg/v1/ns_" + id));
    assert(!SameRelativeTail(
        "/storage/emulated/0/Pictures/a.jpg",
        "/storage/emulated/0/Pictures",
        "/storage/emulated/0/Download/shared/_pg/v1/ns_" + id + "/b.jpg",
        "/storage/emulated/0/Download/shared/_pg/v1/ns_" + id));
    return 0;
}
