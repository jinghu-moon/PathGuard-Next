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
#include <linux/jiffies.h>
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

/* 0=all, 1=i_op only, 2=f_op only, 3=dentry d_op only,
 * 4=FUSE read-only (lookup/atomic_open/readdir/revalidate). */
/* Keep the module's direct-load default aligned with the last stable lab
 * package.  f_op/d_op are opt-in until their bridge has passed device tests. */
static int hide1_shadow_mode = 1;
module_param_named(shadow_mode, hide1_shadow_mode, int, 0600);
MODULE_PARM_DESC(shadow_mode,
                 "Shadow isolation mode: 0=all, 1=i_op, 2=f_op, 3=d_op, 4=fuse-ro");

struct hide1_binding;

struct hide1_iop_meta {
    struct hlist_node node;
    struct inode *inode;
    struct hide1_binding *binding;
    const struct inode_operations *orig;
    struct inode_operations shadow;
    atomic_t active;
    wait_queue_head_t wait;
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
    wait_queue_head_t wait;
};

struct hide1_dentry_shadow {
    struct hlist_node hash_node;
    struct list_head node;
    struct dentry *dentry;
    const struct dentry_operations *orig_dop;
    struct dentry_operations shadow_dop;
    struct hide1_binding *binding;
    unsigned int orig_flags;
    u64 cache_generation;
    bool synthetic_negative;
    atomic_t active;
    wait_queue_head_t wait;
    unsigned long state;
};

struct hide1_shadow {
    const struct inode_operations *orig_iop;
    const struct file_operations *orig_fop;
    struct hide1_iop_meta *iop_meta;
    struct hide1_iop_meta *hidden_iop_meta;
    struct hide1_fop_meta *fop_meta;
    bool iop_installed;
    bool hidden_iop_installed;
    bool fop_installed;
    bool module_pin;
};

struct hide1_binding {
    struct pathguard_hide1_rule rule;
    struct path parent_path;
    struct inode *parent_inode;
    struct inode *hidden_inode;
    struct super_block *parent_sb;
    struct task_struct *target_task;
    struct nsproxy *target_nsproxy;
    struct mnt_namespace *target_mnt_ns;
    u64 operation_mask;
    struct hide1_shadow shadow;
    const struct dentry_operations *parent_dop;
    struct list_head dentry_shadows;
    spinlock_t dentry_lock;
    spinlock_t identity_lock;
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
static struct delayed_work hide1_dop_stale_work;
#define HIDE1_DOP_STALE 0
static u32 hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_FREE;
static struct pathguard_hide1_status hide1_status = {
    .abi_version = PATHGUARD_HIDE1_ABI_VERSION,
    .size = sizeof(struct pathguard_hide1_status),
    .state = PATHGUARD_HIDE1_STATE_UNSUPPORTED,
};
static atomic64_t hide1_lookup_calls = ATOMIC64_INIT(0);
static atomic64_t hide1_lookup_hidden = ATOMIC64_INIT(0);
static atomic64_t hide1_atomic_open_calls = ATOMIC64_INIT(0);
static atomic64_t hide1_atomic_open_hidden = ATOMIC64_INIT(0);
static atomic64_t hide1_readdir_calls = ATOMIC64_INIT(0);
static atomic64_t hide1_readdir_filtered = ATOMIC64_INIT(0);
static atomic64_t hide1_d_revalidate_calls = ATOMIC64_INIT(0);
static atomic64_t hide1_d_revalidate_hidden = ATOMIC64_INIT(0);
static atomic64_t hide1_dentry_install_calls = ATOMIC64_INIT(0);
static atomic64_t hide1_dentry_install_success = ATOMIC64_INIT(0);
static atomic64_t hide1_dentry_install_failures = ATOMIC64_INIT(0);
static atomic64_t hide1_mutation_calls = ATOMIC64_INIT(0);
static atomic64_t hide1_mutation_blocked_calls = ATOMIC64_INIT(0);
static atomic64_t hide1_mutation_original = ATOMIC64_INIT(0);
static atomic64_t hide1_mutation_unsupported = ATOMIC64_INIT(0);

enum hide1_mutation_operation {
    HIDE1_MUTATION_ATOMIC_OPEN,
    HIDE1_MUTATION_CREATE,
    HIDE1_MUTATION_MKDIR,
    HIDE1_MUTATION_MKNOD,
    HIDE1_MUTATION_SYMLINK,
    HIDE1_MUTATION_UNLINK,
    HIDE1_MUTATION_RMDIR,
    HIDE1_MUTATION_LINK,
    HIDE1_MUTATION_RENAME,
    HIDE1_MUTATION_COUNT,
};

enum hide1_mutation_outcome {
    HIDE1_MUTATION_BLOCKED,
    HIDE1_MUTATION_ORIGINAL,
    HIDE1_MUTATION_UNSUPPORTED,
};

struct hide1_mutation_atomic_counters {
    atomic64_t calls;
    atomic64_t blocked;
    atomic64_t original;
    atomic64_t unsupported;
};

static struct hide1_mutation_atomic_counters
    hide1_mutation_by_operation[HIDE1_MUTATION_COUNT];

static void hide1_mutation_begin(enum hide1_mutation_operation operation)
{
    atomic64_inc(&hide1_mutation_calls);
    atomic64_inc(&hide1_mutation_by_operation[operation].calls);
}

static void hide1_mutation_finish(enum hide1_mutation_operation operation,
                                  enum hide1_mutation_outcome outcome)
{
    switch (outcome) {
    case HIDE1_MUTATION_BLOCKED:
        atomic64_inc(&hide1_mutation_blocked_calls);
        atomic64_inc(&hide1_mutation_by_operation[operation].blocked);
        break;
    case HIDE1_MUTATION_ORIGINAL:
        atomic64_inc(&hide1_mutation_original);
        atomic64_inc(&hide1_mutation_by_operation[operation].original);
        break;
    case HIDE1_MUTATION_UNSUPPORTED:
        atomic64_inc(&hide1_mutation_unsupported);
        atomic64_inc(&hide1_mutation_by_operation[operation].unsupported);
        break;
    }
}

static void hide1_snapshot_mutation_counters(
    struct pathguard_hide1_mutation_counters *destination,
    enum hide1_mutation_operation operation)
{
    const struct hide1_mutation_atomic_counters *source =
        &hide1_mutation_by_operation[operation];

    destination->calls = atomic64_read(&source->calls);
    destination->blocked = atomic64_read(&source->blocked);
    destination->original = atomic64_read(&source->original);
    destination->unsupported = atomic64_read(&source->unsupported);
}

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

static void hide1_callback_enter(atomic_t *global, atomic_t *local)
{
    atomic_inc(global);
    if (local)
        atomic_inc(local);
}

static void hide1_callback_exit(atomic_t *global, atomic_t *local,
                                wait_queue_head_t *global_wait,
                                wait_queue_head_t *local_wait)
{
    if (local && atomic_dec_and_test(local))
        wake_up_all(local_wait);
    if (atomic_dec_and_test(global))
        wake_up_all(global_wait);
}

static int hide1_preflight_dentry_restore(struct hide1_binding *binding)
{
    const struct hide1_dentry_shadow *meta;
    unsigned long flags;
    int ret = 0;

    if (!binding)
        return -EINVAL;
    spin_lock_irqsave(&binding->dentry_lock, flags);
    list_for_each_entry(meta, &binding->dentry_shadows, node) {
        spin_lock(&meta->dentry->d_lock);
        if (READ_ONCE(meta->dentry->d_op) != &meta->shadow_dop &&
            READ_ONCE(meta->dentry->d_op) != meta->orig_dop)
            ret = -EAGAIN;
        spin_unlock(&meta->dentry->d_lock);
        if (ret)
            break;
    }
    spin_unlock_irqrestore(&binding->dentry_lock, flags);
    return ret;
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
static void hide1_record_hidden_inode(struct hide1_binding *binding,
                                      struct inode *inode);
static bool hide1_mutation_blocked(struct hide1_binding *binding,
                                   struct inode *parent,
                                   struct dentry *dentry);

static void hide1_mark_dentry_stale(struct hide1_dentry_shadow *meta)
{
    if (!meta || READ_ONCE(hide1_lifecycle) !=
        PATHGUARD_HIDE1_LIFECYCLE_RUNNING)
        return;
    if (!test_and_set_bit(HIDE1_DOP_STALE, &meta->state))
        schedule_delayed_work(&hide1_dop_stale_work, 1);
}

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
    bool retry = false;

    (void)work;
    mutex_lock(&hide1_lock);
    spin_lock_irqsave(&hide1_binding.dentry_lock, flags);
    list_for_each_entry_safe(meta, tmp, &hide1_binding.dentry_shadows, node) {
        bool can_retire;
        const struct dentry_operations *dop;

        /* While ACTIVE, a stale dentry must remain shadowed.  Restoring its
         * original d_op after d_drop creates a race in which a concurrent
         * path walk reuses the positive dentry before lookup() can publish a
         * new observer-aware shadow.  DISABLE/RESTORE owns the only path that
         * may retire these objects. */
        if (READ_ONCE(hide1_status.state) == PATHGUARD_HIDE1_STATE_ACTIVE) {
            clear_bit(HIDE1_DOP_STALE, &meta->state);
            continue;
        }

        if (!test_bit(HIDE1_DOP_STALE, &meta->state))
            continue;
        spin_lock(&meta->dentry->d_lock);
        dop = READ_ONCE(meta->dentry->d_op);
        can_retire = d_unhashed(meta->dentry) &&
                     d_count(meta->dentry) == 1 &&
                     (dop == &meta->shadow_dop || dop == meta->orig_dop);
        if (can_retire && dop == &meta->shadow_dop) {
            if (meta->orig_flags & DCACHE_OP_REVALIDATE)
                meta->dentry->d_flags |= DCACHE_OP_REVALIDATE;
            else
                meta->dentry->d_flags &= ~DCACHE_OP_REVALIDATE;
            smp_wmb();
            WRITE_ONCE(meta->dentry->d_op, meta->orig_dop);
        }
        spin_unlock(&meta->dentry->d_lock);
        if (can_retire)
            list_move_tail(&meta->node, &retired);
        else
            retry = true;
    }
    spin_unlock_irqrestore(&hide1_binding.dentry_lock, flags);
    list_for_each_entry_safe(meta, tmp, &retired, node) {
        spin_lock(&hide1_meta_lock);
        hash_del_rcu(&meta->hash_node);
        spin_unlock(&hide1_meta_lock);
    }
    mutex_unlock(&hide1_lock);
    if (list_empty(&retired)) {
        if (retry && READ_ONCE(hide1_lifecycle) ==
                     PATHGUARD_HIDE1_LIFECYCLE_RUNNING)
            schedule_delayed_work(&hide1_dop_stale_work,
                                  msecs_to_jiffies(50));
        return;
    }
    synchronize_srcu(&hide1_srcu);
    synchronize_rcu();
    /* d_revalidate drops the short RCU read-side section before it finishes
     * its policy work.  Do not free a stale shadow while such a callback can
     * still dereference its metadata. */
    wait_event(hide1_dop_wait, atomic_read(&hide1_dop_active) == 0);
    list_for_each_entry(meta, &retired, node)
        wait_event(meta->wait, atomic_read(&meta->active) == 0);
    hide1_free_dentry_shadows(&retired);
    if (retry && READ_ONCE(hide1_lifecycle) ==
                 PATHGUARD_HIDE1_LIFECYCLE_RUNNING)
        schedule_delayed_work(&hide1_dop_stale_work,
                              msecs_to_jiffies(50));
}

static bool hide1_is_target_observer(const struct hide1_binding *binding)
{
    if (!binding || READ_ONCE(binding->retiring) ||
        READ_ONCE(hide1_status.state) != PATHGUARD_HIDE1_STATE_ACTIVE)
        return false;
    if (!current->nsproxy || current->nsproxy->mnt_ns != binding->target_mnt_ns)
        return false;
    if (READ_ONCE(hide1_status.generation) != binding->rule.expected_generation)
        return false;
    if (!binding->target_task ||
        (READ_ONCE(binding->target_task->flags) & PF_EXITING) ||
        !same_thread_group(current, binding->target_task))
        return false;
    return __kuid_val(current_fsuid()) == binding->rule.target_uid;
}

/* A target task is pinned for the lifetime of the binding, so its task_struct
 * remains safe to inspect after exit.  Do not install a process-exit hook in
 * this experimental LKM: PF_EXITING is sufficient to revoke the policy at
 * every ingress and lets userspace perform the normal restore/drain path. */
static bool hide1_target_exited_locked(const struct hide1_binding *binding)
{
    return binding && binding->target_task &&
           (READ_ONCE(binding->target_task->flags) & PF_EXITING);
}

static void hide1_revoke_dead_target_locked(void)
{
    if (hide1_status.state != PATHGUARD_HIDE1_STATE_ACTIVE ||
        !hide1_target_exited_locked(&hide1_binding))
        return;

    /* Publish fail-closed state before userspace observes STATUS.  The
     * shadow vectors remain installed until DISABLE/CLEAR completes the
     * transactional RESTORE -> DRAIN sequence. */
    hide1_binding.retiring = true;
    hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_STOP_NEW;
    WRITE_ONCE(hide1_status.state, PATHGUARD_HIDE1_STATE_INACTIVE);
    hide1_status.last_error = -ESRCH;
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
    return hide1_shadow_mode == 0 || hide1_shadow_mode == 1 ||
           hide1_shadow_mode == 4;
}

static bool hide1_mode_has_fop(void)
{
    return hide1_shadow_mode == 0 || hide1_shadow_mode == 2 ||
           hide1_shadow_mode == 4;
}

static bool hide1_mode_has_dop(void)
{
    return hide1_shadow_mode == 0 || hide1_shadow_mode == 3 ||
           hide1_shadow_mode == 4;
}

static bool hide1_mode_is_readonly(void)
{
    return hide1_shadow_mode == 4;
}

static u64 hide1_required_operation_mask(void)
{
    if (hide1_mode_is_readonly())
        return PATHGUARD_HIDE1_OP_LOOKUP |
               PATHGUARD_HIDE1_OP_ATOMIC_OPEN |
               PATHGUARD_HIDE1_OP_READDIR |
               PATHGUARD_HIDE1_OP_REVALIDATE;
    return PATHGUARD_HIDE1_REQUIRED_OPS;
}

static bool hide1_should_hide(const struct hide1_binding *binding,
                              const struct inode *parent,
                              const struct dentry *dentry)
{
    return hide1_is_target_observer(binding) &&
           parent && parent->i_sb == binding->parent_sb &&
           parent->i_ino == binding->parent_inode->i_ino &&
           hide1_name_matches(binding, dentry);
}

static bool hide1_update_dentry_shadow_locked(
    struct hide1_binding *binding, struct dentry *dentry,
    bool synthetic_negative)
{
    struct hide1_dentry_shadow *meta;
    const struct dentry_operations *dop;
    bool present = false;

    dop = READ_ONCE(dentry->d_op);
    if (!dop || dop->d_revalidate != hide1_d_revalidate)
        return false;
    rcu_read_lock();
    meta = hide1_dop_lookup_rcu(dentry);
    if (meta && meta->binding == binding) {
        if (synthetic_negative) {
            WRITE_ONCE(meta->cache_generation,
                       binding->rule.expected_generation);
            WRITE_ONCE(meta->synthetic_negative, true);
        }
        present = true;
    }
    rcu_read_unlock();
    return present;
}

static bool hide1_dentry_shadow_present(struct hide1_binding *binding,
                                        struct dentry *dentry,
                                        bool synthetic_negative)
{
    bool present;

    if (!dentry)
        return false;

    /* Do not return a metadata pointer after leaving RCU.  The stale worker
     * is allowed to retire and free that object as soon as its grace period
     * completes.  The dentry lock is the ownership check needed here; the
    * second check below closes the install race. */
    spin_lock(&dentry->d_lock);
    present = hide1_update_dentry_shadow_locked(
        binding, dentry, synthetic_negative);
    spin_unlock(&dentry->d_lock);
    return present;
}

static int hide1_install_dentry_shadow(struct hide1_binding *binding,
                                       struct dentry *dentry,
                                       bool synthetic_negative)
{
    struct hide1_dentry_shadow *meta;
    const struct dentry_operations *orig;
    unsigned long flags;

    atomic64_inc(&hide1_dentry_install_calls);
    if (!dentry) {
        atomic64_inc(&hide1_dentry_install_failures);
        return -EINVAL;
    }
    if (hide1_dentry_shadow_present(binding, dentry, synthetic_negative))
        return 0;
    if (READ_ONCE(binding->retiring)) {
        atomic64_inc(&hide1_dentry_install_failures);
        return -ESHUTDOWN;
    }

    spin_lock(&dentry->d_lock);
    orig = READ_ONCE(dentry->d_op);
    if (orig && orig->d_revalidate == hide1_d_revalidate) {
        bool present = hide1_update_dentry_shadow_locked(
            binding, dentry, synthetic_negative);

        spin_unlock(&dentry->d_lock);
        return present ? 0 : -EAGAIN;
    }
    spin_unlock(&dentry->d_lock);
    meta = kzalloc(sizeof(*meta), GFP_ATOMIC);
    if (!meta) {
        atomic64_inc(&hide1_dentry_install_failures);
        return -ENOMEM;
    }
    meta->dentry = dget(dentry);
    meta->orig_dop = orig;
    meta->binding = binding;
    meta->cache_generation = binding->rule.expected_generation;
    meta->synthetic_negative = synthetic_negative;
    if (orig)
        meta->shadow_dop = *orig;
    meta->shadow_dop.d_revalidate = hide1_d_revalidate;

    atomic_set(&meta->active, 0);
    init_waitqueue_head(&meta->wait);
    spin_lock_irqsave(&binding->dentry_lock, flags);
    spin_lock(&dentry->d_lock);
    if (binding->retiring) {
        spin_unlock(&dentry->d_lock);
        spin_unlock_irqrestore(&binding->dentry_lock, flags);
        dput(meta->dentry);
        kfree(meta);
        atomic64_inc(&hide1_dentry_install_failures);
        return -EAGAIN;
    }
    if (READ_ONCE(dentry->d_op) != orig) {
        const struct dentry_operations *observed_dop = READ_ONCE(dentry->d_op);

        /* Another racing lookup may have installed the same shadow. */
        if (observed_dop && observed_dop->d_revalidate == hide1_d_revalidate &&
            hide1_update_dentry_shadow_locked(binding, dentry,
                                               synthetic_negative)) {
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
        atomic64_inc(&hide1_dentry_install_failures);
        return -EAGAIN;
    }
    meta->orig_flags = READ_ONCE(dentry->d_flags);
    list_add_tail(&meta->node, &binding->dentry_shadows);
    spin_lock(&hide1_meta_lock);
    hash_add_rcu(hide1_dop_table, &meta->hash_node,
                 (unsigned long)dentry);
    spin_unlock(&hide1_meta_lock);
    smp_wmb();
    WRITE_ONCE(dentry->d_op, &meta->shadow_dop);
    WRITE_ONCE(dentry->d_flags, meta->orig_flags | DCACHE_OP_REVALIDATE);
    spin_unlock(&dentry->d_lock);
    spin_unlock_irqrestore(&binding->dentry_lock, flags);
    atomic64_inc(&hide1_dentry_install_success);
    return 0;
}

static int hide1_install_iop_shadow_locked(
    struct hide1_binding *binding, struct inode *inode,
    const struct inode_operations *expected, bool parent_ingress,
    struct hide1_iop_meta **slot)
{
    struct hide1_iop_meta *meta;
    const struct inode_operations *observed;

    if (!binding || !inode || !expected || !slot || *slot)
        return -EINVAL;
    if (READ_ONCE(inode->i_op) != expected)
        return -EAGAIN;

    meta = kzalloc(sizeof(*meta), GFP_KERNEL);
    if (!meta)
        return -ENOMEM;
    meta->inode = inode;
    meta->binding = binding;
    meta->orig = expected;
    meta->shadow = *expected;
    if (parent_ingress) {
        meta->shadow.lookup = hide1_lookup;
        meta->shadow.atomic_open = hide1_atomic_open;
    }
    if (!hide1_mode_is_readonly()) {
        /* A previously opened governed directory reaches descendants through
         * its own inode.  Keep reads unchanged for now, but route every
         * mutation ingress (including O_CREAT/O_TRUNC atomic_open) through
         * the same observer check as the governed parent. */
        meta->shadow.atomic_open = hide1_atomic_open;
        meta->shadow.create = hide1_create;
        meta->shadow.mkdir = hide1_mkdir;
        meta->shadow.mknod = hide1_mknod;
        meta->shadow.symlink = hide1_symlink;
        meta->shadow.unlink = hide1_unlink;
        meta->shadow.rmdir = hide1_rmdir;
        meta->shadow.link = hide1_link;
        meta->shadow.rename = hide1_rename;
    }
    atomic_set(&meta->active, 0);
    init_waitqueue_head(&meta->wait);

    spin_lock(&hide1_meta_lock);
    hash_add_rcu(hide1_iop_table, &meta->node, (unsigned long)inode);
    spin_unlock(&hide1_meta_lock);
    smp_wmb();
    observed = cmpxchg(&inode->i_op, expected, &meta->shadow);
    if (observed != expected) {
        spin_lock(&hide1_meta_lock);
        hash_del_rcu(&meta->node);
        spin_unlock(&hide1_meta_lock);
        synchronize_rcu();
        kfree(meta);
        return -EAGAIN;
    }
    *slot = meta;
    return 0;
}

static int hide1_install_named_object_shadows(struct hide1_binding *binding)
{
    char pathbuf[PATHGUARD_HIDE1_PATH_MAX + PATHGUARD_HIDE1_NAME_MAX + 2];
    struct path child;
    struct inode *child_inode;
    struct inode *child_parent;
    int ret;

    if (!binding ||
        (!hide1_mode_has_dop() &&
         (!hide1_mode_has_iop() || hide1_mode_is_readonly())))
        return 0;
    ret = scnprintf(pathbuf, sizeof(pathbuf), "%s/%s",
                    binding->rule.parent, binding->rule.basename);
    if (ret >= sizeof(pathbuf))
        return -ENAMETOOLONG;
    ret = kern_path(pathbuf, LOOKUP_FOLLOW, &child);
    if (ret)
        return ret;
    child_parent = d_backing_inode(child.dentry->d_parent);
    if (!child_parent || child_parent != binding->parent_inode) {
        path_put(&child);
        return -EXDEV;
    }
    child_inode = d_backing_inode(child.dentry);
    if (!child_inode) {
        path_put(&child);
        return -ESTALE;
    }
    hide1_record_hidden_inode(binding, child_inode);
    if (hide1_mode_has_iop() && !hide1_mode_is_readonly() &&
        S_ISDIR(child_inode->i_mode)) {
        ret = hide1_install_iop_shadow_locked(
            binding, child_inode, READ_ONCE(child_inode->i_op), false,
            &binding->shadow.hidden_iop_meta);
        if (ret) {
            path_put(&child);
            return ret;
        }
        binding->shadow.hidden_iop_installed = true;
    }
    ret = hide1_mode_has_dop() ?
          hide1_install_dentry_shadow(binding, child.dentry, false) : 0;
    path_put(&child);
    return ret;
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
            if (meta->orig_flags & DCACHE_OP_REVALIDATE)
                meta->dentry->d_flags |= DCACHE_OP_REVALIDATE;
            else
                meta->dentry->d_flags &= ~DCACHE_OP_REVALIDATE;
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

static void hide1_drain_callbacks(struct hide1_iop_meta *im,
                                  struct hide1_iop_meta *hidden_im,
                                  struct hide1_fop_meta *fm,
                                  struct list_head *retired)
{
    struct hide1_dentry_shadow *dm;

    /* The first Tasks-RCU pass is issued by the RESTORE stage before indices
     * are removed.  Here we close the active-callback and reclamation sides. */
    wait_event(hide1_iop_wait, atomic_read(&hide1_iop_active) == 0);
    wait_event(hide1_fop_wait, atomic_read(&hide1_fop_active) == 0);
    wait_event(hide1_dop_wait, atomic_read(&hide1_dop_active) == 0);
    if (im)
        wait_event(im->wait, atomic_read(&im->active) == 0);
    if (hidden_im)
        wait_event(hidden_im->wait, atomic_read(&hidden_im->active) == 0);
    if (fm)
        wait_event(fm->wait, atomic_read(&fm->active) == 0);
    if (retired)
        list_for_each_entry(dm, retired, node)
            wait_event(dm->wait, atomic_read(&dm->active) == 0);
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
        hide1_callback_enter(&hide1_iop_active, &meta->active);
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
        hide1_callback_enter(&hide1_fop_active, &meta->active);
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
        hide1_callback_enter(&hide1_dop_active, &meta->active);
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
    hide1_callback_exit(&hide1_fop_active, &meta->active,
                        &hide1_fop_wait, &meta->wait);
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
    hide1_callback_exit(&hide1_fop_active, &meta->active,
                        &hide1_fop_wait, &meta->wait);
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

    atomic64_inc(&hide1_lookup_calls);
    if (!binding)
        return ERR_PTR(-EIO);

    idx = srcu_read_lock(&hide1_srcu);
    if (hide1_should_hide(binding, dir, dentry)) {
        atomic64_inc(&hide1_lookup_hidden);
        if (hide1_mode_has_dop() &&
            hide1_install_dentry_shadow(binding, dentry, true)) {
            ret = ERR_PTR(-EAGAIN);
            goto out;
        }
        d_add(dentry, NULL);
        ret = NULL;
        goto out;
    }
    orig = meta->orig;
    result = orig && orig->lookup ? orig->lookup(dir, dentry, flags) : NULL;
    if (!IS_ERR(result) && hide1_mode_has_dop() &&
        dir == binding->parent_inode && hide1_name_matches(binding, dentry)) {
        struct dentry *resolved = result ? result : dentry;
        int install_ret = hide1_install_dentry_shadow(binding, resolved,
                                                       false);

        if (install_ret) {
            /* Do not publish an ungoverned positive/negative cache entry.
             * Parallel lookup waiters are released only after this wrapper
             * returns and VFS calls d_lookup_done(). */
            d_drop(resolved);
            if (result && result != dentry)
                dput(result);
            ret = ERR_PTR(install_ret);
            goto out;
        }
        hide1_record_hidden_inode(binding, d_backing_inode(resolved));
    }
    ret = result;
out:
    hide1_callback_exit(&hide1_iop_active, &meta->active,
                        &hide1_iop_wait, &meta->wait);
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

    atomic64_inc(&hide1_atomic_open_calls);
    if (!binding) {
        srcu_read_unlock(&hide1_srcu, idx);
        return -EIO;
    }
    if (hide1_should_hide(binding, dir, dentry)) {
        atomic64_inc(&hide1_atomic_open_hidden);
        if (open_flag & (O_CREAT | O_EXCL | O_TRUNC)) {
            hide1_mutation_begin(HIDE1_MUTATION_ATOMIC_OPEN);
            hide1_mutation_finish(HIDE1_MUTATION_ATOMIC_OPEN,
                                  HIDE1_MUTATION_BLOCKED);
        }
        /* Kasumi deliberately keeps dentry installation in lookup().  The
         * FUSE atomic_open path may own an in-lookup dentry and perform its
         * own lookup/create transition; do not change d_op or hash state from
         * this callback. */
        ret = -ENOENT;
    } else if ((open_flag & (O_CREAT | O_EXCL | O_TRUNC)) &&
               hide1_mutation_blocked(binding, dir, dentry)) {
        hide1_mutation_begin(HIDE1_MUTATION_ATOMIC_OPEN);
        hide1_mutation_finish(HIDE1_MUTATION_ATOMIC_OPEN,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else {
        orig = meta->orig;
        if (open_flag & (O_CREAT | O_EXCL | O_TRUNC))
            hide1_mutation_begin(HIDE1_MUTATION_ATOMIC_OPEN);
        ret = orig && orig->atomic_open ?
              orig->atomic_open(dir, dentry, file, open_flag, create_mode) :
              -EOPNOTSUPP;
        if (open_flag & (O_CREAT | O_EXCL | O_TRUNC)) {
            if (orig && orig->atomic_open)
                hide1_mutation_finish(HIDE1_MUTATION_ATOMIC_OPEN,
                                      HIDE1_MUTATION_ORIGINAL);
            else
                hide1_mutation_finish(HIDE1_MUTATION_ATOMIC_OPEN,
                                      HIDE1_MUTATION_UNSUPPORTED);
        }
    }
    hide1_callback_exit(&hide1_iop_active, &meta->active,
                        &hide1_iop_wait, &meta->wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

struct hide1_dir_proxy {
    struct dir_context ctx;
    struct dir_context *orig;
    struct hide1_binding *binding;
    struct inode *dir_inode;
    struct dentry *parent_dentry;
};

static void hide1_drop_filtered_child(struct hide1_dir_proxy *proxy,
                                      const char *name, int namelen)
{
    struct qstr child_name;
    struct dentry *child;
    struct hide1_dentry_shadow *meta;

    if (!proxy || !proxy->parent_dentry || !name || namelen <= 0)
        return;

    child_name.name = name;
    child_name.len = namelen;
    child_name.hash = full_name_hash(proxy->dir_inode, name, namelen);
    child = d_lookup(proxy->parent_dentry, &child_name);
    if (!child)
        return;

    /* A filtered FUSE record can still have a positive dentry cached from an
     * earlier lookup.  Drop that cache entry before the next open/stat can
     * reuse it; otherwise readdir-then-open leaks the governed object. */
    d_drop(child);
    rcu_read_lock();
    meta = hide1_dop_lookup_rcu(child);
    if (meta && meta->binding == proxy->binding)
        hide1_mark_dentry_stale(meta);
    rcu_read_unlock();
    dput(child);
}

static bool hide1_dir_actor(struct dir_context *ctx, const char *name,
                            int namelen, loff_t offset, u64 ino,
                            unsigned int d_type)
{
    struct hide1_dir_proxy *proxy = container_of(ctx, struct hide1_dir_proxy, ctx);

    if (proxy->dir_inode == proxy->binding->parent_inode &&
        hide1_is_target_observer(proxy->binding) &&
        namelen == strlen(proxy->binding->rule.basename) &&
        !memcmp(name, proxy->binding->rule.basename, namelen)) {
        /* A filtered record still advances the native directory cookie. */
        hide1_drop_filtered_child(proxy, name, namelen);
        atomic64_inc(&hide1_readdir_filtered);
        proxy->ctx.pos = offset;
        return true;
    }

    /* Filesystems, including FUSE, may update ctx->pos while emitting a
     * record.  Keep the proxy and caller cursors synchronized exactly as the
     * native actor contract expects; otherwise a small getdents buffer can
     * repeat records or skip entries after filtering. */
    proxy->orig->pos = proxy->ctx.pos;
    {
        bool ret = proxy->orig->actor(proxy->orig, name, namelen, offset,
                                      ino, d_type);
        proxy->ctx.pos = proxy->orig->pos;
        return ret;
    }
}

static int hide1_iterate_shared(struct file *file, struct dir_context *ctx)
{
    struct hide1_fop_meta *meta = hide1_fop_enter(file);
    struct hide1_binding *binding = meta ? meta->binding : NULL;
    struct hide1_dir_proxy proxy;
    const struct file_operations *orig;
    int idx;
    int ret;

    atomic64_inc(&hide1_readdir_calls);
    if (!binding)
        return -EIO;
    idx = srcu_read_lock(&hide1_srcu);
    orig = meta->orig;
    if (!orig || !orig->iterate_shared) {
        hide1_callback_exit(&hide1_fop_active, &meta->active,
                            &hide1_fop_wait, &meta->wait);
        srcu_read_unlock(&hide1_srcu, idx);
        return -EOPNOTSUPP;
    }
    if (!hide1_is_target_observer(binding) ||
        file_inode(file) != binding->parent_inode) {
        ret = orig->iterate_shared(file, ctx);
        hide1_callback_exit(&hide1_fop_active, &meta->active,
                            &hide1_fop_wait, &meta->wait);
        srcu_read_unlock(&hide1_srcu, idx);
        return ret;
    }
    proxy.ctx = *ctx;
    proxy.ctx.actor = hide1_dir_actor;
    proxy.orig = ctx;
    proxy.binding = binding;
    proxy.dir_inode = file_inode(file);
    proxy.parent_dentry = file->f_path.dentry;
    ret = orig->iterate_shared(file, &proxy.ctx);
    ctx->pos = proxy.ctx.pos;
    hide1_callback_exit(&hide1_fop_active, &meta->active,
                        &hide1_fop_wait, &meta->wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

static int hide1_d_revalidate(struct dentry *dentry, unsigned int flags)
{
    struct hide1_dentry_shadow *meta;
    struct hide1_binding *binding;
    bool synthetic_negative;
    int idx;
    int ret;

    atomic64_inc(&hide1_d_revalidate_calls);
    idx = srcu_read_lock(&hide1_srcu);
    meta = hide1_dop_enter(dentry);
    binding = meta ? meta->binding : NULL;
    if (!binding) {
        srcu_read_unlock(&hide1_srcu, idx);
        return 1;
    }
    synthetic_negative = READ_ONCE(meta->synthetic_negative);
    if (synthetic_negative) {
        if (hide1_should_hide(binding,
                              d_backing_inode(dentry->d_parent), dentry) &&
            READ_ONCE(meta->cache_generation) ==
                binding->rule.expected_generation) {
            atomic64_inc(&hide1_d_revalidate_hidden);
            ret = 1;
        } else if (flags & LOOKUP_RCU) {
            ret = -ECHILD;
        } else {
            /* A synthetic target-only negative must never hide the real
             * object from another observer or generation. */
            hide1_mark_dentry_stale(meta);
            ret = 0;
        }
        goto out;
    }
    if (hide1_should_hide(binding, d_backing_inode(dentry->d_parent), dentry)) {
        atomic64_inc(&hide1_d_revalidate_hidden);
        if (d_is_negative(dentry)) {
            ret = 1;
        } else if (flags & LOOKUP_RCU) {
            ret = -ECHILD;
        } else {
            /* The real positive dentry is invalid for the target.  Retire it
             * only after VFS unhashed it and all path-walk references drain. */
            hide1_mark_dentry_stale(meta);
            ret = 0;
        }
        goto out;
    }
    ret = meta->orig_dop && meta->orig_dop->d_revalidate ?
          meta->orig_dop->d_revalidate(dentry, flags) : 1;
out:
    hide1_callback_exit(&hide1_dop_active, &meta->active,
                        &hide1_dop_wait, &meta->wait);
    srcu_read_unlock(&hide1_srcu, idx);
    return ret;
}

static bool hide1_mutation_blocked(struct hide1_binding *binding,
                                   struct inode *parent,
                                   struct dentry *dentry)
{
    struct hide1_iop_meta *hidden_meta;

    if (hide1_should_hide(binding, parent, dentry))
        return true;
    if (!hide1_is_target_observer(binding) || !parent)
        return false;
    hidden_meta = READ_ONCE(binding->shadow.hidden_iop_meta);
    return hidden_meta && parent == hidden_meta->inode;
}

static bool hide1_hidden_source(struct hide1_binding *binding,
                                struct dentry *old_dentry)
{
    struct inode *old_inode;
    bool match;

    if (!binding || !old_dentry)
        return false;
    old_inode = d_backing_inode(old_dentry);
    spin_lock(&binding->identity_lock);
    match = old_inode &&
            ((binding->hidden_inode && old_inode == binding->hidden_inode) ||
             (binding->shadow.hidden_iop_meta &&
              old_inode == binding->shadow.hidden_iop_meta->inode));
    spin_unlock(&binding->identity_lock);
    return match;
}

static void hide1_record_hidden_inode(struct hide1_binding *binding,
                                      struct inode *inode)
{
    struct inode *old;

    if (!binding || !inode || !igrab(inode))
        return;
    spin_lock(&binding->identity_lock);
    if (binding->shadow.hidden_iop_installed && binding->hidden_inode != inode) {
        spin_unlock(&binding->identity_lock);
        iput(inode);
        return;
    }
    old = binding->hidden_inode;
    binding->hidden_inode = inode;
    spin_unlock(&binding->identity_lock);
    if (old)
        iput(old);
}

#define HIDE1_IOP_GUARD(_inode, _meta, _binding, _idx)                 \
    (_meta) = hide1_iop_enter((_inode));                               \
    (_binding) = (_meta) ? (_meta)->binding : NULL;                   \
    if (!(_binding))                                                    \
        return -EIO;                                                    \
    (_idx) = srcu_read_lock(&hide1_srcu)

#define HIDE1_IOP_UNGUARD(_meta, _idx)                                 \
    hide1_callback_exit(&hide1_iop_active, &(_meta)->active,           \
                        &hide1_iop_wait, &(_meta)->wait);              \
    srcu_read_unlock(&hide1_srcu, (_idx))

static int hide1_create(struct mnt_idmap *idmap, struct inode *dir,
                        struct dentry *dentry, umode_t mode, bool excl)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    hide1_mutation_begin(HIDE1_MUTATION_CREATE);
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry)) {
        hide1_mutation_finish(HIDE1_MUTATION_CREATE,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else if (meta->orig && meta->orig->create) {
        hide1_mutation_finish(HIDE1_MUTATION_CREATE,
                              HIDE1_MUTATION_ORIGINAL);
        ret = meta->orig->create(idmap, dir, dentry, mode, excl);
    } else {
        hide1_mutation_finish(HIDE1_MUTATION_CREATE,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    }
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
    hide1_mutation_begin(HIDE1_MUTATION_MKDIR);
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry)) {
        hide1_mutation_finish(HIDE1_MUTATION_MKDIR,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else if (meta->orig && meta->orig->mkdir) {
        hide1_mutation_finish(HIDE1_MUTATION_MKDIR,
                              HIDE1_MUTATION_ORIGINAL);
        ret = meta->orig->mkdir(idmap, dir, dentry, mode);
    } else {
        hide1_mutation_finish(HIDE1_MUTATION_MKDIR,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    }
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
    hide1_mutation_begin(HIDE1_MUTATION_MKNOD);
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry)) {
        hide1_mutation_finish(HIDE1_MUTATION_MKNOD,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else if (meta->orig && meta->orig->mknod) {
        hide1_mutation_finish(HIDE1_MUTATION_MKNOD,
                              HIDE1_MUTATION_ORIGINAL);
        ret = meta->orig->mknod(idmap, dir, dentry, mode, dev);
    } else {
        hide1_mutation_finish(HIDE1_MUTATION_MKNOD,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    }
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
    hide1_mutation_begin(HIDE1_MUTATION_SYMLINK);
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry)) {
        hide1_mutation_finish(HIDE1_MUTATION_SYMLINK,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else if (meta->orig && meta->orig->symlink) {
        hide1_mutation_finish(HIDE1_MUTATION_SYMLINK,
                              HIDE1_MUTATION_ORIGINAL);
        ret = meta->orig->symlink(idmap, dir, dentry, symname);
    } else {
        hide1_mutation_finish(HIDE1_MUTATION_SYMLINK,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    }
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_unlink(struct inode *dir, struct dentry *dentry)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    hide1_mutation_begin(HIDE1_MUTATION_UNLINK);
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry)) {
        hide1_mutation_finish(HIDE1_MUTATION_UNLINK,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else if (meta->orig && meta->orig->unlink) {
        hide1_mutation_finish(HIDE1_MUTATION_UNLINK,
                              HIDE1_MUTATION_ORIGINAL);
        ret = meta->orig->unlink(dir, dentry);
    } else {
        hide1_mutation_finish(HIDE1_MUTATION_UNLINK,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    }
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_rmdir(struct inode *dir, struct dentry *dentry)
{
    struct hide1_iop_meta *meta;
    struct hide1_binding *binding;
    int idx;
    int ret;
    hide1_mutation_begin(HIDE1_MUTATION_RMDIR);
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, dentry)) {
        hide1_mutation_finish(HIDE1_MUTATION_RMDIR,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else if (meta->orig && meta->orig->rmdir) {
        hide1_mutation_finish(HIDE1_MUTATION_RMDIR,
                              HIDE1_MUTATION_ORIGINAL);
        ret = meta->orig->rmdir(dir, dentry);
    } else {
        hide1_mutation_finish(HIDE1_MUTATION_RMDIR,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    }
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
    hide1_mutation_begin(HIDE1_MUTATION_LINK);
    HIDE1_IOP_GUARD(dir, meta, binding, idx);
    if (hide1_mutation_blocked(binding, dir, new_dentry) ||
        hide1_hidden_source(binding, old_dentry) ||
        (old_dentry->d_parent &&
         hide1_mutation_blocked(binding, d_backing_inode(old_dentry->d_parent),
                                old_dentry))) {
        hide1_mutation_finish(HIDE1_MUTATION_LINK,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else if (meta->orig && meta->orig->link) {
        hide1_mutation_finish(HIDE1_MUTATION_LINK,
                              HIDE1_MUTATION_ORIGINAL);
        ret = meta->orig->link(old_dentry, dir, new_dentry);
    } else {
        hide1_mutation_finish(HIDE1_MUTATION_LINK,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    }
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
    hide1_mutation_begin(HIDE1_MUTATION_RENAME);
    HIDE1_IOP_GUARD(old_dir, meta, binding, idx);
    if (flags) {
        hide1_mutation_finish(HIDE1_MUTATION_RENAME,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    } else if (old_dir->i_sb != new_dir->i_sb) {
        hide1_mutation_finish(HIDE1_MUTATION_RENAME,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EXDEV;
    } else if (hide1_mutation_blocked(binding, old_dir, old_dentry) ||
               hide1_mutation_blocked(binding, new_dir, new_dentry) ||
               hide1_hidden_source(binding, old_dentry)) {
        hide1_mutation_finish(HIDE1_MUTATION_RENAME,
                              HIDE1_MUTATION_BLOCKED);
        ret = -ENOENT;
    } else if (meta->orig && meta->orig->rename) {
        hide1_mutation_finish(HIDE1_MUTATION_RENAME,
                              HIDE1_MUTATION_ORIGINAL);
        ret = meta->orig->rename(idmap, old_dir, old_dentry,
                                                new_dir, new_dentry, flags);
    } else {
        hide1_mutation_finish(HIDE1_MUTATION_RENAME,
                              HIDE1_MUTATION_UNSUPPORTED);
        ret = -EOPNOTSUPP;
    }
    HIDE1_IOP_UNGUARD(meta, idx);
    return ret;
}

static int hide1_shadow_install_locked(struct hide1_binding *binding)
{
    struct hide1_shadow *shadow = &binding->shadow;
    struct inode *inode = binding->parent_inode;
    struct hide1_iop_meta *im = NULL;
    struct hide1_iop_meta *him = NULL;
    struct hide1_fop_meta *fm = NULL;
    LIST_HEAD(retired);
    int ret;

    if (shadow->iop_installed || shadow->hidden_iop_installed ||
        shadow->fop_installed)
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
        ret = hide1_install_iop_shadow_locked(
            binding, inode, shadow->orig_iop, true, &shadow->iop_meta);
        if (ret)
            goto rollback;
        im = shadow->iop_meta;
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
        init_waitqueue_head(&fm->wait);
        spin_lock(&hide1_meta_lock);
        hash_add_rcu(hide1_fop_table, &fm->node, (unsigned long)inode);
        spin_unlock(&hide1_meta_lock);
        shadow->fop_meta = fm;
        smp_store_release(&inode->i_fop, &fm->ingress);
        shadow->fop_installed = true;
    }

    ret = hide1_mode_has_dop() ?
          hide1_install_dentry_shadow(binding, binding->parent_path.dentry,
                                      false) : 0;
    if (ret)
        goto rollback;
    /* Resolve the governed object once.  This both pins the inode used by
     * pre-opened directory FDs and installs its independent i_op shadow;
     * dentry mode additionally gives the positive object observer-aware
     * revalidation. */
    ret = hide1_install_named_object_shadows(binding);
    if (ret && ret != -ENOENT)
        goto rollback;
    return 0;

rollback:
    WRITE_ONCE(binding->retiring, true);
    if (shadow->fop_installed && shadow->fop_meta &&
        READ_ONCE(inode->i_fop) == &shadow->fop_meta->ingress) {
        smp_store_release(&inode->i_fop, shadow->orig_fop);
        shadow->fop_installed = false;
    }
    if (shadow->hidden_iop_installed && shadow->hidden_iop_meta &&
        READ_ONCE(shadow->hidden_iop_meta->inode->i_op) ==
            &shadow->hidden_iop_meta->shadow) {
        smp_store_release(&shadow->hidden_iop_meta->inode->i_op,
                          shadow->hidden_iop_meta->orig);
        shadow->hidden_iop_installed = false;
    }
    if (shadow->iop_installed && shadow->iop_meta &&
        READ_ONCE(inode->i_op) == &shadow->iop_meta->shadow) {
        smp_store_release(&inode->i_op, shadow->orig_iop);
        shadow->iop_installed = false;
    }
    (void)hide1_restore_dentry_shadows(binding, &retired);
    spin_lock(&hide1_meta_lock);
    if (shadow->iop_meta) {
        hash_del_rcu(&shadow->iop_meta->node);
        im = shadow->iop_meta;
        shadow->iop_meta = NULL;
    }
    if (shadow->hidden_iop_meta) {
        hash_del_rcu(&shadow->hidden_iop_meta->node);
        him = shadow->hidden_iop_meta;
        shadow->hidden_iop_meta = NULL;
    }
    if (shadow->fop_meta) {
        hash_del_rcu(&shadow->fop_meta->node);
        fm = shadow->fop_meta;
        shadow->fop_meta = NULL;
    }
    spin_unlock(&hide1_meta_lock);
    synchronize_rcu();
    /* A wrapper drops the short RCU read-side section immediately after
     * taking an active reference.  RCU alone therefore does not prove that
     * the wrapper stopped dereferencing metadata. */
    hide1_drain_callbacks(im, him, fm, &retired);
    hide1_drain_retired_dentries(&retired);
    hide1_free_dentry_shadows(&retired);
    kfree(im);
    kfree(him);
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
    struct hide1_iop_meta *him;
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

    /* Dentry ownership is part of the same uninstall transaction.  Refuse
     * before publishing any original inode/file vector if another owner has
     * replaced a governed d_op. */
    ret = hide1_preflight_dentry_restore(binding);
    if (ret)
        return ret;

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
    if (shadow->hidden_iop_installed &&
        (!shadow->hidden_iop_meta || !shadow->hidden_iop_meta->inode ||
         READ_ONCE(shadow->hidden_iop_meta->inode->i_op) !=
             &shadow->hidden_iop_meta->shadow))
        return -EAGAIN;

    if (!shadow->iop_installed && !shadow->hidden_iop_installed &&
        !shadow->fop_installed &&
        list_empty(&binding->dentry_shadows))
        return 0;

    /* STOP_NEW: block policy decisions before publishing original vectors. */
    hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_STOP_NEW;
    WRITE_ONCE(binding->retiring, true);
    WRITE_ONCE(hide1_status.state, PATHGUARD_HIDE1_STATE_INACTIVE);

    if (shadow->fop_installed)
        smp_store_release(&inode->i_fop, shadow->orig_fop);
    if (shadow->hidden_iop_installed)
        smp_store_release(&shadow->hidden_iop_meta->inode->i_op,
                          shadow->hidden_iop_meta->orig);
    if (shadow->iop_installed)
        smp_store_release(&inode->i_op, shadow->orig_iop);
    shadow->fop_installed = false;
    shadow->hidden_iop_installed = false;
    shadow->iop_installed = false;

    /* RESTORE: all ingress pointers now reference the original filesystem. */
    hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_RESTORE;

    /* Restore operation pointers first.  This prevents new calls from
     * entering the shadow; SRCU drains wrappers that already entered it and
     * RCU orders publication before dentry metadata is reclaimed. */
    dentry_ret = hide1_restore_dentry_shadows(binding, &retired);
    if (dentry_ret)
        ret = dentry_ret;
    /* DRAIN: close the stale-pointer window before withdrawing indices.  The
     * grace period must not run while hide1_lock is held: a callback or stale
     * worker may legitimately need that mutex to finish its epilogue. */
    hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_DRAINING;
    mutex_unlock(&hide1_lock);
    synchronize_rcu_tasks();
    mutex_lock(&hide1_lock);
    spin_lock(&hide1_meta_lock);
    im = shadow->iop_meta;
    him = shadow->hidden_iop_meta;
    fm = shadow->fop_meta;
    if (im) {
        hash_del_rcu(&im->node);
        shadow->iop_meta = NULL;
    }
    if (him) {
        hash_del_rcu(&him->node);
        shadow->hidden_iop_meta = NULL;
    }
    if (fm) {
        hash_del_rcu(&fm->node);
        shadow->fop_meta = NULL;
    }
    spin_unlock(&hide1_meta_lock);

    mutex_unlock(&hide1_lock);
    hide1_drain_callbacks(im, him, fm, &retired);
    mutex_lock(&hide1_lock);
    hide1_drain_retired_dentries(&retired);
    hide1_free_dentry_shadows(&retired);
    kfree(im);
    kfree(him);
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
    if (binding->hidden_inode) {
        struct inode *hidden;
        spin_lock(&binding->identity_lock);
        hidden = binding->hidden_inode;
        binding->hidden_inode = NULL;
        spin_unlock(&binding->identity_lock);
        iput(hidden);
    }
    binding->parent_sb = NULL;
    if (binding->target_task) {
        put_task_struct(binding->target_task);
        binding->target_task = NULL;
    }
    if (binding->target_nsproxy) {
        put_nsproxy(binding->target_nsproxy);
        binding->target_nsproxy = NULL;
    }
    binding->target_mnt_ns = NULL;
    binding->parent_dop = NULL;
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
    hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_FREE;
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
    get_task_struct(task);
    task_unlock(task);
    put_task_struct(task);
    if (!nsproxy || !nsproxy->mnt_ns) {
        if (nsproxy)
            put_nsproxy(nsproxy);
        put_task_struct(task);
        return -ESRCH;
    }

    /* Resolving with kern_path uses the caller's mount namespace.  Refuse
     * cross-namespace installation instead of silently binding the wrong one.
     */
    if (current->nsproxy->mnt_ns != nsproxy->mnt_ns) {
        put_nsproxy(nsproxy);
        put_task_struct(task);
        return -EXDEV;
    }

    ret = kern_path(rule->parent, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &parent);
    if (ret) {
        put_nsproxy(nsproxy);
        put_task_struct(task);
        return ret;
    }
    inode = d_backing_inode(parent.dentry);
    if (!inode || !S_ISDIR(inode->i_mode)) {
        path_put(&parent);
        put_nsproxy(nsproxy);
        put_task_struct(task);
        return -ENOTDIR;
    }
    if (!inode->i_sb || !inode->i_sb->s_type ||
        strcmp(inode->i_sb->s_type->name, "fuse") != 0) {
        path_put(&parent);
        put_nsproxy(nsproxy);
        put_task_struct(task);
        return -EOPNOTSUPP;
    }
    operation_mask = hide1_operation_mask(inode, parent.dentry);
    if ((operation_mask & hide1_required_operation_mask()) !=
        hide1_required_operation_mask()) {
        path_put(&parent);
        put_nsproxy(nsproxy);
        put_task_struct(task);
        return -EOPNOTSUPP;
    }
    if (!igrab(inode)) {
        path_put(&parent);
        put_nsproxy(nsproxy);
        put_task_struct(task);
        return -ESTALE;
    }
    binding->rule = *rule;
    binding->parent_path = parent;
    binding->parent_inode = inode;
    binding->parent_sb = inode->i_sb;
    binding->target_task = task;
    binding->target_nsproxy = nsproxy;
    binding->target_mnt_ns = nsproxy->mnt_ns;
    binding->hidden_inode = NULL;
    binding->operation_mask = operation_mask;
    binding->shadow.orig_iop = inode->i_op;
    binding->shadow.orig_fop = inode->i_fop;
    binding->shadow.iop_meta = NULL;
    binding->shadow.fop_meta = NULL;
    binding->shadow.module_pin = false;
    binding->parent_dop = parent.dentry->d_op;
    INIT_LIST_HEAD(&binding->dentry_shadows);
    spin_lock_init(&binding->dentry_lock);
    spin_lock_init(&binding->identity_lock);
    binding->retiring = false;
    return 0;
}

static void hide1_commit_binding(struct hide1_binding *binding)
{
    const struct inode *inode = binding->parent_inode;
    const struct ns_common *mnt_ns = from_mnt_ns(binding->target_mnt_ns);

    hide1_release_binding(&hide1_binding);

    /* dentry_shadows and dentry_lock are address-bearing synchronization
     * objects.  Copying the whole prepared binding would leave the global
     * list head pointing at the temporary allocation and would copy a
     * spinlock.  Transfer only owned references and immutable snapshots. */
    hide1_binding.rule = binding->rule;
    hide1_binding.parent_path = binding->parent_path;
    hide1_binding.parent_inode = binding->parent_inode;
    hide1_binding.parent_sb = binding->parent_sb;
    hide1_binding.target_task = binding->target_task;
    hide1_binding.target_nsproxy = binding->target_nsproxy;
    hide1_binding.target_mnt_ns = binding->target_mnt_ns;
    hide1_binding.hidden_inode = binding->hidden_inode;
    hide1_binding.operation_mask = binding->operation_mask;
    hide1_binding.shadow = binding->shadow;
    hide1_binding.parent_dop = binding->parent_dop;
    INIT_LIST_HEAD(&hide1_binding.dentry_shadows);
    spin_lock_init(&hide1_binding.dentry_lock);
    spin_lock_init(&hide1_binding.identity_lock);
    hide1_binding.retiring = false;

    /* Ownership moved to hide1_binding.  The caller always releases the
     * prepared object, so it must no longer contain reference-bearing data. */
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
        cancel_delayed_work_sync(&hide1_dop_stale_work);
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
                hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_READY;
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
        if (hide1_status.state == PATHGUARD_HIDE1_STATE_ACTIVE) {
            hide1_status.last_error = -EALREADY;
            mutex_unlock(&hide1_lock);
            return -EALREADY;
        }
        if (hide1_status.state != PATHGUARD_HIDE1_STATE_INACTIVE ||
            hide1_lifecycle != PATHGUARD_HIDE1_LIFECYCLE_READY) {
            hide1_status.last_error = -EOPNOTSUPP;
            mutex_unlock(&hide1_lock);
            return -EOPNOTSUPP;
        }
        hide1_binding.retiring = false;
        ret = hide1_shadow_install_locked(&hide1_binding);
        if (ret) {
            /* Installation rollback has restored all published vectors. */
            hide1_binding.retiring = false;
            hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_READY;
            hide1_status.last_error = ret;
            hide1_status.state = PATHGUARD_HIDE1_STATE_INACTIVE;
            mutex_unlock(&hide1_lock);
            return ret;
        }
        hide1_binding.retiring = false;
        hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_RUNNING;
        hide1_status.state = PATHGUARD_HIDE1_STATE_ACTIVE;
        hide1_status.last_error = 0;
        mutex_unlock(&hide1_lock);
        return 0;

    case PATHGUARD_HIDE1_IOC_DISABLE:
        hide1_revoke_dead_target_locked();
        if (hide1_status.state == PATHGUARD_HIDE1_STATE_ACTIVE ||
            hide1_binding.shadow.iop_installed ||
            hide1_binding.shadow.hidden_iop_installed ||
            hide1_binding.shadow.fop_installed ||
            !list_empty(&hide1_binding.dentry_shadows)) {
            ret = hide1_shadow_uninstall_locked(&hide1_binding);
            /* Preflight failures leave every vector installed and the state
             * ACTIVE.  Only finalize READY after uninstall crossed
             * STOP_NEW and published INACTIVE itself. */
            if (hide1_status.state == PATHGUARD_HIDE1_STATE_INACTIVE) {
                hide1_binding.retiring = false;
                hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_READY;
            }
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
        hide1_revoke_dead_target_locked();
        status = hide1_status;
        status.lifecycle = hide1_lifecycle;
        status.lookup_calls = atomic64_read(&hide1_lookup_calls);
        status.lookup_hidden = atomic64_read(&hide1_lookup_hidden);
        status.atomic_open_calls = atomic64_read(&hide1_atomic_open_calls);
        status.atomic_open_hidden = atomic64_read(&hide1_atomic_open_hidden);
        status.readdir_calls = atomic64_read(&hide1_readdir_calls);
        status.readdir_filtered = atomic64_read(&hide1_readdir_filtered);
        status.d_revalidate_calls = atomic64_read(&hide1_d_revalidate_calls);
        status.d_revalidate_hidden = atomic64_read(&hide1_d_revalidate_hidden);
        status.dentry_install_calls = atomic64_read(&hide1_dentry_install_calls);
        status.dentry_install_success = atomic64_read(&hide1_dentry_install_success);
        status.dentry_install_failures = atomic64_read(&hide1_dentry_install_failures);
        status.iop_active = atomic_read(&hide1_iop_active);
        status.fop_active = atomic_read(&hide1_fop_active);
        status.dop_active = atomic_read(&hide1_dop_active);
        status.fop_open_count = hide1_binding.shadow.fop_meta ?
            atomic_read(&hide1_binding.shadow.fop_meta->open_count) : 0;
        status.mutation_calls = atomic64_read(&hide1_mutation_calls);
        status.mutation_blocked = atomic64_read(&hide1_mutation_blocked_calls);
        status.mutation_original = atomic64_read(&hide1_mutation_original);
        status.mutation_unsupported = atomic64_read(&hide1_mutation_unsupported);
        hide1_snapshot_mutation_counters(&status.mutation_atomic_open,
                                         HIDE1_MUTATION_ATOMIC_OPEN);
        hide1_snapshot_mutation_counters(&status.mutation_create,
                                         HIDE1_MUTATION_CREATE);
        hide1_snapshot_mutation_counters(&status.mutation_mkdir,
                                         HIDE1_MUTATION_MKDIR);
        hide1_snapshot_mutation_counters(&status.mutation_mknod,
                                         HIDE1_MUTATION_MKNOD);
        hide1_snapshot_mutation_counters(&status.mutation_symlink,
                                         HIDE1_MUTATION_SYMLINK);
        hide1_snapshot_mutation_counters(&status.mutation_unlink,
                                         HIDE1_MUTATION_UNLINK);
        hide1_snapshot_mutation_counters(&status.mutation_rmdir,
                                         HIDE1_MUTATION_RMDIR);
        hide1_snapshot_mutation_counters(&status.mutation_link,
                                         HIDE1_MUTATION_LINK);
        hide1_snapshot_mutation_counters(&status.mutation_rename,
                                         HIDE1_MUTATION_RENAME);
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
    if (hide1_shadow_mode < 0 || hide1_shadow_mode > 4)
        return -EINVAL;
    if (strcmp(init_utsname()->release, PATHGUARD_HIDE1_EXPECTED_RELEASE) != 0)
        return -ENODEV;
    INIT_LIST_HEAD(&hide1_binding.dentry_shadows);
    spin_lock_init(&hide1_binding.dentry_lock);
    spin_lock_init(&hide1_binding.identity_lock);
    hash_init(hide1_iop_table);
    hash_init(hide1_fop_table);
    hash_init(hide1_dop_table);
    INIT_DELAYED_WORK(&hide1_dop_stale_work, hide1_dop_stale_workfn);
    hide1_lifecycle = PATHGUARD_HIDE1_LIFECYCLE_READY;
    strscpy(hide1_status.kernel_release, init_utsname()->release,
            sizeof(hide1_status.kernel_release));
    hide1_status.last_error = -EOPNOTSUPP;
    return misc_register(&hide1_device);
}

static void __exit hide1_exit(void)
{
    misc_deregister(&hide1_device);
    cancel_delayed_work_sync(&hide1_dop_stale_work);
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
