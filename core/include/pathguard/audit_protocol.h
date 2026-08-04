#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pathguard::audit_protocol {

constexpr uint32_t kMagic = UINT32_C(0x41554750);  // PGUA
constexpr uint16_t kVersion = 1;
constexpr uint32_t kQueueMagic = UINT32_C(0x51414750);  // PGAQ
constexpr uint16_t kQueueVersion = 1;
constexpr uint32_t kQueueCapacity = 64;
constexpr uint32_t kPathCapacity = 4096;
constexpr uint32_t kHandleCapacity = 128;
constexpr char kAndroidSocketPath[] =
    "/data/adb/modules/pathguard_next/run/audit.sock";

enum class Command : uint16_t {
    kObserve = 1,
    kSnapshotInfo = 2,
    kSnapshotRecord = 3,
};

enum class Operation : uint8_t {
    kUpsert = 1,
    kRename = 2,
    kDelete = 3,
};

enum class Confidence : uint8_t {
    kPathOnly = 1,
    kInodeMetadata = 2,
    kBirthTime = 3,
    kFileHandle = 4,
};

enum class Error : uint8_t {
    kNone,
    kUnavailable,
    kCorrupt,
    kCommitFailed,
    kStoreLimitExceeded,
    kInvalidRecord,
};

struct QueueHello {
    uint32_t magic = kQueueMagic;
    uint16_t version = kQueueVersion;
    uint16_t reserved = 0;
    int32_t pid = 0;
    uint32_t uid = 0;
    uint64_t process_start_time = 0;
};

struct Identity {
    uint64_t device = 0;
    uint64_t inode = 0;
    uint64_t size = 0;
    int64_t modified_seconds = 0;
    int64_t changed_seconds = 0;
    int64_t birth_seconds = 0;
    uint32_t mode = 0;
    uint32_t modified_nanoseconds = 0;
    uint32_t changed_nanoseconds = 0;
    uint32_t birth_nanoseconds = 0;
    int32_t handle_type = 0;
    uint16_t handle_size = 0;
    uint8_t has_birth_time = 0;
    uint8_t reserved = 0;
    uint8_t handle[kHandleCapacity]{};
};

struct Record {
    int32_t caller_uid = -1;
    uint32_t user_id = 0;
    uint64_t rule_id = 0;
    uint64_t content_generation = 0;
    uint64_t plan_generation = 0;
    uint64_t observed_realtime_ns = 0;
    uint64_t observed_boottime_ns = 0;
    uint64_t sequence = 0;
    Operation operation = Operation::kUpsert;
    Confidence confidence = Confidence::kPathOnly;
    uint8_t reserved[6]{};
    Identity identity{};
    char logical_source[kPathCapacity]{};
    char target_path[kPathCapacity]{};
    char previous_target_path[kPathCapacity]{};
};

struct Request {
    uint32_t magic = kMagic;
    uint16_t version = kVersion;
    Command command = Command::kObserve;
    uint32_t snapshot_index = 0;
    uint32_t reserved = 0;
    Record record{};
};

struct Response {
    uint32_t magic = kMagic;
    uint16_t version = kVersion;
    Error error = Error::kNone;
    uint8_t reserved = 0;
    uint64_t snapshot_generation = 0;
    uint32_t snapshot_count = 0;
    uint32_t snapshot_index = 0;
    Record record{};
};

struct QueueSlot {
    uint32_t ready = 0;
    uint32_t reserved = 0;
    Request request{};
};

struct SharedQueue {
    uint32_t magic = kQueueMagic;
    uint16_t version = kQueueVersion;
    uint16_t reserved = 0;
    int32_t pid = 0;
    uint32_t uid = 0;
    uint64_t process_start_time = 0;
    uint64_t head = 0;
    uint64_t tail = 0;
    uint64_t enqueued = 0;
    uint64_t dropped = 0;
    uint64_t delivered = 0;
    uint64_t failed = 0;
    QueueSlot slots[kQueueCapacity]{};
};

static_assert(sizeof(Identity) == 200);
static_assert(sizeof(QueueHello) == 24);
static_assert(sizeof(QueueSlot) == 12576);
static_assert(sizeof(Record) == 12552);
static_assert(sizeof(Request) == 12568);
static_assert(sizeof(Response) == 12576);
static_assert(offsetof(Request, record) == 16);
static_assert(offsetof(Response, record) == 24);

}  // namespace pathguard::audit_protocol
