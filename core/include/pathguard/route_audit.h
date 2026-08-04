#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pathguard::audit {

enum class Error : std::uint8_t {
    kNone,
    kUnavailable,
    kCorrupt,
    kCommitFailed,
    kStoreLimitExceeded,
    kInvalidRecord,
};

enum class Operation : std::uint8_t {
    kUpsert = 1,
    kRename = 2,
    kDelete = 3,
};

enum class Confidence : std::uint8_t {
    kPathOnly = 1,
    kInodeMetadata = 2,
    kBirthTime = 3,
    kFileHandle = 4,
};

struct ObjectIdentity {
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::uint64_t size = 0;
    std::uint32_t mode = 0;
    std::int64_t modified_seconds = 0;
    std::uint32_t modified_nanoseconds = 0;
    std::int64_t changed_seconds = 0;
    std::uint32_t changed_nanoseconds = 0;
    bool has_birth_time = false;
    std::int64_t birth_seconds = 0;
    std::uint32_t birth_nanoseconds = 0;
    std::int32_t handle_type = 0;
    std::vector<std::uint8_t> handle;

    Confidence confidence() const noexcept;
    bool operator==(const ObjectIdentity&) const = default;
};

struct Record {
    Operation operation = Operation::kUpsert;
    Confidence confidence = Confidence::kPathOnly;
    std::int32_t caller_uid = -1;
    std::uint32_t user_id = 0;
    std::uint64_t rule_id = 0;
    std::uint64_t content_generation = 0;
    std::uint64_t plan_generation = 0;
    std::uint64_t observed_realtime_ns = 0;
    std::uint64_t observed_boottime_ns = 0;
    std::uint64_t sequence = 0;
    std::string logical_source_path;
    std::string target_path;
    std::string previous_target_path;
    ObjectIdentity identity;

    bool operator==(const Record&) const = default;
};

class Journal {
public:
    virtual ~Journal() = default;
    virtual Error Append(const Record& record) = 0;
    virtual Error Replay(std::vector<Record>* records) = 0;
};

class MemoryJournal final : public Journal {
public:
    Error Append(const Record& record) override;
    Error Replay(std::vector<Record>* records) override;
    void FailNextAppend(Error error) noexcept { next_error_ = error; }
    const std::vector<Record>& records() const noexcept { return records_; }
private:
    Error next_error_ = Error::kNone;
    std::vector<Record> records_;
};

class FileJournal final : public Journal {
public:
    explicit FileJournal(std::string path) : path_(std::move(path)) {}
    Error Append(const Record& record) override;
    Error Replay(std::vector<Record>* records) override;
private:
    std::string path_;
};

class Store final {
public:
    explicit Store(Journal* journal) : journal_(journal) {}
    Error Recover();
    Error Observe(Record record);
    std::size_t current_count() const noexcept { return current_.size(); }
    std::uint64_t generation() const noexcept {
        return next_sequence_ == 0 ? 0 : next_sequence_ - 1;
    }
    bool CurrentAt(std::size_t index, Record* output) const;
    std::optional<Record> Find(std::string_view target_path) const;
private:
    Error Apply(const Record& record);
    Journal* journal_ = nullptr;
    std::uint64_t next_sequence_ = 1;
    std::uint64_t recovered_through_sequence_ = 0;
    std::map<std::string, Record, std::less<>> current_;
};

}  // namespace pathguard::audit
