#include "test_assert.h"

#include <fstream>
#include <iterator>
#include <string>

#ifndef PATHGUARD_SOURCE_DIR
#error "PATHGUARD_SOURCE_DIR must be defined"
#endif

namespace {

std::string Read(const char* relative) {
    std::ifstream input(std::string(PATHGUARD_SOURCE_DIR) + "/" + relative,
                        std::ios::binary);
    assert(input.good());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

void Require(const std::string& source, const char* text) {
    assert(source.find(text) != std::string::npos);
}

void RequireOrder(const std::string& source, const char* first,
                  const char* second) {
    const std::size_t lhs = source.find(first);
    const std::size_t rhs = source.find(second,
        lhs == std::string::npos ? 0 : lhs);
    assert(lhs != std::string::npos && rhs != std::string::npos && lhs < rhs);
}

}  // namespace

int main() {
    const std::string source = Read("experimental/hide-vfs/pathguard_hide1.c");
    Require(source, "static int hide1_shadow_install_locked");
    Require(source, "static int hide1_shadow_uninstall_locked");
    Require(source, "static int hide1_restore_dentry_shadows");
    Require(source, "PATHGUARD_HIDE1_LIFECYCLE_STOP_NEW");
    Require(source, "PATHGUARD_HIDE1_LIFECYCLE_RESTORE");
    Require(source, "PATHGUARD_HIDE1_LIFECYCLE_DRAINING");
    Require(source, "synchronize_srcu(&hide1_srcu)");
    Require(source, "synchronize_rcu()");
    Require(source, "hide1_free_dentry_shadows(&retired)");
    Require(source, "hide1_install_descendant_iop_shadow");
    Require(source, "hide1_install_cached_descendant_shadows");
    Require(source, "list_for_each_entry(child, &parent->d_subdirs, d_child)");
    Require(source, "hide1_dentry_should_hide");
    Require(source, "hide1_mutation_blocked");
    Require(source, "same_thread_group(current, binding->target_task)");
    Require(source, "binding->target_mnt_ns = nsproxy->mnt_ns");
    Require(source, "put_nsproxy(binding->target_nsproxy)");
    Require(source, "register_kprobe(&hide1_symlink_probe)");
    Require(source, "unregister_kprobe(&hide1_symlink_probe)");
    RequireOrder(source, "hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_STOP_NEW",
                 "hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_RESTORE");
    RequireOrder(source, "hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_RESTORE",
                 "hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_DRAINING");
    return 0;
}
