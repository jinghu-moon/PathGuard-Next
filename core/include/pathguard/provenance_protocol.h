#pragma once

#include <stdint.h>

namespace pathguard::provenance_protocol {

constexpr uint32_t kMagic = UINT32_C(0x50565047);  // GPVP
constexpr uint16_t kVersion = 1;
constexpr uint32_t kPathCapacity = 1024;
constexpr uint32_t kVolumeCapacity = 64;
constexpr char kAndroidSocketPath[] =
    "/data/adb/modules/pathguard_next/run/provenance.sock";

enum class Command : uint16_t {
    kPrepareCreate = 1,
    kMaterialize = 2,
    kCommit = 3,
    kAbort = 4,
    kResolve = 5,
    kPrepareRename = 6,
    kPrepareDelete = 7,
};

enum class ResolveStatus : uint8_t { kNone, kUnique, kAmbiguous };
enum class Error : uint8_t {
    kNone,
    kUnavailable,
    kCorrupt,
    kCommitFailed,
    kRouteBusy,
    kIdentityMismatch,
    kPolicyStale,
    kStoreLimitExceeded,
    kTransactionConflict,
    kInvalidState,
};

struct Identity {
    uint64_t inode = 0;
    int64_t birth_seconds = 0;
    uint32_t birth_nanoseconds = 0;
    uint8_t object_type = 1;
    uint8_t reserved[3]{};
    char volume[kVolumeCapacity]{};
};

struct Record {
    int32_t caller_uid = -1;
    uint32_t user_id = 0;
    uint64_t identity_epoch = 0;
    uint64_t rule_id = 0;
    uint64_t content_generation = 0;
    uint64_t created_plan_generation = 0;
    uint64_t bound_plan_generation = 0;
    Identity identity{};
    char storage_root[kVolumeCapacity]{};
    char target_relative[kPathCapacity]{};
    char logical_source[kPathCapacity]{};
};

struct Request {
    uint32_t magic = kMagic;
    uint16_t version = kVersion;
    Command command = Command::kPrepareCreate;
    uint64_t transaction_high = 0;
    uint64_t transaction_low = 0;
    Record record{};
    Record previous{};
};

struct Response {
    uint32_t magic = kMagic;
    uint16_t version = kVersion;
    Error provenance_error = Error::kNone;
    ResolveStatus resolve_status = ResolveStatus::kNone;
    char logical_source[kPathCapacity]{};
};

}  // namespace pathguard::provenance_protocol
