#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pathguard::provenance {

enum class Error : std::uint8_t {
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

enum class Event : std::uint8_t {
    kPrepare = 1,
    kMaterialized = 2,
    kCommit = 3,
    kAbort = 4,
};

enum class Operation : std::uint8_t { kCreate = 1, kRename = 2, kDelete = 3 };
enum class IdentityKind : std::uint8_t { kFileHandle = 1, kStatxBirthTime = 2 };

struct TransactionId {
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    auto operator<=>(const TransactionId&) const = default;
    bool valid() const { return high != 0 || low != 0; }
};

struct RouteScope {
    std::int32_t caller_uid = -1;
    std::uint32_t user_id = 0;
    std::uint64_t identity_epoch = 0;
    std::string trusted_attribution;
    auto operator<=>(const RouteScope&) const = default;
};

struct ObjectIdentity {
    IdentityKind kind = IdentityKind::kFileHandle;
    std::string volume;
    std::vector<std::uint8_t> handle;
    std::uint64_t inode = 0;
    std::int64_t birth_seconds = 0;
    std::uint32_t birth_nanoseconds = 0;
    std::uint8_t object_type = 1;
    bool Strong() const;
    bool operator==(const ObjectIdentity&) const = default;
};

struct RouteKey {
    std::string storage_root_id;
    std::string target_relative_path;
    auto operator<=>(const RouteKey&) const = default;
};

struct RouteRecord {
    RouteKey key;
    RouteScope scope;
    ObjectIdentity identity;
    std::string logical_source_path;
    std::uint64_t rule_id = 0;
    std::uint64_t content_generation = 0;
    std::uint64_t created_plan_generation = 0;
    std::uint64_t bound_plan_generation = 0;
    std::uint64_t commit_sequence = 0;
    bool operator==(const RouteRecord&) const = default;
};

struct JournalFrame {
    std::uint64_t sequence = 0;
    TransactionId transaction;
    Event event = Event::kPrepare;
    Operation operation = Operation::kCreate;
    std::optional<RouteRecord> record;
    std::optional<RouteRecord> previous_record;
    bool operator==(const JournalFrame&) const = default;
};

class RouteJournal {
public:
    virtual ~RouteJournal() = default;
    virtual Error Append(const JournalFrame& frame) = 0;
    virtual Error Replay(std::vector<JournalFrame>* frames) = 0;
};

class MemoryRouteJournal final : public RouteJournal {
public:
    Error Append(const JournalFrame& frame) override;
    Error Replay(std::vector<JournalFrame>* frames) override;
    void FailNextAppend(Error error) { next_error_ = error; }
    const std::vector<JournalFrame>& frames() const { return frames_; }
private:
    Error next_error_ = Error::kNone;
    std::vector<JournalFrame> frames_;
};

class FileRouteJournal final : public RouteJournal {
public:
    explicit FileRouteJournal(std::string path) : path_(std::move(path)) {}
    Error Append(const JournalFrame& frame) override;
    Error Replay(std::vector<JournalFrame>* frames) override;
private:
    std::string path_;
};

enum class ResolveStatus : std::uint8_t { kNone, kUnique, kAmbiguous };
struct ResolveResult {
    ResolveStatus status = ResolveStatus::kNone;
    Error error = Error::kNone;
    std::optional<RouteRecord> record;
};

class RouteProvenanceStore final {
public:
    explicit RouteProvenanceStore(RouteJournal* journal) : journal_(journal) {}
    Error Recover();
    Error Prepare(TransactionId transaction, Operation operation,
                  const RouteRecord& candidate);
    Error PrepareRename(TransactionId transaction,
                        const RouteRecord& previous,
                        const RouteRecord& candidate);
    Error PrepareDelete(TransactionId transaction,
                        const RouteRecord& previous);
    Error Materialize(TransactionId transaction, const ObjectIdentity& identity);
    Error Commit(TransactionId transaction);
    Error Abort(TransactionId transaction);
    ResolveResult ResolveReverse(const RouteScope& scope, const RouteKey& key,
                                 const ObjectIdentity& identity,
                                 std::uint64_t current_plan_generation) const;
    std::size_t committed_count() const { return committed_.size(); }
    std::size_t pending_count() const { return pending_.size(); }
private:
    struct Pending {
        Operation operation = Operation::kCreate;
        RouteRecord record;
        std::optional<RouteRecord> previous_record;
        bool materialized = false;
    };
    Error Append(TransactionId transaction, Event event, Operation operation,
                 const std::optional<RouteRecord>& record,
                 const std::optional<RouteRecord>& previous_record = std::nullopt);
    Error Apply(const JournalFrame& frame, bool replay);
    RouteJournal* journal_ = nullptr;
    std::uint64_t next_sequence_ = 1;
    // A target path names one physical object. Scope identifies its owner; it
    // must not create a second ownership namespace for the same object.
    std::map<RouteKey, RouteRecord> committed_;
    std::map<TransactionId, Pending> pending_;
    std::map<TransactionId, Event> finalized_;
};

struct MutationResult {
    int error_number = 0;
    Error provenance_error = Error::kNone;
    bool committed = false;
    bool compensated = false;
};

class ProvenanceCoordinator final {
public:
    explicit ProvenanceCoordinator(RouteProvenanceStore* store) : store_(store) {}
    MutationResult Create(
        TransactionId transaction, const RouteRecord& candidate,
        const std::function<int()>& create_no_replace,
        const std::function<std::optional<ObjectIdentity>()>& identity,
        const std::function<bool(const ObjectIdentity&)>& compensate_unlink);
    MutationResult Rename(
        TransactionId transaction, const RouteRecord& previous,
        const RouteRecord& candidate,
        const std::function<int()>& rename_no_replace,
        const std::function<std::optional<ObjectIdentity>()>& new_identity,
        const std::function<bool(const ObjectIdentity&)>& compensate_rename);
    MutationResult Delete(
        TransactionId transaction, const RouteRecord& previous,
        const std::function<int()>& remove,
        const std::function<void()>& mark_degraded);
private:
    RouteProvenanceStore* store_ = nullptr;
};

}  // namespace pathguard::provenance
