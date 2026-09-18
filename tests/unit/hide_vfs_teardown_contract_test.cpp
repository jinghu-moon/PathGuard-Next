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
    const std::string atomic_open = FunctionBody(
        source, "static int hide1_atomic_open(struct inode *dir",
        "struct hide1_dir_proxy");
    const std::string lookup = FunctionBody(
        source, "static struct dentry *hide1_lookup(struct inode *dir",
        "static int hide1_atomic_open(struct inode *dir");
    const std::string dentry_presence = FunctionBody(
        source, "static bool hide1_dentry_shadow_present",
        "static int hide1_install_dentry_shadow");
    const std::string iterate_shared = FunctionBody(
        source, "static int hide1_iterate_shared(struct file *file",
        "static int hide1_d_revalidate");
    const std::string dir_actor = FunctionBody(
        source, "static bool hide1_dir_actor(struct dir_context *ctx",
        "static int hide1_iterate_shared");
    const std::string d_revalidate = FunctionBody(
        source, "static int hide1_d_revalidate(struct dentry *dentry",
        "static bool hide1_mutation_blocked");
    const std::string install_mode = FunctionBody(
        source, "static int hide1_shadow_install_locked",
        "static int hide1_shadow_uninstall_locked");
    const std::string commit = FunctionBody(
        source, "static void hide1_commit_binding",
        "static long hide1_ioctl");
    const std::string revoke = FunctionBody(
        source, "static void hide1_revoke_dead_target_locked",
        "static bool hide1_name_matches");
    const std::string target_exited = FunctionBody(
        source, "static bool hide1_target_exited_locked",
        "static void hide1_revoke_dead_target_locked");

    assert(source.find("static int hide1_shadow_mode = 1;") !=
           std::string::npos);
    assert(source.find("hide1_shadow_mode == 4") != std::string::npos);
    assert(source.find("static bool hide1_mode_is_readonly(void)") !=
           std::string::npos);
    assert(source.find("hide1_required_operation_mask") != std::string::npos);
    assert(atomic_open.find("hide1_install_dentry_shadow") ==
           std::string::npos);
    assert(atomic_open.find("O_CREAT | O_EXCL | O_TRUNC") !=
           std::string::npos);
    assert(atomic_open.find("hide1_mutation_blocked_calls") !=
           std::string::npos);
    assert(atomic_open.find("d_drop(") == std::string::npos);
    RequireOrder(lookup, {
        "orig->lookup(dir, dentry, flags)",
        "struct dentry *resolved = result ? result : dentry",
        "hide1_install_dentry_shadow(binding, resolved",
        "d_drop(resolved)",
    });
    assert(lookup.find("hide1_is_target_observer(binding)") ==
           std::string::npos);
    assert(dentry_presence.find("spin_lock(&dentry->d_lock)") !=
           std::string::npos);
    assert(dentry_presence.find("hide1_dop_lookup_rcu") ==
           std::string::npos);
    assert(iterate_shared.find("file_inode(file) != binding->parent_inode") !=
           std::string::npos);
    assert(iterate_shared.find("proxy.dir_inode = file_inode(file)") !=
           std::string::npos);
    assert(dir_actor.find("proxy->ctx.pos = offset") !=
           std::string::npos);
    assert(dir_actor.find("proxy->orig->pos = proxy->ctx.pos") !=
           std::string::npos);
    assert(dir_actor.find("proxy->ctx.pos = proxy->orig->pos") !=
           std::string::npos);
    assert(d_revalidate.find("hide1_d_revalidate_hidden") !=
           std::string::npos);
    assert(d_revalidate.find("hide1_mark_dentry_stale(meta)") !=
           std::string::npos);
    assert(d_revalidate.find("d_unhashed(dentry)") == std::string::npos);
    assert(d_revalidate.find("synthetic_negative") !=
           std::string::npos);
    assert(d_revalidate.find("cache_generation") !=
           std::string::npos);
    assert(d_revalidate.find("d_is_negative(dentry)") !=
           std::string::npos);
    assert(d_revalidate.find("ret = 1") != std::string::npos);
    assert(d_revalidate.find("ret = 0") != std::string::npos);
    assert(d_revalidate.find("A synthetic target-only negative") !=
           std::string::npos);
    const std::string stale_marker = FunctionBody(
        source, "static void hide1_mark_dentry_stale",
        "static void hide1_free_fop_meta");
    RequireOrder(stale_marker, {
        "PATHGUARD_HIDE1_LIFECYCLE_RUNNING",
        "test_and_set_bit(HIDE1_DOP_STALE",
        "schedule_delayed_work(&hide1_dop_stale_work",
    });
    assert(install_mode.find("if (!hide1_mode_is_readonly())") !=
           std::string::npos);

    assert(uninstall.find("!binding->dentry_shadows.next") == std::string::npos);
    RequireOrder(uninstall, {
        "hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_STOP_NEW",
        "WRITE_ONCE(binding->retiring, true)",
        "WRITE_ONCE(hide1_status.state, PATHGUARD_HIDE1_STATE_INACTIVE)",
        "smp_store_release(&inode->i_fop",
        "smp_store_release(&inode->i_op",
        "hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_RESTORE",
        "hide1_restore_dentry_shadows(binding, &retired)",
        "hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_DRAINING",
        "hide1_drain_callbacks(im, fm, &retired)",
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
        "meta->dentry->d_flags &= ~DCACHE_OP_REVALIDATE",
        "smp_wmb()",
        "WRITE_ONCE(meta->dentry->d_op, meta->orig_dop)",
        "spin_unlock(&meta->dentry->d_lock)",
        "d_drop(meta->dentry)",
    });
    assert(source.find("synchronize_rcu_tasks();") != std::string::npos);
    assert(source.find("PATHGUARD_HIDE1_LIFECYCLE_RESTORE") != std::string::npos);
    assert(source.find("struct hide1_iop_meta") != std::string::npos);
    assert(source.find("struct hide1_fop_meta") != std::string::npos);
    assert(source.find("struct inode *hidden_inode") != std::string::npos);
    const std::string link = FunctionBody(
        source, "static int hide1_link(struct dentry *old_dentry",
        "static int hide1_rename(struct mnt_idmap *idmap");
    RequireOrder(link, {
        "hide1_mutation_blocked(binding, dir, new_dentry)",
        "hide1_hidden_source(binding, old_dentry)",
        "meta->orig->link",
    });
    const std::string rename = FunctionBody(
        source, "static int hide1_rename", "static int hide1_shadow_install_locked");
    RequireOrder(rename, {
        "if (flags)",
        "-EOPNOTSUPP",
        "old_dir->i_sb != new_dir->i_sb",
        "-EXDEV",
        "hide1_mutation_blocked(binding, old_dir, old_dentry)",
        "hide1_mutation_blocked(binding, new_dir, new_dentry)",
        "meta->orig->rename",
    });
    assert(source.find("struct task_struct *target_task") != std::string::npos);
    assert(source.find("same_thread_group(current, binding->target_task)") !=
           std::string::npos);
    assert(source.find("static bool hide1_target_exited_locked") !=
           std::string::npos);
    assert(target_exited.find("PF_EXITING") != std::string::npos);
    RequireOrder(revoke, {
        "hide1_binding.retiring = true",
        "hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_STOP_NEW",
        "WRITE_ONCE(hide1_status.state, PATHGUARD_HIDE1_STATE_INACTIVE)",
        "hide1_status.last_error = -ESRCH",
    });
    const std::string disable = FunctionBody(
        source, "case PATHGUARD_HIDE1_IOC_DISABLE:",
        "case PATHGUARD_HIDE1_IOC_CLEAR:");
    RequireOrder(disable, {
        "hide1_revoke_dead_target_locked();",
        "hide1_binding.shadow.iop_installed",
        "hide1_binding.shadow.fop_installed",
        "!list_empty(&hide1_binding.dentry_shadows)",
        "hide1_shadow_uninstall_locked(&hide1_binding)",
    });
    const std::string status = FunctionBody(
        source, "case PATHGUARD_HIDE1_IOC_STATUS:",
        "default:");
    assert(status.find("hide1_revoke_dead_target_locked();") !=
           std::string::npos);
    assert(source.find("put_task_struct(binding->target_task)") !=
           std::string::npos);
    assert(commit.find("hide1_binding = *binding") == std::string::npos);
    RequireOrder(commit, {
        "hide1_release_binding(&hide1_binding)",
        "hide1_binding.target_task = binding->target_task",
        "hide1_binding.target_nsproxy = binding->target_nsproxy",
        "INIT_LIST_HEAD(&hide1_binding.dentry_shadows)",
        "spin_lock_init(&hide1_binding.dentry_lock)",
        "memset(binding, 0, sizeof(*binding))",
    });
    assert(source.find("DEFINE_HASHTABLE(hide1_iop_table") != std::string::npos);
    assert(source.find("DEFINE_HASHTABLE(hide1_fop_table") != std::string::npos);
    assert(source.find("DEFINE_HASHTABLE(hide1_dop_table") != std::string::npos);
    assert(source.find("atomic_t hide1_iop_active") != std::string::npos);
    assert(source.find("atomic_t hide1_fop_active") != std::string::npos);
    assert(source.find("atomic_t hide1_dop_active") != std::string::npos);
    assert(source.find("atomic_t open_count") != std::string::npos);
    assert(source.find("hide1_mutation_calls") != std::string::npos);
    assert(source.find("hide1_mutation_blocked_calls") != std::string::npos);
    assert(source.find("status.mutation_unsupported") != std::string::npos);
    assert(source.find("hide1_lookup_calls") != std::string::npos);
    assert(source.find("hide1_dentry_install_success") != std::string::npos);
    assert(source.find("status.lookup_calls") != std::string::npos);
    assert(source.find("hide1_dop_stale_workfn") != std::string::npos);
    const std::string stale_worker = FunctionBody(
        source, "static void hide1_dop_stale_workfn",
        "static bool hide1_is_target_observer");
    RequireOrder(stale_worker, {
        "d_unhashed(meta->dentry)",
        "d_count(meta->dentry) == 1",
        "synchronize_srcu(&hide1_srcu);",
        "synchronize_rcu();",
        "wait_event(hide1_dop_wait, atomic_read(&hide1_dop_active) == 0);",
        "hide1_free_dentry_shadows(&retired);",
    });
    assert(source.find("PATHGUARD_HIDE1_LIFECYCLE_STOP_NEW") != std::string::npos);
    assert(source.find("PATHGUARD_HIDE1_LIFECYCLE_DRAINING") != std::string::npos);
    assert(source.find("hide1_status.state == PATHGUARD_HIDE1_STATE_ACTIVE ||") !=
           std::string::npos);
    assert(source.find("return -EBUSY") != std::string::npos);
    assert(source.find("if (hide1_status.state == PATHGUARD_HIDE1_STATE_ACTIVE) {") !=
           std::string::npos);
    assert(source.find("if (hide1_status.state == PATHGUARD_HIDE1_STATE_INACTIVE) {") !=
           std::string::npos);
    assert(source.find("fm->ingress.open = hide1_fop_open") !=
           std::string::npos);
    assert(source.find("fm->live.release = hide1_fop_release") !=
           std::string::npos);
    assert(source.find("WRITE_ONCE(file->f_op, &meta->live)") !=
           std::string::npos);
    assert(uninstall.find("Preflight all ingress pointers") != std::string::npos);
    assert(uninstall.find("return -EAGAIN") != std::string::npos);
    const std::string install = FunctionBody(
        source, "static int hide1_shadow_install_locked",
        "static int hide1_shadow_uninstall_locked");
    RequireOrder(install, {
        "rollback:",
        "hash_del_rcu(&shadow->iop_meta->node)",
        "synchronize_rcu();",
        "hide1_drain_callbacks(im, fm, &retired);",
        "kfree(im)",
    });
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
    assert(wrapper.find("MODE=\"${2:-1}\"") != std::string::npos);
    return 0;
}
