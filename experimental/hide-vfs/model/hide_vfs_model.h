#pragma once

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 pg_hide1_u8;
typedef u16 pg_hide1_u16;
typedef u32 pg_hide1_u32;
typedef u64 pg_hide1_u64;
#else
#include <stdint.h>
typedef uint8_t pg_hide1_u8;
typedef uint16_t pg_hide1_u16;
typedef uint32_t pg_hide1_u32;
typedef uint64_t pg_hide1_u64;
#endif

#define PG_HIDE1_BASENAME_MAX 255U

#ifdef __cplusplus
extern "C" {
#endif

struct pg_hide1_parent_identity {
    pg_hide1_u64 superblock_cookie;
    pg_hide1_u64 inode;
};

struct pg_hide1_observer {
    pg_hide1_u32 uid;
    pg_hide1_u64 mount_namespace_cookie;
};

struct pg_hide1_rule_model {
    struct pg_hide1_parent_identity parent;
    struct pg_hide1_observer target;
    pg_hide1_u64 generation;
    pg_hide1_u16 basename_length;
    char basename[PG_HIDE1_BASENAME_MAX + 1U];
};

enum pg_hide1_operation {
    PG_HIDE1_LOOKUP,
    PG_HIDE1_ATOMIC_OPEN,
    PG_HIDE1_READDIR,
    PG_HIDE1_CREATE,
    PG_HIDE1_MKDIR,
    PG_HIDE1_MKNOD,
    PG_HIDE1_SYMLINK,
    PG_HIDE1_UNLINK,
    PG_HIDE1_RMDIR,
    PG_HIDE1_LINK,
    PG_HIDE1_RENAME,
};

enum pg_hide1_cache_kind {
    PG_HIDE1_REAL_POSITIVE,
    PG_HIDE1_REAL_NEGATIVE,
    PG_HIDE1_SYNTHETIC_NEGATIVE,
};

enum pg_hide1_outcome {
    PG_HIDE1_PASS,
    PG_HIDE1_HIDE_NEGATIVE,
    PG_HIDE1_OMIT,
    PG_HIDE1_REJECT,
    PG_HIDE1_KEEP_CACHE,
    PG_HIDE1_INVALIDATE_CACHE,
};

struct pg_hide1_decision {
    enum pg_hide1_outcome outcome;
    int error_number;
    pg_hide1_u8 call_original;
};

int pg_hide1_rule_set_basename(struct pg_hide1_rule_model *rule,
                               const char *basename,
                               pg_hide1_u16 basename_length);
int pg_hide1_rule_valid(const struct pg_hide1_rule_model *rule);
int pg_hide1_accepts_generation(const struct pg_hide1_rule_model *rule,
                                pg_hide1_u64 generation);

struct pg_hide1_decision pg_hide1_evaluate(
    const struct pg_hide1_rule_model *rule,
    enum pg_hide1_operation operation,
    const struct pg_hide1_observer *observer,
    struct pg_hide1_parent_identity parent,
    const char *basename,
    pg_hide1_u16 basename_length);

struct pg_hide1_decision pg_hide1_evaluate_cache(
    const struct pg_hide1_rule_model *rule,
    const struct pg_hide1_observer *observer,
    struct pg_hide1_parent_identity parent,
    const char *basename,
    pg_hide1_u16 basename_length,
    enum pg_hide1_cache_kind cache,
    pg_hide1_u64 cache_generation);

struct pg_hide1_decision pg_hide1_evaluate_rename(
    const struct pg_hide1_rule_model *rule,
    const struct pg_hide1_observer *observer,
    struct pg_hide1_parent_identity source_parent,
    const char *source_basename,
    pg_hide1_u16 source_basename_length,
    struct pg_hide1_parent_identity destination_parent,
    const char *destination_basename,
    pg_hide1_u16 destination_basename_length);

#ifdef __cplusplus
}
#endif
