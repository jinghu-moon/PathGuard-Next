// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hide 1.0 fixed-KMI VFS shadow prototype.
 *
 * INSTALL validates and pins a complete parent/observer binding. ENABLE
 * publishes the experimental shadow; admission remains blocked until device
 * and HideLab evidence exists.
 */
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/miscdevice.h>
#include <linux/mnt_namespace.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/rcupdate.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/pid.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "pathguard_hide1_uapi.h"

#ifndef PATHGUARD_HIDE1_EXPECTED_RELEASE
#define PATHGUARD_HIDE1_EXPECTED_RELEASE \
    "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"
#endif

static DEFINE_MUTEX(hide1_lock);
DEFINE_STATIC_SRCU(hide1_srcu);

/* 0=all, 1=i_op only, 2=f_op only, 3=dentry d_op only. */
static int hide1_shadow_mode;
module_param_named(shadow_mode, hide1_shadow_mode, int, 0600);
MODULE_PARM_DESC(shadow_mode,
                 "Shadow isolation mode: 0=all, 1=i_op, 2=f_op, 3=d_op");

struct hide1_binding;

struct hide1_iop_meta {
    struct hlist_node node;
    struct inode *inode;
    struct hide1_binding *binding;
    const struct inode_operations *orig;
    struct inode_operations shadow;
    atomic_t active;
};

struct hide1_fop_meta {
    struct hlist_node node;
    struct inode *inode;
    struct hide1_binding *binding;
    const struct file_operations *orig;
    struct module *orig_owner;
    struct file_operations ingress;
    struct file_operations live;
    atomic_t active;
    atomic_t open_count;
};

struct hide1_dentry_shadow {
    struct hlist_node hash_node;
    struct list_head node;
    struct dentry *dentry;
    const struct dentry_operations *orig_dop;
    struct dentry_operations shadow_dop;
    struct hide1_binding *binding;
    unsigned int orig_flags;
    atomic_t active;
    unsigned long state;
};

struct hide1_shadow {
    const struct inode_operations *orig_iop;
    const struct file_operations *orig_fop;
    struct hide1_iop_meta *iop_meta;
    struct hide1_fop_meta *fop_meta;
    bool iop_installed;
    bool fop_installed;
    bool module_pin;
};

struct hide1_binding {
    struct pathguard_hide1_rule rule;
    struct path parent_path;
    struct inode *parent_inode;
    struct nsproxy *target_nsproxy;
    struct mnt_namespace *target_mnt_ns;
    u64 operation_mask;
    struct hide1_shadow shadow;
    const struct dentry_operations *parent_dop;
    struct list_head dentry_shadows;
    spinlock_t dentry_lock;
    bool retiring;
};

static struct hide1_binding hide1_binding;
static DEFINE_HASHTABLE(hide1_iop_table, 4);
static DEFINE_HASHTABLE(hide1_fop_table, 4);
static DEFINE_HASHTABLE(hide1_dop_table, 6);
static DEFINE_SPINLOCK(hide1_meta_lock);
static atomic_t hide1_iop_active = ATOMIC_INIT(0);
static atomic_t hide1_fop_active = ATOMIC_INIT(0);
static atomic_t hide1_dop_active = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(hide1_iop_wait);
static DECLARE_WAIT_QUEUE_HEAD(hide1_fop_wait);
static DECLARE_WAIT_QUEUE_HEAD(hide1_dop_wait);
static struct work_struct hide1_dop_stale_work;
#define HIDE1_DOP_STALE 0
static enum {
    HIDE1_LIFECYCLE_FREE = 0,
    HIDE1_LIFECYCLE_READY,
    HIDE1_LIFECYCLE_RUNNING,
    HIDE1_LIFECYCLE_STOP_NEW,
    HIDE1_LIFECYCLE_RESTORE,
    HIDE1_LIFECYCLE_DRAINING,
} hide1_lifecycle = HIDE1_LIFECYCLE_FREE;
static struct pathguard_hide1_status hide1_status = {
    .abi_version = PATHGUARD_HIDE1_ABI_VERSION,
    .size = sizeof(struct pathguard_hide1_status),
    .state = PATHGUARD_HIDE1_STATE_UNSUPPORTED,
};

static struct hide1_iop_meta *hide1_iop_lookup_rcu(const struct inode *inode)
{
    struct hide1_iop_meta *meta;

    hash_for_each_possible_rcu(hide1_iop_table, meta, node,
                               (unsigned long)inode)
        if (meta->inode == inode)
            return meta;
    return NULL;
}

static struct hide1_fop_meta *hide1_fop_lookup_rcu(const struct inode *inode)
{
    struct hide1_fop_meta *meta;

    hash_for_each_possible_rcu(hide1_fop_table, meta, node,
                               (unsigned long)inode)
        if (meta->inode == inode)
            return meta;
    return NULL;
}

static struct hide1_dentry_shadow *hide1_dop_lookup_rcu(
    const struct dentry *dentry)
{
    struct hide1_dentry_shadow *meta;

    hash_for_each_possible_rcu(hide1_dop_table, meta, hash_node,
                               (unsigned long)dentry)
        if (meta->dentry == dentry)
            return meta;
    return NULL;
}

static void hide1_callback_enter(atomic_t *global, atomic_t *local,
                                 wait_queue_head_t *wait)
{
    atomic_inc(global);
    if (local)
        atomic_inc(local);
    (void)wait;
}

static void hide1_callback_exit(atomic_t *global, atomic_t *local,
                                wait_queue_head_t *wait)
{
    if (local && atomic_dec_and_test(local))
        wake_up_all(wait);
    if (atomic_dec_and_test(global))
        wake_up_all(wait);
}

static struct dentry *hide1_lookup(struct inode *, struct dentry *, unsigned int);
static int hide1_atomic_open(struct inode *, struct dentry *, struct file *,
                             unsigned int, umode_t);
static int hide1_fop_open(struct inode *, struct file *);
static int hide1_fop_release(struct inode *, struct file *);
static int hide1_iterate_shared(struct file *, struct dir_context *);
static int hide1_create(struct mnt_idmap *, struct inode *, struct dentry *,
                        umode_t, bool);
static int hide1_mkdir(struct mnt_idmap *, struct inode *, struct dentry *, umode_t);
static int hide1_mknod(struct mnt_idmap *, struct inode *, struct dentry *,
                       umode_t, dev_t);
static int hide1_symlink(struct mnt_idmap *, struct inode *, struct dentry *,
                         const char *);
static int hide1_unlink(struct inode *, struct dentry *);
static int hide1_rmdir(struct inode *, struct dentry *);
static int hide1_link(struct dentry *, struct inode *, struct dentry *);
static int hide1_rename(struct mnt_idmap *, struct inode *, struct dentry *,
                        struct inode *, struct dentry *, unsigned int);
static int hide1_d_revalidate(struct dentry *, unsigned int);
static void hide1_free_dentry_shadows(struct list_head *retired);

static void hide1_free_fop_meta(struct hide1_fop_meta *meta)
{
    if (!meta)
        return;
    if (meta->orig_owner)
        module_put(meta->orig_owner);
    kfree(meta);
}

static void hide1_dop_stale_workfn(struct work_struct *work)
{
    struct hide1_dentry_shadow *meta, *tmp;
    LIST_HEAD(retired);
    unsigned long flags;

    (void)work;
    mutex_lock(&hide1_lock);
    spin_lock_irqsave(&hide1_binding.dentry_lock, flags);
    list_for_each_entry_safe(meta, tmp, &hide1_binding.dentry_shadows, node) {
        if (!test_bit(HIDE1_DOP_STALE, &meta->state))
            continue;
        list_move_tail(&meta->node, &retired);
    }
    spin_unlock_irqrestore(&hide1_binding.dentry_lock, flags);
    list_for_each_entry_safe(meta, tmp, &retired, node) {
        bool drop = false;

        spin_lock(&meta->dentry->d_lock);
        if (READ_ONCE(meta->dentry->d_op) == &meta->shadow_dop) {
            WRITE_ONCE(meta->dentry->d_flags, meta->orig_flags);
            smp_wmb();
            WRITE_ONCE(meta->dentry->d_op, meta->orig_dop);
            drop = true;
        }
        spin_unlock(&meta->dentry->d_lock);
        spin_lock(&hide1_meta_lock);
        hash_del_rcu(&meta->hash_node);
        spin_unlock(&hide1_meta_lock);
        if (drop)
            d_drop(meta->dentry);
    }
    mutex_unlock(&hide1_lock);
    synchronize_srcu(&hide1_srcu);
    synchronize_rcu();
    hide1_free_dentry_shadows(&retired);
}

static bool hide1_is_target_observer(const struct hide1_binding *binding)
{
    if (!binding || READ_ONCE(binding->retiring) ||
        READ_ONCE(hide1_status.state) != PATHGUARD_HIDE1_STATE_ACTIVE)
        return false;
    if (!current->nsproxy || current->nsproxy->mnt_ns != binding->target_mnt_ns)
        return false;
    return __kuid_val(current_fsuid()) == binding->rule.target_uid;
}

static bool hide1_name_matches(const struct hide1_binding *binding,
                               const struct dentry *dentry)
{
    const char *name;

    if (!binding || !dentry || dentry->d_name.len !=
        strnlen(binding->rule.basename, sizeof(binding->rule.basename)))
        return false;
    name = dentry->d_name.name;
    return !memcmp(name, binding->rule.basename, dentry->d_name.len);
}

static bool hide1_mode_has_iop(void)
{
    return hide1_shadow_mode == 0 || hide1_shadow_mode == 1;
}

static bool hide1_mode_has_fop(void)
{
    return hide1_shadow_mode == 0 || hide1_shadow_mode == 2;
}

static bool hide1_mode_has_dop(void)
{
    return hide1_shadow_mode == 0 || hide1_shadow_mode == 3;
}

static bool hide1_should_hide(const struct hide1_binding *binding,
                              const struct inode *parent,
                              const struct dentry *dentry)
{
    return hide1_is_target_observer(binding) &&
           parent == binding->parent_inode && hide1_name_matches(binding, dentry);
}

static struct hide1_dentry_shadow *hide1_dentry_shadow_of(
    const struct dentry *dentry)
{
    struct hide1_dentry_shadow *meta;

    if (!dentry)
        return NULL;
    rcu_read_lock();
    meta = hide1_dop_lookup_rcu(dentry);
    rcu_read_unlock();
    return meta;
}

static int hide1_install_dentry_shadow(struct hide1_binding *binding,
                                       struct dentry *dentry)
{
    struct hide1_dentry_shadow *meta;
    const struct dentry_operations *orig;
    unsigned long flags;

    if (!dentry)
        return -EINVAL;
    if (hide1_dentry_shadow_of(dentry))
        return 0;
    if (READ_ONCE(binding->retiring))
        return -ESHUTDOWN;

    spin_lock(&dentry->d_lock);
    orig = READ_ONCE(dentry->d_op);
    if (orig && orig->d_revalidate == hide1_d_revalidate) {
        spin_unlock(&dentry->d_lock);
        return 0;
    }
    spin_unlock(&dentry->d_lock);
    meta = kzalloc(sizeof(*meta), GFP_ATOMIC);
    if (!meta)
        return -ENOMEM;
    meta->dentry = dget(dentry);
    meta->orig_dop = orig;
    meta->binding = binding;
    if (orig)
        meta->shadow_dop = *orig;
    meta->shadow_dop.d_revalidate = hide1_d_revalidate;

    atomic_set(&meta->active, 0);
    spin_lock_irqsave(&binding->dentry_lock, flags);
    spin_lock(&dentry->d_lock);
    if (binding->retiring) {
        spin_unlock(&dentry->d_lock);
        spin_unlock_irqrestore(&binding->dentry_lock, flags);
        dput(meta->dentry);
        kfree(meta);
        return -EAGAIN;
    }
    if (READ_ONCE(dentry->d_op) != orig) {
        const struct dentry_operations *observed_dop = READ_ONCE(dentry->d_op);

        /* Another racing lookup may have installed the same shadow. */
        if (observed_dop && observed_dop->d_revalidate == hide1_d_revalidate) {
            spin_unlock(&dentry->d_lock);
            spin_unlock_irqrestore(&binding->dentry_lock, flags);
            dput(meta->dentry);
            kfree(meta);
            return 0;
        }
        spin_unlock(&dentry->d_lock);
        spin_unlock_irqrestore(&binding->dentry_lock, flags);
        dput(meta->dentry);
        kfree(meta);
        return -EAGAIN;
    }
    meta->orig_flags = READ_ONCE(dentry->d_flags);
    list_add_tail(&meta->node, &binding->dentry_shadows);
    hash_add_rcu(hide1_dop_table, &meta->hash_node,
                 (unsigned long)dentry);
    smp_wmb();
    WRITE_ONCE(dentry->d_op, &meta->shadow_dop);
    WRITE_ONCE(dentry->d_flags, meta->orig_flags | DCACHE_OP_REVALIDATE);
    spin_unlock(&dentry->d_lock);
    spin_unlock_irqrestore(&binding->dentry_lock, flags);
    return 0;
}

static int hide1_restore_dentry_shadows(struct hide1_binding *binding,
                                        struct list_head *retired)
{
    struct hide1_dentry_shadow *meta;
    unsigned long flags;
    int ret = 0;

    if (!retired)
        return -EINVAL;

    spin_lock_irqsave(&binding->dentry_lock, flags);
    list_splice_init(&binding->dentry_shadows, retired);
    spin_unlock_irqrestore(&binding->dentry_lock, flags);

    list_for_each_entry(meta, retired, node) {
        bool drop = false;

        spin_lock(&meta->dentry->d_lock);
        if (READ_ONCE(meta->dentry->d_op) == &meta->shadow_dop) {
            WRITE_ONCE(meta->dentry->d_flags, meta->orig_flags);
            smp_wmb();
            WRITE_ONCE(meta->dentry->d_op, meta->orig_dop);
            /* Remove a negative/positive cache entry that was governed by
             * the shadow.  The dget held by meta keeps it alive until drain. */
            drop = true;
        } else if (READ_ONCE(meta->dentry->d_op) != meta->orig_dop) {
            /* Another owner replaced the vector while the shadow was live. */
            ret = -EAGAIN;
        }
        spin_unlock(&meta->dentry->d_lock);
        spin_lock(&hide1_meta_lock);
        hash_del_rcu(&meta->hash_node);
        spin_unlock(&hide1_meta_lock);
        if (drop)
            d_drop(meta->dentry);
    }
    return ret;
}

static void hide1_free_dentry_shadows(struct list_head *retired)
{
    struct hide1_dentry_shadow *meta, *tmp;

    if (!retired)
        return;
    list_for_each_entry_safe(meta, tmp, retired, node) {
        list_del_init(&meta->node);
        dput(meta->dentry);
        kfree(meta);
    }
}

static void hide1_drain_retired_dentries(struct list_head *retired)
{
    if (!retired || list_empty(retired))
        return;

    /* i_op/f_op shadows are embedded in the binding and remain valid after
     * DISABLE.  Only dynamically allocated dentry shadows need grace periods
     * before their metadata is released. */
    synchronize_srcu(&hide1_srcu);
    synchronize_rcu();
}

static void hide1_drain_callbacks(void)
{
    /* The first Tasks-RCU pass is issued by the RESTORE stage before indices
     * are removed.  Here we close the active-callback and reclamation sides. */
    wait_event(hide1_iop_wait, atomic_read(&hide1_iop_active) == 0);
    wait_event(hide1_fop_wait, atomic_read(&hide1_fop_active) == 0);
    wait_event(hide1_dop_wait, atomic_read(&hide1_dop_active) == 0);
    synchronize_rcu_tasks();
    synchronize_rcu();
}

static struct hide1_iop_meta *hide1_iop_enter(struct inode *inode)
{
    struct hide1_iop_meta *meta;

    rcu_read_lock();
    meta = hide1_iop_lookup_rcu(inode);
    if (!meta || READ_ONCE(inode->i_op) != &meta->shadow)
        meta = NULL;
    if (meta)
        hide1_callback_enter(&hide1_iop_active, &meta->active,
                             &hide1_iop_wait);
    rcu_read_unlock();
    return meta;
}

static struct hide1_fop_meta *hide1_fop_enter(struct file *file)
{
    struct hide1_fop_meta *meta;

    if (!file || !file_inode(file))
        return NULL;
    rcu_read_lock();
    meta = hide1_fop_lookup_rcu(file_inode(file));
    if (!meta || (READ_ONCE(file->f_op) != &meta->ingress &&
                  READ_ONCE(file->f_op) != &meta->live))
        meta = NULL;
    if (meta)
        hide1_callback_enter(&hide1_fop_active, &meta->active,
                             &hide1_fop_wait);
    rcu_read_unlock();
    return meta;
}

static struct hide1_dentry_shadow *hide1_dop_enter(
    const struct dentry *dentry)
{
    struct hide1_dentry_shadow *meta;

    rcu_read_lock();
    meta = hide1_dop_lookup_rcu(dentry);
    if (meta)
        hide1_callback_enter(&hide1_dop_active, &meta->active,
                             &hide1_dop_wait);
    rcu_read_unlock();
    return meta;
}

static int hide1_fop_open(struct inode *inode, struct file *file)
{
    struct hide1_fop_meta *meta = hide1_fop_enter(file);
    struct hide1_binding *binding = meta ? meta->binding : NULL;
    const struct file_operations *orig;
    int idx;
    int ret = 0;

    if (!binding)
        return -EIO;
    idx = srcu_read_lock(&hide1_srcu);
    atomic_inc(&meta->open_count);
    orig = meta->orig;
    if (orig && orig->open)
        ret = orig->open(inode, file);
    if (ret)
        atomic_dec(&meta->open_count);
    else
        WRITE_ONCE(file->f_op, &meta->live);
    hide1_callback_exit(&hide1_fop_active, &meta->active, &hide1_fop_wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

static int hide1_fop_release(struct inode *inode, struct file *file)
{
    struct hide1_fop_meta *meta = hide1_fop_enter(file);
    struct hide1_binding *binding = meta ? meta->binding : NULL;
    const struct file_operations *orig;
    int idx;
    int ret = 0;

    if (!binding)
        return -EIO;
    idx = srcu_read_lock(&hide1_srcu);
    orig = meta->orig;
    if (orig && orig->release)
        ret = orig->release(inode, file);
    atomic_dec(&meta->open_count);
    hide1_callback_exit(&hide1_fop_active, &meta->active, &hide1_fop_wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

static struct dentry *hide1_lookup(struct inode *dir, struct dentry *dentry,
                                   unsigned int flags)
{
    struct hide1_iop_meta *meta = hide1_iop_enter(dir);
    struct hide1_binding *binding = meta ? meta->binding : NULL;
    const struct inode_operations *orig;
    struct dentry *result;
    int idx;
    struct dentry *ret;

    if (!binding)
        return ERR_PTR(-EIO);

    idx = srcu_read_lock(&hide1_srcu);
    if (hide1_should_hide(binding, dir, dentry)) {
        if (hide1_mode_has_dop() &&
            hide1_install_dentry_shadow(binding, dentry)) {
            ret = ERR_PTR(-EAGAIN);
            goto out;
        }
        d_add(dentry, NULL);
        ret = NULL;
        goto out;
    }
    if (hide1_mode_has_dop() && hide1_is_target_observer(binding) &&
        dir == binding->parent_inode &&
        hide1_name_matches(binding, dentry))
        (void)hide1_install_dentry_shadow(binding, dentry);
    orig = meta->orig;
    result = orig && orig->lookup ? orig->lookup(dir, dentry, flags) : NULL;
    ret = result;
out:
    hide1_callback_exit(&hide1_iop_active, &meta->active, &hide1_iop_wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

static int hide1_atomic_open(struct inode *dir, struct dentry *dentry,
                             struct file *file, unsigned int open_flag,
                             umode_t create_mode)
{
    struct hide1_iop_meta *meta = hide1_iop_enter(dir);
    struct hide1_binding *binding = meta ? meta->binding : NULL;
    const struct inode_operations *orig;
    int idx = srcu_read_lock(&hide1_srcu);
    int ret;

    if (!binding) {
        srcu_read_unlock(&hide1_srcu, idx);
        return -EIO;
    }
    if (hide1_should_hide(binding, dir, dentry))
        ret = -ENOENT;
    else {
        orig = meta->orig;
        ret = orig && orig->atomic_open ?
              orig->atomic_open(dir, dentry, file, open_flag, create_mode) :
              -EOPNOTSUPP;
    }
    hide1_callback_exit(&hide1_iop_active, &meta->active, &hide1_iop_wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

struct hide1_dir_proxy {
    struct dir_context ctx;
    struct dir_context *orig;
    struct hide1_binding *binding;
};

static bool hide1_dir_actor(struct dir_context *ctx, const char *name,
                            int namelen, loff_t offset, u64 ino,
                            unsigned int d_type)
{
    struct hide1_dir_proxy *proxy = container_of(ctx, struct hide1_dir_proxy, ctx);

    if (hide1_is_target_observer(proxy->binding) &&
        namelen == strlen(proxy->binding->rule.basename) &&
        !memcmp(name, proxy->binding->rule.basename, namelen))
        return true;
    return proxy->orig->actor(proxy->orig, name, namelen, offset, ino, d_type);
}

static int hide1_iterate_shared(struct file *file, struct dir_context *ctx)
{
    struct hide1_fop_meta *meta = hide1_fop_enter(file);
    struct hide1_binding *binding = meta ? meta->binding : NULL;
    struct hide1_dir_proxy proxy;
    const struct file_operations *orig;
    int idx;
    int ret;

    if (!binding)
        return -EIO;
    idx = srcu_read_lock(&hide1_srcu);
    orig = meta->orig;
    if (!orig || !orig->iterate_shared) {
        hide1_callback_exit(&hide1_fop_active, &meta->active, &hide1_fop_wait);
        srcu_read_unlock(&hide1_srcu, idx);
        return -EOPNOTSUPP;
    }
    if (!hide1_is_target_observer(binding)) {
        ret = orig->iterate_shared(file, ctx);
        hide1_callback_exit(&hide1_fop_active, &meta->active, &hide1_fop_wait);
        srcu_read_unlock(&hide1_srcu, idx);
        return ret;
    }
    proxy.ctx = *ctx;
    proxy.ctx.actor = hide1_dir_actor;
    proxy.orig = ctx;
    proxy.binding = binding;
    ret = orig->iterate_shared(file, &proxy.ctx);
    ctx->pos = proxy.ctx.pos;
    hide1_callback_exit(&hide1_fop_active, &meta->active, &hide1_fop_wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

static int hide1_d_revalidate(struct dentry *dentry, unsigned int flags)
{
    struct hide1_dentry_shadow *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;

    idx = srcu_read_lock(&hide1_srcu);
    meta = hide1_dop_enter(dentry);
    binding = meta ? meta->binding : NULL;
    if (!binding) {
        srcu_read_unlock(&hide1_srcu, idx);
        return 1;
    }
    if (hide1_should_hide(binding, d_backing_inode(dentry->d_parent), dentry)) {
        set_bit(HIDE1_DOP_STALE, &meta->state);
        schedule_work(&hide1_dop_stale_work);
        hide1_callback_exit(&hide1_dop_active, &meta->active, &hide1_dop_wait);
        srcu_read_unlock(&hide1_srcu, idx);
        return 0;
    }
    ret = meta->orig_dop && meta->orig_dop->d_revalidate ?
          meta->orig_dop->d_revalidate(dentry, flags) : 1;
    hide1_callback_exit(&hide1_dop_active, &meta->active, &hide1_dop_wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

static bool hide1_mutation_blocked(struct hide1_binding *binding,
                                   struct inode *parent,
                                   struct dentry *dentry)
{
    return hide1_should_hide(binding, parent, dentry);
}

#define HIDE1_IOP_GUARD(_inode, _meta, _binding, _idx)                 \
    (_meta) = hide1_iop_enter((_inode));                               \
    (_binding) = (_meta) ? (_meta)->binding : NULL;                   \
    if (!(_binding))                                                    \
        return -EIO;                                                    \
    (_idx) = srcu_read_lock(&hide1_srcu)

#define HIDE1_IOP_UNGUARD(_meta, _idx)                                 \
    hide1_callback_exit(&hide1_iop_active, &(_meta)->active,           \
                        &hide1_iop_wait);                              \
    srcu_read_unlock(&hide1_srcu, (_idx))

static int hide1_create(struct mnt_idmap *idmap, struct inode *dir,
                        struct dentry *dentry, umode_t mode, bool excl)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry))
        ret = -ENOENT;
    else if (meta->orig && meta->orig->create)
        ret = meta->orig->create(idmap, dir, dentry, mode, excl);
    else
        ret = -EOPNOTSUPP;
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                       struct dentry *dentry, umode_t mode)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry))
        ret = -ENOENT;
    else if (meta->orig && meta->orig->mkdir)
        ret = meta->orig->mkdir(idmap, dir, dentry, mode);
    else
        ret = -EOPNOTSUPP;
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_mknod(struct mnt_idmap *idmap, struct inode *dir,
                       struct dentry *dentry, umode_t mode, dev_t dev)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry))
        ret = -ENOENT;
    else if (meta->orig && meta->orig->mknod)
        ret = meta->orig->mknod(idmap, dir, dentry, mode, dev);
    else
        ret = -EOPNOTSUPP;
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_symlink(struct mnt_idmap *idmap, struct inode *dir,
                         struct dentry *dentry, const char *symname)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry))
        ret = -ENOENT;
    else if (meta->orig && meta->orig->symlink)
        ret = meta->orig->symlink(idmap, dir, dentry, symname);
    else
        ret = -EOPNOTSUPP;
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_unlink(struct inode *dir, struct dentry *dentry)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry))
        ret = -ENOENT;
    else if (meta->orig && meta->orig->unlink)
        ret = meta->orig->unlink(dir, dentry);
    else
        ret = -EOPNOTSUPP;
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_rmdir(struct inode *dir, struct dentry *dentry)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry))
        ret = -ENOENT;
    else if (meta->orig && meta->orig->rmdir)
        ret = meta->orig->rmdir(dir, dentry);
    else
        ret = -EOPNOTSUPP;
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_link(struct dentry *old_dentry, struct inode *dir,
                      struct dentry *new_dentry)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, new_dentry))
        ret = -ENOENT;
    else if (meta->orig && meta->orig->link)
        ret = meta->orig->link(old_dentry, dir, new_dentry);
    else
        ret = -EOPNOTSUPP;
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_rename(struct mnt_idmap *idmap, struct inode *old_dir,
                        struct dentry *old_dentry, struct inode *new_dir,
                        struct dentry *new_dentry, unsigned int flags)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    HIDE1_IOP_GUARD(old_dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, old_dir, old_dentry) ||
        hide1_mutation_blocked(binding, new_dir, new_dentry))
        ret = -ENOENT;
    else if (meta->orig && meta->orig->rename)
        ret = meta->orig->rename(idmap, old_dir, old_dentry,
                                               new_dir, new_dentry, flags);
    else
        ret = -EOPNOTSUPP;
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_shadow_install_locked(struct hide1_binding *binding)
{
    struct hide1_shadow *shadow = &binding->shadow;
    struct inode *inode = binding->parent_inode;
    struct hide1_iop_meta *im = NULL;
    struct hide1_fop_meta *fm = NULL;
    struct dentry *cached;
    struct qstr name;
    size_t name_len;
    int ret;

    if (shadow->iop_installed || shadow->fop_installed)
        return -EALREADY;
    if (!inode || (hide1_mode_has_iop() && !shadow->orig_iop) ||
        (hide1_mode_has_fop() && !shadow->orig_fop))
        return -EINVAL;
    if (hide1_mode_has_iop() && READ_ONCE(inode->i_op) != shadow->orig_iop)
        return -EAGAIN;
    if (hide1_mode_has_fop() && READ_ONCE(inode->i_fop) != shadow->orig_fop)
        return -EAGAIN;
    if (hide1_mode_has_dop() &&
        READ_ONCE(binding->parent_path.dentry->d_op) != binding->parent_dop)
        return -EAGAIN;

    if (!try_module_get(THIS_MODULE))
        return -ENODEV;
    shadow->module_pin = true;

    if (hide1_mode_has_iop()) {
        im = kzalloc(sizeof(*im), GFP_KERNEL);
        if (!im) { ret = -ENOMEM; goto rollback; }
        im->inode = inode;
        im->binding = binding;
        im->orig = shadow->orig_iop;
        im->shadow = *shadow->orig_iop;
        im->shadow.lookup = hide1_lookup;
        im->shadow.atomic_open = hide1_atomic_open;
        im->shadow.create = hide1_create;
        im->shadow.mkdir = hide1_mkdir;
        im->shadow.mknod = hide1_mknod;
        im->shadow.symlink = hide1_symlink;
        im->shadow.unlink = hide1_unlink;
        im->shadow.rmdir = hide1_rmdir;
        im->shadow.link = hide1_link;
        im->shadow.rename = hide1_rename;
        atomic_set(&im->active, 0);
        spin_lock(&hide1_meta_lock);
        hash_add_rcu(hide1_iop_table, &im->node, (unsigned long)inode);
        spin_unlock(&hide1_meta_lock);
        shadow->iop_meta = im;
        smp_store_release(&inode->i_op, &im->shadow);
        shadow->iop_installed = true;
    }
    if (hide1_mode_has_fop()) {
        fm = kzalloc(sizeof(*fm), GFP_KERNEL);
        if (!fm) { ret = -ENOMEM; goto rollback; }
        fm->inode = inode;
        fm->binding = binding;
        fm->orig = shadow->orig_fop;
        fm->orig_owner = shadow->orig_fop->owner;
        if (fm->orig_owner && !try_module_get(fm->orig_owner)) {
            kfree(fm);
            fm = NULL;
            ret = -ENODEV;
            goto rollback;
        }
        fm->ingress = *shadow->orig_fop;
        fm->live = *shadow->orig_fop;
        fm->ingress.owner = THIS_MODULE;
        fm->live.owner = THIS_MODULE;
        fm->ingress.open = hide1_fop_open;
        fm->ingress.release = hide1_fop_release;
        fm->ingress.iterate_shared = hide1_iterate_shared;
        fm->live.open = hide1_fop_open;
        fm->live.release = hide1_fop_release;
        fm->live.iterate_shared = hide1_iterate_shared;
        atomic_set(&fm->active, 0);
        atomic_set(&fm->open_count, 0);
        spin_lock(&hide1_meta_lock);
        hash_add_rcu(hide1_fop_table, &fm->node, (unsigned long)inode);
        spin_unlock(&hide1_meta_lock);
        shadow->fop_meta = fm;
        smp_store_release(&inode->i_fop, &fm->ingress);
        shadow->fop_installed = true;
    }

    ret = hide1_mode_has_dop() ?
          hide1_install_dentry_shadow(binding, binding->parent_path.dentry) : 0;
    if (ret)
        goto rollback;
    name_len = strnlen(binding->rule.basename, sizeof(binding->rule.basename));
    name.name = binding->rule.basename;
    name.len = name_len;
    name.hash = full_name_hash(inode, name.name, name.len);
    cached = hide1_mode_has_dop() ?
             d_lookup(binding->parent_path.dentry, &name) : NULL;
    if (cached) {
        ret = hide1_install_dentry_shadow(binding, cached);
        dput(cached);
        if (ret)
            goto rollback;
    }
    return 0;

rollback:
    WRITE_ONCE(binding->retiring, true);
    if (shadow->fop_installed && shadow->fop_meta &&
        READ_ONCE(inode->i_fop) == &shadow->fop_meta->ingress) {
        smp_store_release(&inode->i_fop, shadow->orig_fop);
        shadow->fop_installed = false;
    }
    if (shadow->iop_installed && shadow->iop_meta &&
        READ_ONCE(inode->i_op) == &shadow->iop_meta->shadow) {
        smp_store_release(&inode->i_op, shadow->orig_iop);
        shadow->iop_installed = false;
    }
    {
        LIST_HEAD(retired);
        (void)hide1_restore_dentry_shadows(binding, &retired);
        hide1_drain_retired_dentries(&retired);
        hide1_free_dentry_shadows(&retired);
    }
    spin_lock(&hide1_meta_lock);
    if (shadow->iop_meta) {
        hash_del_rcu(&shadow->iop_meta->node);
        im = shadow->iop_meta;
        shadow->iop_meta = NULL;
    }
    if (shadow->fop_meta) {
        hash_del_rcu(&shadow->fop_meta->node);
        fm = shadow->fop_meta;
        shadow->fop_meta = NULL;
    }
    spin_unlock(&hide1_meta_lock);
    synchronize_rcu();
    kfree(im);
    hide1_free_fop_meta(fm);
    if (shadow->module_pin) {
        shadow->module_pin = false;
        module_put(THIS_MODULE);
    }
    return ret;
}

static int hide1_shadow_uninstall_locked(struct hide1_binding *binding)
{
    struct hide1_shadow *shadow = &binding->shadow;
    struct inode *inode = binding->parent_inode;
    struct hide1_iop_meta *im;
    struct hide1_fop_meta *fm;
    LIST_HEAD(retired);
    int ret = 0;
    int dentry_ret;

    /* A live file keeps f_op pointed at the bridge table.  Until its release
     * callback runs the metadata must remain indexed and module-pinned; a
     * disable request is therefore retried after callers close descriptors. */
    if (shadow->fop_meta &&
        atomic_read(&shadow->fop_meta->open_count) != 0)
        return -EBUSY;

    /* Preflight all ingress pointers.  If another subsystem replaced one,
     * leave every shadow and its metadata untouched for an explicit retry. */
    if (shadow->fop_installed &&
        (!inode || !shadow->fop_meta ||
         READ_ONCE(inode->i_fop) != &shadow->fop_meta->ingress))
        return -EAGAIN;
    if (shadow->iop_installed &&
        (!inode || !shadow->iop_meta ||
         READ_ONCE(inode->i_op) != &shadow->iop_meta->shadow))
        return -EAGAIN;

    if (!shadow->iop_installed && !shadow->fop_installed &&
        list_empty(&binding->dentry_shadows))
        return 0;

    /* STOP_NEW: block policy decisions before publishing original vectors. */
    hide1_lifecycle = HIDE1_LIFECYCLE_STOP_NEW;
    WRITE_ONCE(binding->retiring, true);
    WRITE_ONCE(hide1_status.state, PATHGUARD_HIDE1_STATE_INACTIVE);

    if (shadow->fop_installed)
        smp_store_release(&inode->i_fop, shadow->orig_fop);
    if (shadow->iop_installed)
        smp_store_release(&inode->i_op, shadow->orig_iop);
    shadow->fop_installed = false;
    shadow->iop_installed = false;

    /* RESTORE: all ingress pointers now reference the original filesystem. */
    hide1_lifecycle = HIDE1_LIFECYCLE_RESTORE;

    /* Restore operation pointers first.  This prevents new calls from
     * entering the shadow; SRCU drains wrappers that already entered it and
     * RCU orders publication before dentry metadata is reclaimed. */
    dentry_ret = hide1_restore_dentry_shadows(binding, &retired);
    if (dentry_ret)
        ret = dentry_ret;
    /* DRAIN: close the stale-pointer window before withdrawing indices.  The
     * grace period must not run while hide1_lock is held: a callback or stale
     * worker may legitimately need that mutex to finish its epilogue. */
    hide1_lifecycle = HIDE1_LIFECYCLE_DRAINING;
    mutex_unlock(&hide1_lock);
    synchronize_rcu_tasks();
    mutex_lock(&hide1_lock);
    spin_lock(&hide1_meta_lock);
    im = shadow->iop_meta;
    fm = shadow->fop_meta;
    if (im) {
        hash_del_rcu(&im->node);
        shadow->iop_meta = NULL;
    }
    if (fm) {
        hash_del_rcu(&fm->node);
        shadow->fop_meta = NULL;
    }
    spin_unlock(&hide1_meta_lock);

    mutex_unlock(&hide1_lock);
    hide1_drain_callbacks();
    mutex_lock(&hide1_lock);
    hide1_drain_retired_dentries(&retired);
    hide1_free_dentry_shadows(&retired);
    kfree(im);
    hide1_free_fop_meta(fm);
    if (shadow->module_pin) {
        shadow->module_pin = false;
        module_put(THIS_MODULE);
    }
    return ret;
}

static u64 hide1_operation_mask(const struct inode *inode,
                                const struct dentry *dentry)
{
    const struct inode_operations *iop = inode->i_op;
    const struct file_operations *fop = inode->i_fop;
    const struct dentry_operations *dop = dentry->d_op;
    u64 mask = 0;

    if (iop && iop->lookup) mask |= PATHGUARD_HIDE1_OP_LOOKUP;
    if (iop && iop->atomic_open) mask |= PATHGUARD_HIDE1_OP_ATOMIC_OPEN;
    if (fop && fop->iterate_shared) mask |= PATHGUARD_HIDE1_OP_READDIR;
    if (iop && iop->create) mask |= PATHGUARD_HIDE1_OP_CREATE;
    if (iop && iop->mkdir) mask |= PATHGUARD_HIDE1_OP_MKDIR;
    if (iop && iop->mknod) mask |= PATHGUARD_HIDE1_OP_MKNOD;
    if (iop && iop->symlink) mask |= PATHGUARD_HIDE1_OP_SYMLINK;
    if (iop && iop->unlink) mask |= PATHGUARD_HIDE1_OP_UNLINK;
    if (iop && iop->rmdir) mask |= PATHGUARD_HIDE1_OP_RMDIR;
    if (iop && iop->link) mask |= PATHGUARD_HIDE1_OP_LINK;
    if (iop && iop->rename) mask |= PATHGUARD_HIDE1_OP_RENAME;
    if (dop && dop->d_revalidate) mask |= PATHGUARD_HIDE1_OP_REVALIDATE;
    return mask;
}

static void hide1_release_binding(struct hide1_binding *binding)
{
    if (binding->parent_path.dentry) {
        path_put(&binding->parent_path);
        binding->parent_path = (struct path){};
    }
    if (binding->parent_inode) {
        iput(binding->parent_inode);
        binding->parent_inode = NULL;
    }
    if (binding->target_nsproxy) {
        put_nsproxy(binding->target_nsproxy);
        binding->target_nsproxy = NULL;
    }
    binding->target_mnt_ns = NULL;
    binding->operation_mask = 0;
    memset(&binding->shadow, 0, sizeof(binding->shadow));
    binding->retiring = false;
    memset(&binding->rule, 0, sizeof(binding->rule));
}

static int hide1_reset_locked(void)
{
    if (hide1_binding.shadow.fop_meta &&
        atomic_read(&hide1_binding.shadow.fop_meta->open_count) != 0)
        return -EBUSY;

    int ret = hide1_shadow_uninstall_locked(&hide1_binding);

    if (ret)
        return ret;

    hide1_release_binding(&hide1_binding);
    hide1_lifecycle = HIDE1_LIFECYCLE_FREE;
    hide1_status.state = PATHGUARD_HIDE1_STATE_UNSUPPORTED;
    hide1_status.last_error = -EOPNOTSUPP;
    hide1_status.target_uid = 0;
    hide1_status.target_pid = 0;
    hide1_status.target_mnt_ns = 0;
    hide1_status.generation = 0;
    hide1_status.operation_mask = 0;
    hide1_status.parent_inode = 0;
    return ret;
}

static int hide1_prepare_binding(const struct pathguard_hide1_rule *rule,
                                 struct hide1_binding *binding)
{
    struct task_struct *task;
    struct pid *pid;
    struct nsproxy *nsproxy;
    const struct cred *cred;
    struct path parent;
    struct inode *inode;
    u64 operation_mask;
    int ret;

    pid = find_get_pid(rule->target_pid);
    if (!pid)
        return -ESRCH;
    task = get_pid_task(pid, PIDTYPE_PID);
    put_pid(pid);
    if (!task)
        return -ESRCH;
    cred = get_task_cred(task);
    if (!cred || __kuid_val(cred->fsuid) != rule->target_uid) {
        if (cred)
            put_cred(cred);
        put_task_struct(task);
        return -EPERM;
    }
    put_cred(cred);

    task_lock(task);
    nsproxy = task->nsproxy;
    if (nsproxy)
        get_nsproxy(nsproxy);
    task_unlock(task);
    put_task_struct(task);
    if (!nsproxy || !nsproxy->mnt_ns) {
        if (nsproxy)
            put_nsproxy(nsproxy);
        return -ESRCH;
    }

    /* Resolving with kern_path uses the caller's mount namespace.  Refuse
     * cross-namespace installation instead of silently binding the wrong one.
     */
    if (current->nsproxy->mnt_ns != nsproxy->mnt_ns) {
        put_nsproxy(nsproxy);
        return -EXDEV;
    }

    ret = kern_path(rule->parent, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &parent);
    if (ret) {
        put_nsproxy(nsproxy);
        return ret;
    }
    inode = d_backing_inode(parent.dentry);
    if (!inode || !S_ISDIR(inode->i_mode)) {
        path_put(&parent);
        put_nsproxy(nsproxy);
        return -ENOTDIR;
    }
    operation_mask = hide1_operation_mask(inode, parent.dentry);
    if ((operation_mask & PATHGUARD_HIDE1_REQUIRED_OPS) !=
        PATHGUARD_HIDE1_REQUIRED_OPS) {
        path_put(&parent);
        put_nsproxy(nsproxy);
        return -EOPNOTSUPP;
    }
    if (!igrab(inode)) {
        path_put(&parent);
        put_nsproxy(nsproxy);
        return -ESTALE;
    }
    binding->rule = *rule;
    binding->parent_path = parent;
    binding->parent_inode = inode;
    binding->target_nsproxy = nsproxy;
    binding->target_mnt_ns = nsproxy->mnt_ns;
    binding->operation_mask = operation_mask;
    binding->shadow.orig_iop = inode->i_op;
    binding->shadow.orig_fop = inode->i_fop;
    binding->shadow.iop_meta = NULL;
    binding->shadow.fop_meta = NULL;
    binding->shadow.module_pin = false;
    binding->parent_dop = parent.dentry->d_op;
    INIT_LIST_HEAD(&binding->dentry_shadows);
    spin_lock_init(&binding->dentry_lock);
    binding->retiring = false;
    return 0;
}

static void hide1_commit_binding(struct hide1_binding *binding)
{
    const struct inode *inode = binding->parent_inode;
    const struct ns_common *mnt_ns = from_mnt_ns(binding->target_mnt_ns);

    hide1_release_binding(&hide1_binding);
    hide1_binding = *binding;
    memset(binding, 0, sizeof(*binding));
    hide1_status.state = PATHGUARD_HIDE1_STATE_INACTIVE;
    hide1_status.last_error = 0;
    hide1_status.target_uid = hide1_binding.rule.target_uid;
    hide1_status.target_pid = hide1_binding.rule.target_pid;
    hide1_status.target_mnt_ns = mnt_ns->inum;
    hide1_status.generation = hide1_binding.rule.expected_generation;
    hide1_status.operation_mask = hide1_binding.operation_mask;
    hide1_status.parent_inode = inode->i_ino;
}

static long hide1_ioctl(struct file *file, unsigned int command,
                        unsigned long argument)
{
    struct pathguard_hide1_rule rule;
    struct hide1_binding *binding;
    struct pathguard_hide1_status status;
    u64 generation;
    int ret;

    (void)file;
    mutex_lock(&hide1_lock);
    if (command == PATHGUARD_HIDE1_IOC_DISABLE ||
        command == PATHGUARD_HIDE1_IOC_CLEAR) {
        /* Let a worker that already owns the mutex finish before waiting for
         * its work item; cancelling while holding hide1_lock can deadlock. */
        mutex_unlock(&hide1_lock);
        cancel_work_sync(&hide1_dop_stale_work);
        mutex_lock(&hide1_lock);
    }
    switch (command) {
    case PATHGUARD_HIDE1_IOC_INSTALL:
        binding = kzalloc(sizeof(*binding), GFP_KERNEL);
        if (!binding) {
            mutex_unlock(&hide1_lock);
            return -ENOMEM;
        }
        if (copy_from_user(&rule, (void __user *)argument, sizeof(rule))) {
            kfree(binding);
            mutex_unlock(&hide1_lock);
            return -EFAULT;
        }
        if (rule.abi_version != PATHGUARD_HIDE1_ABI_VERSION ||
            rule.size != sizeof(rule) || rule.target_uid < 10000 ||
            rule.target_pid <= 0 ||
            rule.expected_generation == 0 || rule.parent[0] != '/' ||
            rule.basename[0] == '\0' ||
            strcmp(rule.basename, ".") == 0 ||
            strcmp(rule.basename, "..") == 0 ||
            strnlen(rule.parent, sizeof(rule.parent)) >= sizeof(rule.parent) ||
            strnlen(rule.basename, sizeof(rule.basename)) >= sizeof(rule.basename) ||
            strchr(rule.basename, '/') != NULL) {
            kfree(binding);
            mutex_unlock(&hide1_lock);
            return -EINVAL;
        }
        ret = hide1_prepare_binding(&rule, binding);
        if (ret) {
            hide1_status.last_error = ret;
        } else {
            if (hide1_status.state == PATHGUARD_HIDE1_STATE_ACTIVE ||
                (hide1_binding.shadow.fop_meta &&
                 atomic_read(&hide1_binding.shadow.fop_meta->open_count) != 0)) {
                ret = -EBUSY;
                hide1_status.last_error = ret;
            } else {
                hide1_commit_binding(binding);
                hide1_lifecycle = HIDE1_LIFECYCLE_READY;
            }
        }
        hide1_release_binding(binding);
        kfree(binding);
        mutex_unlock(&hide1_lock);
        return ret;

    case PATHGUARD_HIDE1_IOC_ENABLE:
        if (copy_from_user(&generation, (void __user *)argument,
                           sizeof(generation))) {
            mutex_unlock(&hide1_lock);
            return -EFAULT;
        }
        if (generation == 0 || generation != hide1_status.generation) {
            hide1_status.last_error = -ESTALE;
            mutex_unlock(&hide1_lock);
            return -ESTALE;
        }
        ret = hide1_shadow_install_locked(&hide1_binding);
        if (ret) {
            hide1_status.last_error = ret;
            hide1_status.state = PATHGUARD_HIDE1_STATE_INACTIVE;
            mutex_unlock(&hide1_lock);
            return ret;
        }
        hide1_binding.retiring = false;
        hide1_lifecycle = HIDE1_LIFECYCLE_RUNNING;
        hide1_status.state = PATHGUARD_HIDE1_STATE_ACTIVE;
        hide1_status.last_error = 0;
        mutex_unlock(&hide1_lock);
        return 0;

    case PATHGUARD_HIDE1_IOC_DISABLE:
        if (hide1_status.state == PATHGUARD_HIDE1_STATE_ACTIVE) {
            ret = hide1_shadow_uninstall_locked(&hide1_binding);
            hide1_binding.retiring = false;
            hide1_lifecycle = HIDE1_LIFECYCLE_READY;
            hide1_status.state = PATHGUARD_HIDE1_STATE_INACTIVE;
            hide1_status.last_error = ret;
            mutex_unlock(&hide1_lock);
            return ret;
        }
        hide1_status.last_error = hide1_status.state ==
                PATHGUARD_HIDE1_STATE_INACTIVE ? 0 : -EOPNOTSUPP;
        mutex_unlock(&hide1_lock);
        return 0;

    case PATHGUARD_HIDE1_IOC_CLEAR:
        ret = hide1_reset_locked();
        mutex_unlock(&hide1_lock);
        return ret;

    case PATHGUARD_HIDE1_IOC_STATUS:
        status = hide1_status;
        mutex_unlock(&hide1_lock);
        return copy_to_user((void __user *)argument, &status, sizeof(status))
                   ? -EFAULT
                   : 0;
    default:
        mutex_unlock(&hide1_lock);
        return -ENOTTY;
    }
}

static const struct file_operations hide1_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = hide1_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = hide1_ioctl,
#endif
};

static struct miscdevice hide1_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "pathguard_hide1",
    .fops = &hide1_fops,
    .mode = 0600,
};

static int __init hide1_init(void)
{
    if (hide1_shadow_mode < 0 || hide1_shadow_mode > 3)
        return -EINVAL;
    if (strcmp(init_utsname()->release, PATHGUARD_HIDE1_EXPECTED_RELEASE) != 0)
        return -ENODEV;
    INIT_LIST_HEAD(&hide1_binding.dentry_shadows);
    spin_lock_init(&hide1_binding.dentry_lock);
    hash_init(hide1_iop_table);
    hash_init(hide1_fop_table);
    hash_init(hide1_dop_table);
    INIT_WORK(&hide1_dop_stale_work, hide1_dop_stale_workfn);
    hide1_lifecycle = HIDE1_LIFECYCLE_READY;
    strscpy(hide1_status.kernel_release, init_utsname()->release,
            sizeof(hide1_status.kernel_release));
    hide1_status.last_error = -EOPNOTSUPP;
    return misc_register(&hide1_device);
}

static void __exit hide1_exit(void)
{
    misc_deregister(&hide1_device);
    hide1_lifecycle = HIDE1_LIFECYCLE_STOP_NEW;
    cancel_work_sync(&hide1_dop_stale_work);
    mutex_lock(&hide1_lock);
    (void)hide1_reset_locked();
    mutex_unlock(&hide1_lock);
}

module_init(hide1_init);
module_exit(hide1_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("PathGuard");
MODULE_DESCRIPTION("PathGuard Hide 1.0 fixed-device VFS shadow prototype");
MODULE_VERSION("0.3.0-prototype");
