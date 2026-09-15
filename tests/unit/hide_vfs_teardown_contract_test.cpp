#include "test_assert.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

#ifndef PATHGUARD_SOURCE_DIR
#error "PATHGUARD_SOURCE_DIR must be defined"
#endif

namespace {

std::string ReadFile(const std::string& relative_path) {
    const std::string path = std::string(PATHGUARD_SOURCE_DIR) + "/" +
                             relative_path;
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string FunctionBody(const std::string& source,
                         const std::string& signature,
                         const std::string& next_function) {
    const std::size_t start = source.find(signature);
    assert(start != std::string::npos);
    const std::size_t end = source.find(next_function, start + signature.size());
    assert(end != std::string::npos && end > start);
    return source.substr(start, end - start);
}

void RequireOrder(const std::string& body,
                  std::initializer_list<std::string> tokens) {
    std::size_t previous = 0;
    for (const std::string& token : tokens) {
        const std::size_t position = body.find(token, previous);
        assert(position != std::string::npos);
        previous = position + token.size();
    }
}

}  // namespace

int main() {
    const std::string source = ReadFile(
        "experimental/hide-vfs/pathguard_hide1.c");
    const std::string uninstall = FunctionBody(
        source, "static int hide1_shadow_uninstall_locked",
        "static u64 hide1_operation_mask");
    const std::string restore = FunctionBody(
        source, "static int hide1_restore_dentry_shadows",
        "static void hide1_free_dentry_shadows");

    assert(uninstall.find("!binding->dentry_shadows.next") == std::string::npos);
    RequireOrder(uninstall, {
        "hide1_lifecycle = HIDE1_LIFECYCLE_STOP_NEW",
        "WRITE_ONCE(binding->retiring, true)",
        "WRITE_ONCE(hide1_status.state, PATHGUARD_HIDE1_STATE_INACTIVE)",
        "smp_store_release(&inode->i_fop",
        "smp_store_release(&inode->i_op",
        "hide1_lifecycle = HIDE1_LIFECYCLE_RESTORE",
        "hide1_restore_dentry_shadows(binding, &retired)",
        "hide1_lifecycle = HIDE1_LIFECYCLE_DRAINING",
        "hide1_drain_callbacks()",
        "hide1_drain_retired_dentries(&retired)",
        "hide1_free_dentry_shadows(&retired)",
        "return ret",
    });
    const std::string drain = FunctionBody(
        source, "static void hide1_drain_retired_dentries",
        "static struct hide1_iop_meta *hide1_iop_enter");
    RequireOrder(drain, {
        "if (!retired || list_empty(retired))",
        "return",
        "synchronize_srcu(&hide1_srcu)",
        "synchronize_rcu()",
    });

    RequireOrder(restore, {
        "spin_lock(&meta->dentry->d_lock)",
        "WRITE_ONCE(meta->dentry->d_flags, meta->orig_flags)",
        "smp_wmb()",
        "WRITE_ONCE(meta->dentry->d_op, meta->orig_dop)",
        "spin_unlock(&meta->dentry->d_lock)",
        "d_drop(meta->dentry)",
    });
    assert(source.find("synchronize_rcu_tasks();") != std::string::npos);
    assert(source.find("HIDE1_LIFECYCLE_RESTORE") != std::string::npos);
    assert(source.find("struct hide1_iop_meta") != std::string::npos);
    assert(source.find("struct hide1_fop_meta") != std::string::npos);
    assert(source.find("DEFINE_HASHTABLE(hide1_iop_table") != std::string::npos);
    assert(source.find("DEFINE_HASHTABLE(hide1_fop_table") != std::string::npos);
    assert(source.find("DEFINE_HASHTABLE(hide1_dop_table") != std::string::npos);
    assert(source.find("atomic_t hide1_iop_active") != std::string::npos);
    assert(source.find("atomic_t hide1_fop_active") != std::string::npos);
    assert(source.find("atomic_t hide1_dop_active") != std::string::npos);
    assert(source.find("atomic_t open_count") != std::string::npos);
    assert(source.find("hide1_dop_stale_workfn") != std::string::npos);
    assert(source.find("HIDE1_LIFECYCLE_STOP_NEW") != std::string::npos);
    assert(source.find("HIDE1_LIFECYCLE_DRAINING") != std::string::npos);
    assert(source.find("hide1_status.state == PATHGUARD_HIDE1_STATE_ACTIVE ||") !=
           std::string::npos);
    assert(source.find("return -EBUSY") != std::string::npos);
    assert(source.find("fm->ingress.open = hide1_fop_open") !=
           std::string::npos);
    assert(source.find("fm->live.release = hide1_fop_release") !=
           std::string::npos);
    assert(source.find("WRITE_ONCE(file->f_op, &meta->live)") !=
           std::string::npos);
    assert(uninstall.find("Preflight all ingress pointers") != std::string::npos);
    assert(uninstall.find("return -EAGAIN") != std::string::npos);
    const std::string reset = FunctionBody(
        source, "static int hide1_reset_locked",
        "static int hide1_prepare_binding");
    RequireOrder(reset, {
        "int ret = hide1_shadow_uninstall_locked(&hide1_binding)",
        "if (ret)",
        "return ret",
        "hide1_release_binding(&hide1_binding)",
    });

    const std::string wrapper = ReadFile(
        "experimental/hide-vfs/package/bin/hide1ctl");
    assert(wrapper.find("exec >> \"$LOG\" 2>&1") == std::string::npos);
    assert(wrapper.find("run_control()") != std::string::npos);
    return 0;
}
