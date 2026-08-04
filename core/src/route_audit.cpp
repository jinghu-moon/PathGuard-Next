#include "pathguard/route_audit.h"

#include <cerrno>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "pathguard/policy_format.h"

namespace pathguard::audit {
namespace {

constexpr std::uint32_t kMagic = UINT32_C(0x41414750);  // PGAA
constexpr std::uint16_t kFormat = 1;
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kMaxPayload = 16 * 1024;
constexpr std::size_t kMaxRecords = 200000;
constexpr std::uint64_t kMaxJournalBytes = 64 * 1024 * 1024;

void Put16(std::vector<std::uint8_t>* out, std::uint16_t value) {
    out->push_back(static_cast<std::uint8_t>(value));
    out->push_back(static_cast<std::uint8_t>(value >> 8));
}

void Put32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out->push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void Put64(std::vector<std::uint8_t>* out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out->push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void Set32(std::vector<std::uint8_t>* out, std::size_t offset,
           std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        (*out)[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::uint16_t Read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | data[1] << 8);
}

std::uint32_t Read32(const std::uint8_t* data) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(data[i]) << (8 * i);
    }
    return value;
}

std::uint64_t Read64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

bool ValidPath(const std::string& value) {
    if (value.empty() || value.front() != '/' || value.size() > 4095
        || value.find('\0') != std::string::npos) {
        return false;
    }
    std::size_t begin = 1;
    while (begin < value.size()) {
        const std::size_t end = value.find('/', begin);
        const std::size_t size = (end == std::string::npos
            ? value.size() : end) - begin;
        if (size == 0 || size > 255
            || (size == 1 && value[begin] == '.')
            || (size == 2 && value[begin] == '.'
                && value[begin + 1] == '.')) {
            return false;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return begin < value.size();
}

bool ValidRecord(const Record& record) {
    const auto operation = static_cast<std::uint8_t>(record.operation);
    const auto confidence = static_cast<std::uint8_t>(record.confidence);
    return operation >= 1 && operation <= 3
        && confidence >= 1 && confidence <= 4
        && record.caller_uid >= 10000 && record.rule_id != 0
        && record.observed_realtime_ns != 0
        && ValidPath(record.logical_source_path)
        && ValidPath(record.target_path)
        && (record.operation != Operation::kRename
            || ValidPath(record.previous_target_path))
        && (record.operation == Operation::kRename
            || record.previous_target_path.empty())
        && record.identity.handle.size() <= 128
        && record.identity.modified_nanoseconds < 1000000000
        && record.identity.changed_nanoseconds < 1000000000
        && record.identity.birth_nanoseconds < 1000000000
        && record.confidence == record.identity.confidence();
}

bool IsOlderThan(const Record& candidate, const Record& current,
                 std::uint64_t recovered_through_sequence) {
    if (current.sequence <= recovered_through_sequence
        && candidate.sequence > recovered_through_sequence) {
        return false;
    }
    if (candidate.observed_boottime_ns != 0
        && current.observed_boottime_ns != 0
        && candidate.observed_boottime_ns
            != current.observed_boottime_ns) {
        return candidate.observed_boottime_ns < current.observed_boottime_ns;
    }
    return candidate.observed_realtime_ns < current.observed_realtime_ns;
}

bool PutBytes(std::vector<std::uint8_t>* out, const std::uint8_t* data,
              std::size_t size) {
    if (size > 4095 || out->size() + 4 + size > kMaxPayload) return false;
    Put32(out, static_cast<std::uint32_t>(size));
    if (size != 0) out->insert(out->end(), data, data + size);
    return true;
}

bool PutString(std::vector<std::uint8_t>* out, const std::string& value) {
    return PutBytes(out, reinterpret_cast<const std::uint8_t*>(value.data()),
                    value.size());
}

class Reader final {
public:
    Reader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}
    bool U8(std::uint8_t* out) {
        if (offset_ >= size_) return false;
        *out = data_[offset_++];
        return true;
    }
    bool U32(std::uint32_t* out) {
        if (size_ - offset_ < 4) return false;
        *out = Read32(data_ + offset_);
        offset_ += 4;
        return true;
    }
    bool I32(std::int32_t* out) {
        std::uint32_t value = 0;
        if (!U32(&value)) return false;
        std::memcpy(out, &value, sizeof(value));
        return true;
    }
    bool U64(std::uint64_t* out) {
        if (size_ - offset_ < 8) return false;
        *out = Read64(data_ + offset_);
        offset_ += 8;
        return true;
    }
    bool I64(std::int64_t* out) {
        std::uint64_t value = 0;
        if (!U64(&value)) return false;
        std::memcpy(out, &value, sizeof(value));
        return true;
    }
    bool String(std::string* out) {
        std::uint32_t size = 0;
        if (!U32(&size) || size > 4095 || size_ - offset_ < size) return false;
        out->assign(reinterpret_cast<const char*>(data_ + offset_), size);
        offset_ += size;
        return out->find('\0') == std::string::npos;
    }
    bool Bytes(std::vector<std::uint8_t>* out) {
        std::uint32_t size = 0;
        if (!U32(&size) || size > 128 || size_ - offset_ < size) return false;
        out->assign(data_ + offset_, data_ + offset_ + size);
        offset_ += size;
        return true;
    }
    bool done() const noexcept { return offset_ == size_; }
private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_ = 0;
};

std::vector<std::uint8_t> Encode(const Record& record) {
    if (!ValidRecord(record) || record.sequence == 0) return {};
    std::vector<std::uint8_t> payload;
    Put32(&payload, static_cast<std::uint32_t>(record.caller_uid));
    Put32(&payload, record.user_id);
    Put64(&payload, record.rule_id);
    Put64(&payload, record.content_generation);
    Put64(&payload, record.plan_generation);
    Put64(&payload, record.observed_realtime_ns);
    Put64(&payload, record.observed_boottime_ns);
    Put64(&payload, record.identity.device);
    Put64(&payload, record.identity.inode);
    Put64(&payload, record.identity.size);
    Put32(&payload, record.identity.mode);
    Put64(&payload, static_cast<std::uint64_t>(record.identity.modified_seconds));
    Put32(&payload, record.identity.modified_nanoseconds);
    Put64(&payload, static_cast<std::uint64_t>(record.identity.changed_seconds));
    Put32(&payload, record.identity.changed_nanoseconds);
    payload.push_back(record.identity.has_birth_time ? 1 : 0);
    payload.insert(payload.end(), 3, 0);
    Put64(&payload, static_cast<std::uint64_t>(record.identity.birth_seconds));
    Put32(&payload, record.identity.birth_nanoseconds);
    Put32(&payload, static_cast<std::uint32_t>(record.identity.handle_type));
    if (!PutBytes(&payload, record.identity.handle.data(),
                  record.identity.handle.size())
        || !PutString(&payload, record.logical_source_path)
        || !PutString(&payload, record.target_path)
        || !PutString(&payload, record.previous_target_path)) {
        return {};
    }
    std::vector<std::uint8_t> frame(kHeaderSize, 0);
    Set32(&frame, 0, kMagic);
    frame[4] = static_cast<std::uint8_t>(kFormat);
    frame[6] = static_cast<std::uint8_t>(kHeaderSize);
    Set32(&frame, 8, static_cast<std::uint32_t>(kHeaderSize + payload.size()));
    for (int i = 0; i < 8; ++i) {
        frame[16 + i] = static_cast<std::uint8_t>(record.sequence >> (8 * i));
    }
    frame[24] = static_cast<std::uint8_t>(record.operation);
    frame[25] = static_cast<std::uint8_t>(record.confidence);
    frame.insert(frame.end(), payload.begin(), payload.end());
    Set32(&frame, 12, binary_format::Crc32(frame.data(), frame.size()));
    return frame;
}

bool Decode(std::vector<std::uint8_t> bytes, Record* record) {
    if (record == nullptr || bytes.size() < kHeaderSize
        || Read32(bytes.data()) != kMagic || Read16(bytes.data() + 4) != kFormat
        || Read16(bytes.data() + 6) != kHeaderSize
        || Read32(bytes.data() + 8) != bytes.size()) {
        return false;
    }
    const std::uint32_t expected = Read32(bytes.data() + 12);
    Set32(&bytes, 12, 0);
    if (binary_format::Crc32(bytes.data(), bytes.size()) != expected) return false;
    for (std::size_t i = 26; i < kHeaderSize; ++i) {
        if (bytes[i] != 0) return false;
    }
    *record = {};
    record->sequence = Read64(bytes.data() + 16);
    record->operation = static_cast<Operation>(bytes[24]);
    record->confidence = static_cast<Confidence>(bytes[25]);
    Reader reader(bytes.data() + kHeaderSize, bytes.size() - kHeaderSize);
    std::uint32_t caller_uid = 0;
    std::uint8_t has_birth = 0;
    std::uint8_t reserved = 0;
    if (!reader.U32(&caller_uid) || !reader.U32(&record->user_id)
        || !reader.U64(&record->rule_id)
        || !reader.U64(&record->content_generation)
        || !reader.U64(&record->plan_generation)
        || !reader.U64(&record->observed_realtime_ns)
        || !reader.U64(&record->observed_boottime_ns)
        || !reader.U64(&record->identity.device)
        || !reader.U64(&record->identity.inode)
        || !reader.U64(&record->identity.size)
        || !reader.U32(&record->identity.mode)
        || !reader.I64(&record->identity.modified_seconds)
        || !reader.U32(&record->identity.modified_nanoseconds)
        || !reader.I64(&record->identity.changed_seconds)
        || !reader.U32(&record->identity.changed_nanoseconds)
        || !reader.U8(&has_birth)
        || !reader.U8(&reserved) || reserved != 0
        || !reader.U8(&reserved) || reserved != 0
        || !reader.U8(&reserved) || reserved != 0
        || !reader.I64(&record->identity.birth_seconds)
        || !reader.U32(&record->identity.birth_nanoseconds)
        || !reader.I32(&record->identity.handle_type)
        || !reader.Bytes(&record->identity.handle)
        || !reader.String(&record->logical_source_path)
        || !reader.String(&record->target_path)
        || !reader.String(&record->previous_target_path)
        || !reader.done() || has_birth > 1) {
        return false;
    }
    record->caller_uid = static_cast<std::int32_t>(caller_uid);
    record->identity.has_birth_time = has_birth != 0;
    return record->sequence != 0 && ValidRecord(*record);
}

bool SyncFile(std::FILE* file) {
    if (std::fflush(file) != 0) return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

bool TruncateFile(std::FILE* file, std::uint64_t size) {
#if defined(_WIN32)
    return _chsize_s(_fileno(file), size) == 0 && SyncFile(file);
#else
    return ftruncate(fileno(file), static_cast<off_t>(size)) == 0
        && SyncFile(file);
#endif
}

}  // namespace

Confidence ObjectIdentity::confidence() const noexcept {
    if (!handle.empty()) return Confidence::kFileHandle;
    if (device != 0 && inode != 0 && has_birth_time) {
        return Confidence::kBirthTime;
    }
    if (device != 0 && inode != 0) return Confidence::kInodeMetadata;
    return Confidence::kPathOnly;
}

Error MemoryJournal::Append(const Record& record) {
    if (next_error_ != Error::kNone) {
        const Error result = next_error_;
        next_error_ = Error::kNone;
        return result;
    }
    records_.push_back(record);
    return Error::kNone;
}

Error MemoryJournal::Replay(std::vector<Record>* records) {
    if (records == nullptr) return Error::kCorrupt;
    *records = records_;
    return Error::kNone;
}

Error FileJournal::Append(const Record& record) {
    const std::vector<std::uint8_t> frame = Encode(record);
    if (frame.empty()) return Error::kInvalidRecord;
    std::FILE* file = std::fopen(path_.c_str(), "ab+");
    if (file == nullptr) return Error::kUnavailable;
#if !defined(_WIN32)
    fchmod(fileno(file), 0600);
#endif
    const bool positioned = std::fseek(file, 0, SEEK_END) == 0;
    const long position = positioned ? std::ftell(file) : -1;
    if (position < 0
        || static_cast<std::uint64_t>(position) + frame.size()
            > kMaxJournalBytes) {
        std::fclose(file);
        return position < 0 ? Error::kCommitFailed
                            : Error::kStoreLimitExceeded;
    }
    const bool ok = std::fwrite(frame.data(), 1, frame.size(), file)
            == frame.size()
        && SyncFile(file);
    std::fclose(file);
    return ok ? Error::kNone : Error::kCommitFailed;
}

Error FileJournal::Replay(std::vector<Record>* records) {
    if (records == nullptr) return Error::kCorrupt;
    records->clear();
    std::FILE* file = std::fopen(path_.c_str(), "rb+");
    if (file == nullptr) return errno == ENOENT ? Error::kNone
                                                : Error::kUnavailable;
#if !defined(_WIN32)
    fchmod(fileno(file), 0600);
#endif
    std::uint64_t valid_size = 0;
    while (true) {
        std::uint8_t header[kHeaderSize]{};
        const std::size_t header_read = std::fread(header, 1, sizeof(header), file);
        if (header_read == 0 && std::feof(file)) break;
        if (header_read != sizeof(header)) {
            const bool repaired = TruncateFile(file, valid_size);
            std::fclose(file);
            return repaired ? Error::kNone : Error::kCommitFailed;
        }
        const std::uint32_t size = Read32(header + 8);
        if (Read32(header) != kMagic || size < kHeaderSize
            || size > kHeaderSize + kMaxPayload) {
            std::fclose(file);
            return Error::kCorrupt;
        }
        std::vector<std::uint8_t> frame(size);
        std::memcpy(frame.data(), header, sizeof(header));
        const std::size_t tail = size - sizeof(header);
        if (std::fread(frame.data() + sizeof(header), 1, tail, file) != tail) {
            const bool repaired = TruncateFile(file, valid_size);
            std::fclose(file);
            return repaired ? Error::kNone : Error::kCommitFailed;
        }
        Record record;
        if (!Decode(std::move(frame), &record)) {
            std::fclose(file);
            return Error::kCorrupt;
        }
        records->push_back(std::move(record));
        valid_size += size;
    }
    std::fclose(file);
    return Error::kNone;
}

Error Store::Apply(const Record& record) {
    if (!ValidRecord(record) || record.sequence == 0) {
        return Error::kInvalidRecord;
    }
    if (record.operation == Operation::kDelete) {
        const auto current = current_.find(record.target_path);
        if (current == current_.end()
            || !IsOlderThan(record, current->second,
                            recovered_through_sequence_)) {
            current_.erase(record.target_path);
        }
        return Error::kNone;
    }
    if (record.operation == Operation::kRename) {
        const auto previous = current_.find(record.previous_target_path);
        if (previous == current_.end()
            || !IsOlderThan(record, previous->second,
                            recovered_through_sequence_)) {
            current_.erase(record.previous_target_path);
        }
    }
    if (!current_.contains(record.target_path)
        && current_.size() >= kMaxRecords) {
        return Error::kStoreLimitExceeded;
    }
    const auto current = current_.find(record.target_path);
    if (current == current_.end()
        || !IsOlderThan(record, current->second,
                        recovered_through_sequence_)) {
        current_[record.target_path] = record;
    }
    return Error::kNone;
}

Error Store::Recover() {
    if (journal_ == nullptr) return Error::kUnavailable;
    std::vector<Record> records;
    const Error replayed = journal_->Replay(&records);
    if (replayed != Error::kNone) return replayed;
    current_.clear();
    next_sequence_ = 1;
    recovered_through_sequence_ = 0;
    for (const Record& record : records) {
        if (record.sequence != next_sequence_) return Error::kCorrupt;
        const Error applied = Apply(record);
        if (applied != Error::kNone) return applied;
        ++next_sequence_;
    }
    recovered_through_sequence_ = next_sequence_ - 1;
    return Error::kNone;
}

Error Store::Observe(Record record) {
    if (journal_ == nullptr) return Error::kUnavailable;
    record.sequence = next_sequence_;
    if (!ValidRecord(record)) return Error::kInvalidRecord;
    if (record.operation != Operation::kDelete
        && !current_.contains(record.target_path)
        && current_.size() >= kMaxRecords
        && (record.operation != Operation::kRename
            || !current_.contains(record.previous_target_path))) {
        return Error::kStoreLimitExceeded;
    }
    const Error appended = journal_->Append(record);
    if (appended != Error::kNone) return appended;
    const Error applied = Apply(record);
    if (applied != Error::kNone) return applied;
    ++next_sequence_;
    return Error::kNone;
}

bool Store::CurrentAt(std::size_t index, Record* output) const {
    if (output == nullptr || index >= current_.size()) return false;
    auto it = current_.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(index));
    *output = it->second;
    return true;
}

std::optional<Record> Store::Find(std::string_view target_path) const {
    const auto found = current_.find(target_path);
    return found == current_.end() ? std::nullopt
                                   : std::optional<Record>(found->second);
}

}  // namespace pathguard::audit
