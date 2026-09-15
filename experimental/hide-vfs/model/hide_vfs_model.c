#include "hide_vfs_model.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

static struct pg_hide1_decision pass_decision(void)
{
    const struct pg_hide1_decision decision = { PG_HIDE1_PASS, 0, 1 };
    return decision;
}

static struct pg_hide1_decision reject_decision(void)
{
    const struct pg_hide1_decision decision = { PG_HIDE1_REJECT, ENOENT, 0 };
    return decision;
}

static int bytes_equal(const char *left, const char *right,
                       pg_hide1_u16 length)
{
    pg_hide1_u16 index;

    if (!left || !right)
        return 0;
    for (index = 0; index < length; ++index) {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

static int observer_is_target(const struct pg_hide1_rule_model *rule,
                              const struct pg_hide1_observer *observer)
{
    return observer && observer->uid == rule->target.uid &&
           observer->mount_namespace_cookie ==
               rule->target.mount_namespace_cookie;
}

static int entry_matches(const struct pg_hide1_rule_model *rule,
                         struct pg_hide1_parent_identity parent,
                         const char *basename,
                         pg_hide1_u16 basename_length)
{
    return parent.superblock_cookie == rule->parent.superblock_cookie &&
           parent.inode == rule->parent.inode &&
           basename_length == rule->basename_length &&
           bytes_equal(basename, rule->basename, basename_length);
}

int pg_hide1_rule_set_basename(struct pg_hide1_rule_model *rule,
                               const char *basename,
                               pg_hide1_u16 basename_length)
{
    pg_hide1_u16 index;

    if (!rule || !basename || basename_length == 0 ||
        basename_length > PG_HIDE1_BASENAME_MAX)
        return 0;
    for (index = 0; index < basename_length; ++index) {
        if (basename[index] == '/' || basename[index] == '\0')
            return 0;
    }
    for (index = 0; index < basename_length; ++index)
        rule->basename[index] = basename[index];
    rule->basename[basename_length] = '\0';
    rule->basename_length = basename_length;
    return 1;
}

int pg_hide1_rule_valid(const struct pg_hide1_rule_model *rule)
{
    pg_hide1_u16 index;

    if (!rule || rule->parent.superblock_cookie == 0 ||
        rule->parent.inode == 0 || rule->target.uid < 10000 ||
        rule->target.mount_namespace_cookie == 0 || rule->generation == 0 ||
        rule->basename_length == 0 ||
        rule->basename_length > PG_HIDE1_BASENAME_MAX ||
        rule->basename[rule->basename_length] != '\0')
        return 0;
    for (index = 0; index < rule->basename_length; ++index) {
        if (rule->basename[index] == '/' || rule->basename[index] == '\0')
            return 0;
    }
    return 1;
}

int pg_hide1_accepts_generation(const struct pg_hide1_rule_model *rule,
                                pg_hide1_u64 generation)
{
    return rule && generation != 0 && generation == rule->generation;
}

struct pg_hide1_decision pg_hide1_evaluate(
    const struct pg_hide1_rule_model *rule,
    enum pg_hide1_operation operation,
    const struct pg_hide1_observer *observer,
    struct pg_hide1_parent_identity parent,
    const char *basename,
    pg_hide1_u16 basename_length)
{
    if (!pg_hide1_rule_valid(rule) || !observer_is_target(rule, observer) ||
        !entry_matches(rule, parent, basename, basename_length))
        return pass_decision();

    switch (operation) {
    case PG_HIDE1_LOOKUP: {
        const struct pg_hide1_decision decision = {
            PG_HIDE1_HIDE_NEGATIVE, ENOENT, 0
        };
        return decision;
    }
    case PG_HIDE1_READDIR: {
        const struct pg_hide1_decision decision = { PG_HIDE1_OMIT, 0, 0 };
        return decision;
    }
    case PG_HIDE1_ATOMIC_OPEN:
    case PG_HIDE1_CREATE:
    case PG_HIDE1_MKDIR:
    case PG_HIDE1_MKNOD:
    case PG_HIDE1_SYMLINK:
    case PG_HIDE1_UNLINK:
    case PG_HIDE1_RMDIR:
    case PG_HIDE1_LINK:
    case PG_HIDE1_RENAME:
        return reject_decision();
    default:
        return pass_decision();
    }
}

struct pg_hide1_decision pg_hide1_evaluate_cache(
    const struct pg_hide1_rule_model *rule,
    const struct pg_hide1_observer *observer,
    struct pg_hide1_parent_identity parent,
    const char *basename,
    pg_hide1_u16 basename_length,
    enum pg_hide1_cache_kind cache,
    pg_hide1_u64 cache_generation)
{
    const int synthetic = cache == PG_HIDE1_SYNTHETIC_NEGATIVE;
    const int governed = pg_hide1_rule_valid(rule) &&
        entry_matches(rule, parent, basename, basename_length);

    if (!governed || !observer_is_target(rule, observer)) {
        const struct pg_hide1_decision decision = synthetic
            ? (struct pg_hide1_decision){ PG_HIDE1_INVALIDATE_CACHE, 0, 0 }
            : (struct pg_hide1_decision){ PG_HIDE1_KEEP_CACHE, 0, 1 };
        return decision;
    }
    if (synthetic) {
        const struct pg_hide1_decision decision =
            cache_generation == rule->generation
                ? (struct pg_hide1_decision){ PG_HIDE1_KEEP_CACHE, 0, 0 }
                : (struct pg_hide1_decision){
                      PG_HIDE1_INVALIDATE_CACHE, 0, 0
                  };
        return decision;
    }
    if (cache == PG_HIDE1_REAL_NEGATIVE) {
        const struct pg_hide1_decision decision = {
            PG_HIDE1_KEEP_CACHE, 0, 0
        };
        return decision;
    }
    {
        const struct pg_hide1_decision decision = {
            PG_HIDE1_INVALIDATE_CACHE, 0, 0
        };
        return decision;
    }
}

struct pg_hide1_decision pg_hide1_evaluate_rename(
    const struct pg_hide1_rule_model *rule,
    const struct pg_hide1_observer *observer,
    struct pg_hide1_parent_identity source_parent,
    const char *source_basename,
    pg_hide1_u16 source_basename_length,
    struct pg_hide1_parent_identity destination_parent,
    const char *destination_basename,
    pg_hide1_u16 destination_basename_length)
{
    if (!pg_hide1_rule_valid(rule) || !observer_is_target(rule, observer))
        return pass_decision();
    if (entry_matches(rule, source_parent, source_basename,
                      source_basename_length) ||
        entry_matches(rule, destination_parent, destination_basename,
                      destination_basename_length))
        return reject_decision();
    return pass_decision();
}
