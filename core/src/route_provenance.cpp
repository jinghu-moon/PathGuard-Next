#include "pathguard/route_provenance.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "pathguard/policy_format.h"

namespace pathguard::provenance {
namespace {

constexpr std::uint32_t kMagic = UINT32_C(0x52504750);  // PGPR
constexpr std::uint16_t kFormat = 1;
constexpr std::size_t kHeaderSize = 48;
constexpr std::size_t kMaxPayload = 16 * 1024;
constexpr std::size_t kMaxPending = 1024;
constexpr std::size_t kMaxRecords = 200000;

void Put16(std::vector<std::uint8_t>* out, std::uint16_t value) {
    out->push_back(static_cast<std::uint8_t>(value));
    out->push_back(static_cast<std::uint8_t>(value >> 8));
}
void Put32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out->push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void Put64(std::vector<std::uint8_t>* out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out->push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void Set32(std::vector<std::uint8_t>* out, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) (*out)[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
bool PutBytes(std::vector<std::uint8_t>* out, const std::uint8_t* data, std::size_t size) {
    if (size > 4096 || out->size() + 4 + size > kMaxPayload) return false;
    Put32(out, static_cast<std::uint32_t>(size));
    out->insert(out->end(), data, data + size);
    return true;
}
bool PutString(std::vector<std::uint8_t>* out, const std::string& value) {
    return PutBytes(out, reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
}
std::uint16_t Read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | data[1] << 8);
}
std::uint32_t Read32(const std::uint8_t* data) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[i]) << (8 * i);
    return value;
}
std::uint64_t Read64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(data[i]) << (8 * i);
    return value;
}

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    bool U8(std::uint8_t* out) { if (offset_ >= size_) return false; *out = data_[offset_++]; return true; }
    bool U32(std::uint32_t* out) { if (size_ - offset_ < 4) return false; *out = Read32(data_ + offset_); offset_ += 4; return true; }
    bool U64(std::uint64_t* out) { if (size_ - offset_ < 8) return false; *out = Read64(data_ + offset_); offset_ += 8; return true; }
    bool I64(std::int64_t* out) { std::uint64_t value; if (!U64(&value)) return false; std::memcpy(out, &value, 8); return true; }
    bool String(std::string* out) { std::uint32_t n; if (!U32(&n) || n > 4096 || size_ - offset_ < n) return false; out->assign(reinterpret_cast<const char*>(data_ + offset_), n); offset_ += n; return out->find('\0') == std::string::npos; }
    bool Bytes(std::vector<std::uint8_t>* out) { std::uint32_t n; if (!U32(&n) || n > 4096 || size_ - offset_ < n) return false; out->assign(data_ + offset_, data_ + offset_ + n); offset_ += n; return true; }
    bool done() const { return offset_ == size_; }
private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_ = 0;
};

bool ValidRelative(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.size() > 4095) return false;
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t end = value.find('/', start);
        const std::size_t count = (end == std::string::npos ? value.size() : end) - start;
        if (count == 0 || count > 255 || (count == 1 && value[start] == '.')
            || (count == 2 && value[start] == '.' && value[start + 1] == '.')) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool EncodeRecord(const RouteRecord& record, std::vector<std::uint8_t>* out) {
    if (!ValidRelative(record.key.target_relative_path)
        || !ValidRelative(record.logical_source_path)
        || (!record.identity.Strong()
            && (!record.identity.volume.empty() || !record.identity.handle.empty()
                || record.identity.inode != 0 || record.identity.birth_seconds != 0
                || record.identity.birth_nanoseconds != 0))
        || record.scope.caller_uid < 0 || record.rule_id == 0) return false;
    Put32(out, static_cast<std::uint32_t>(record.scope.caller_uid));
    Put32(out, record.scope.user_id); Put64(out, record.scope.identity_epoch);
    if (!PutString(out, record.scope.trusted_attribution)
        || !PutString(out, record.key.storage_root_id)
        || !PutString(out, record.key.target_relative_path)
        || !PutString(out, record.logical_source_path)) return false;
    out->push_back(static_cast<std::uint8_t>(record.identity.kind));
    out->push_back(record.identity.object_type); Put16(out, 0);
    if (!PutString(out, record.identity.volume)
        || !PutBytes(out, record.identity.handle.data(), record.identity.handle.size())) return false;
    Put64(out, record.identity.inode);
    std::uint64_t birth = 0; std::memcpy(&birth, &record.identity.birth_seconds, 8);
    Put64(out, birth); Put32(out, record.identity.birth_nanoseconds);
    Put64(out, record.rule_id); Put64(out, record.content_generation);
    Put64(out, record.created_plan_generation); Put64(out, record.bound_plan_generation);
    Put64(out, record.commit_sequence);
    return out->size() <= kMaxPayload;
}

bool DecodeRecord(const std::uint8_t* data, std::size_t size, RouteRecord* record) {
    Reader reader(data, size); std::uint32_t uid = 0; std::uint8_t kind = 0;
    std::uint8_t object_type = 0; std::uint32_t reserved = 0;
    if (!reader.U32(&uid) || !reader.U32(&record->scope.user_id)
        || !reader.U64(&record->scope.identity_epoch)
        || !reader.String(&record->scope.trusted_attribution)
        || !reader.String(&record->key.storage_root_id)
        || !reader.String(&record->key.target_relative_path)
        || !reader.String(&record->logical_source_path)
        || !reader.U8(&kind) || !reader.U8(&object_type)) return false;
    std::uint8_t r0 = 0, r1 = 0;
    if (!reader.U8(&r0) || !reader.U8(&r1) || r0 != 0 || r1 != 0
        || !reader.String(&record->identity.volume)
        || !reader.Bytes(&record->identity.handle)
        || !reader.U64(&record->identity.inode)
        || !reader.I64(&record->identity.birth_seconds)
        || !reader.U32(&record->identity.birth_nanoseconds)
        || !reader.U64(&record->rule_id)
        || !reader.U64(&record->content_generation)
        || !reader.U64(&record->created_plan_generation)
        || !reader.U64(&record->bound_plan_generation)
        || !reader.U64(&record->commit_sequence) || !reader.done()) return false;
    record->scope.caller_uid = static_cast<std::int32_t>(uid);
    record->identity.kind = static_cast<IdentityKind>(kind);
    record->identity.object_type = object_type;
    const bool empty_identity = record->identity.volume.empty()
        && record->identity.handle.empty() && record->identity.inode == 0
        && record->identity.birth_seconds == 0
        && record->identity.birth_nanoseconds == 0;
    return (kind == 1 || kind == 2) && ValidRelative(record->key.target_relative_path)
        && ValidRelative(record->logical_source_path)
        && (record->identity.Strong() || empty_identity);
}

std::vector<std::uint8_t> EncodeFrame(const JournalFrame& frame) {
    std::vector<std::uint8_t> payload;
    payload.resize(8, 0);
    payload[0] = frame.record.has_value() ? 1 : 0;
    payload[1] = frame.previous_record.has_value() ? 1 : 0;
    const auto append_record = [&](const std::optional<RouteRecord>& record) {
        if (!record) return true;
        std::vector<std::uint8_t> encoded;
        if (!EncodeRecord(*record, &encoded)) return false;
        Put32(&payload, static_cast<std::uint32_t>(encoded.size()));
        payload.insert(payload.end(), encoded.begin(), encoded.end());
        return payload.size() <= kMaxPayload;
    };
    if (!append_record(frame.record) || !append_record(frame.previous_record)) return {};
    std::vector<std::uint8_t> out(kHeaderSize, 0);
    Set32(&out, 0, kMagic); out[4] = static_cast<std::uint8_t>(kFormat);
    out[6] = static_cast<std::uint8_t>(kHeaderSize);
    Set32(&out, 8, static_cast<std::uint32_t>(kHeaderSize + payload.size()));
    for (int i = 0; i < 8; ++i) out[16 + i] = static_cast<std::uint8_t>(frame.sequence >> (8 * i));
    for (int i = 0; i < 8; ++i) out[24 + i] = static_cast<std::uint8_t>(frame.transaction.high >> (8 * i));
    for (int i = 0; i < 8; ++i) out[32 + i] = static_cast<std::uint8_t>(frame.transaction.low >> (8 * i));
    out[40] = static_cast<std::uint8_t>(frame.event); out[41] = static_cast<std::uint8_t>(frame.operation);
    Set32(&out, 44, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    Set32(&out, 12, binary_format::Crc32(out.data(), out.size()));
    return out;
}

bool DecodeFrame(std::vector<std::uint8_t> bytes, JournalFrame* frame) {
    if (bytes.size() < kHeaderSize || Read32(bytes.data()) != kMagic
        || Read16(bytes.data() + 4) != kFormat || Read16(bytes.data() + 6) != kHeaderSize
        || Read32(bytes.data() + 8) != bytes.size()
        || Read32(bytes.data() + 44) != bytes.size() - kHeaderSize) return false;
    const std::uint32_t expected = Read32(bytes.data() + 12); Set32(&bytes, 12, 0);
    if (binary_format::Crc32(bytes.data(), bytes.size()) != expected) return false;
    frame->sequence = Read64(bytes.data() + 16); frame->transaction.high = Read64(bytes.data() + 24);
    frame->transaction.low = Read64(bytes.data() + 32); frame->event = static_cast<Event>(bytes[40]);
    frame->operation = static_cast<Operation>(bytes[41]);
    if (!frame->transaction.valid() || bytes[42] != 0 || bytes[43] != 0
        || bytes[40] < 1 || bytes[40] > 4 || bytes[41] < 1 || bytes[41] > 3) return false;
    const std::uint8_t* payload = bytes.data() + kHeaderSize;
    const std::size_t payload_size = bytes.size() - kHeaderSize;
    if (payload_size < 8 || payload[0] > 1 || payload[1] > 1) return false;
    for (std::size_t i = 2; i < 8; ++i) if (payload[i] != 0) return false;
    std::size_t offset = 8;
    auto decode_record = [&](bool present, std::optional<RouteRecord>* output) {
        if (!present) return true;
        if (payload_size - offset < 4) return false;
        const std::uint32_t size = Read32(payload + offset); offset += 4;
        if (size == 0 || size > kMaxPayload || payload_size - offset < size) return false;
        RouteRecord record;
        if (!DecodeRecord(payload + offset, size, &record)) return false;
        offset += size; *output = std::move(record); return true;
    };
    if (!decode_record(payload[0] != 0, &frame->record)
        || !decode_record(payload[1] != 0, &frame->previous_record)
        || offset != payload_size) return false;
    return true;
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

bool ObjectIdentity::Strong() const {
    if (volume.empty() || object_type == 0) return false;
    if (kind == IdentityKind::kFileHandle) return !handle.empty() && handle.size() <= 128;
    return kind == IdentityKind::kStatxBirthTime && inode != 0
        && birth_nanoseconds < 1000000000;
}

Error MemoryRouteJournal::Append(const JournalFrame& frame) {
    if (next_error_ != Error::kNone) { const Error result = next_error_; next_error_ = Error::kNone; return result; }
    frames_.push_back(frame); return Error::kNone;
}
Error MemoryRouteJournal::Replay(std::vector<JournalFrame>* frames) { if (!frames) return Error::kCorrupt; *frames = frames_; return Error::kNone; }

Error FileRouteJournal::Append(const JournalFrame& frame) {
    const auto bytes = EncodeFrame(frame); if (bytes.empty()) return Error::kCorrupt;
    std::FILE* file = std::fopen(path_.c_str(), "ab"); if (!file) return Error::kUnavailable;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size() && SyncFile(file);
    std::fclose(file); return ok ? Error::kNone : Error::kCommitFailed;
}

Error FileRouteJournal::Replay(std::vector<JournalFrame>* frames) {
    if (!frames) return Error::kCorrupt; frames->clear();
    std::FILE* file = std::fopen(path_.c_str(), "rb+"); if (!file) return errno == ENOENT ? Error::kNone : Error::kUnavailable;
    std::uint64_t expected = 1;
    std::uint64_t valid_size = 0;
    for (;;) {
        std::uint8_t header[kHeaderSize]; const std::size_t got = std::fread(header, 1, sizeof(header), file);
        if (got == 0 && std::feof(file)) break;
        if (got != sizeof(header)) {
            const bool repaired = std::feof(file) && TruncateFile(file, valid_size);
            std::fclose(file);
            return repaired ? Error::kNone : Error::kCorrupt;
        }
        const std::uint32_t size = Read32(header + 8);
        if (size < kHeaderSize || size > kHeaderSize + kMaxPayload) { std::fclose(file); return Error::kCorrupt; }
        std::vector<std::uint8_t> bytes(header, header + kHeaderSize); bytes.resize(size);
        if (std::fread(bytes.data() + kHeaderSize, 1, size - kHeaderSize, file)
            != size - kHeaderSize) {
            const bool repaired = std::feof(file) && TruncateFile(file, valid_size);
            std::fclose(file);
            return repaired ? Error::kNone : Error::kCorrupt;
        }
        JournalFrame frame; if (!DecodeFrame(std::move(bytes), &frame) || frame.sequence != expected++) { std::fclose(file); return Error::kCorrupt; }
        frames->push_back(std::move(frame));
        valid_size += size;
    }
    std::fclose(file); return Error::kNone;
}

Error RouteProvenanceStore::Append(TransactionId transaction, Event event,
                                   Operation operation, const std::optional<RouteRecord>& record,
                                   const std::optional<RouteRecord>& previous_record) {
    if (!journal_) return Error::kUnavailable;
    JournalFrame frame{next_sequence_, transaction, event, operation, record, previous_record};
    const Error result = journal_->Append(frame); if (result != Error::kNone) return result;
    ++next_sequence_; return Apply(frame, false);
}

Error RouteProvenanceStore::Apply(const JournalFrame& frame, bool replay) {
    auto pending = pending_.find(frame.transaction);
    if (frame.event == Event::kPrepare) {
        if (finalized_.contains(frame.transaction)) {
            return Error::kTransactionConflict;
        }
        if (!frame.record
            || (frame.operation != Operation::kCreate
                && !frame.record->identity.Strong())) return Error::kCorrupt;
        if (pending != pending_.end()) return pending->second.record == *frame.record ? Error::kNone : Error::kTransactionConflict;
        if (pending_.size() >= kMaxPending) return Error::kStoreLimitExceeded;
        if (frame.operation == Operation::kCreate
            && committed_.contains(frame.record->key)) return Error::kRouteBusy;
        if (frame.operation != Operation::kCreate) {
            if (!frame.previous_record) return Error::kCorrupt;
            const auto previous = committed_.find(frame.previous_record->key);
            if (previous == committed_.end()
                || previous->second != *frame.previous_record) {
                return Error::kIdentityMismatch;
            }
        }
        if (frame.operation == Operation::kRename
            && frame.record->key != frame.previous_record->key
            && committed_.contains(frame.record->key)) return Error::kRouteBusy;
        for (const auto& item : pending_) {
            if (item.second.record.key == frame.record->key) {
                return Error::kRouteBusy;
            }
        }
        pending_.emplace(frame.transaction, Pending{
            frame.operation, *frame.record, frame.previous_record, false});
        return Error::kNone;
    }
    if (pending == pending_.end()) return replay ? Error::kCorrupt : Error::kInvalidState;
    if (pending->second.operation != frame.operation) return Error::kTransactionConflict;
    if (frame.event == Event::kMaterialized) { if (!frame.record || !frame.record->identity.Strong()) return Error::kCorrupt; pending->second.record.identity = frame.record->identity; pending->second.materialized = true; return Error::kNone; }
    if (frame.event == Event::kAbort) {
        pending_.erase(pending);
        finalized_[frame.transaction] = Event::kAbort;
        return Error::kNone;
    }
    if (frame.event != Event::kCommit || !frame.record
        || (frame.operation != Operation::kDelete
            && !pending->second.materialized)) return Error::kInvalidState;
    RouteRecord record = *frame.record; record.commit_sequence = frame.sequence;
    if (record.key != pending->second.record.key || record.identity != pending->second.record.identity) return Error::kTransactionConflict;
    if (frame.operation == Operation::kCreate
        && committed_.size() >= kMaxRecords) {
        return Error::kStoreLimitExceeded;
    }
    if (frame.operation == Operation::kDelete) {
        committed_.erase(pending->second.previous_record->key);
    } else {
        if (frame.operation == Operation::kRename) {
            committed_.erase(pending->second.previous_record->key);
        }
        committed_[record.key] = record;
    }
    pending_.erase(pending);
    finalized_[frame.transaction] = Event::kCommit;
    return Error::kNone;
}

Error RouteProvenanceStore::Recover() {
    committed_.clear(); pending_.clear(); finalized_.clear(); next_sequence_ = 1;
    if (!journal_) return Error::kUnavailable; std::vector<JournalFrame> frames;
    const Error loaded = journal_->Replay(&frames); if (loaded != Error::kNone) return loaded;
    for (const auto& frame : frames) { if (frame.sequence != next_sequence_++) return Error::kCorrupt; const Error applied = Apply(frame, true); if (applied != Error::kNone) return applied; }
    // This store deliberately does not infer filesystem state from a path
    // after restart. Conservatively release every recovered reservation and
    // leave any physical intermediate object unowned/ambiguous. The abort is
    // durable so another restart cannot resurrect the pending transaction.
    while (!pending_.empty()) {
        const TransactionId transaction = pending_.begin()->first;
        const Pending pending = pending_.begin()->second;
        const Error aborted = Append(transaction, Event::kAbort,
                                     pending.operation, std::nullopt);
        if (aborted != Error::kNone) return aborted;
    }
    return Error::kNone;
}

Error RouteProvenanceStore::Prepare(TransactionId tx, Operation op, const RouteRecord& record) {
    if (!tx.valid() || op != Operation::kCreate) return Error::kInvalidState;
    if (finalized_.contains(tx)) return Error::kTransactionConflict;
    const auto existing = pending_.find(tx);
    if (existing != pending_.end()) {
        return existing->second.record == record ? Error::kNone
            : Error::kTransactionConflict;
    }
    if (committed_.contains(record.key)) return Error::kRouteBusy;
    for (const auto& item : pending_) {
        if (item.second.record.key == record.key) return Error::kRouteBusy;
    }
    return Append(tx, Event::kPrepare, op, record);
}
Error RouteProvenanceStore::PrepareRename(TransactionId tx,
        const RouteRecord& previous, const RouteRecord& candidate) {
    if (!tx.valid() || !previous.identity.Strong() || !candidate.identity.Strong()) {
        return Error::kIdentityMismatch;
    }
    if (finalized_.contains(tx)) return Error::kTransactionConflict;
    const auto existing = pending_.find(tx);
    if (existing != pending_.end()) {
        return existing->second.operation == Operation::kRename
                && existing->second.record == candidate
                && existing->second.previous_record == previous
            ? Error::kNone : Error::kTransactionConflict;
    }
    const auto owner = committed_.find(previous.key);
    if (owner == committed_.end() || owner->second != previous) {
        return Error::kIdentityMismatch;
    }
    if (candidate.key != previous.key && committed_.contains(candidate.key)) {
        return Error::kRouteBusy;
    }
    for (const auto& item : pending_) {
        if (item.second.record.key == candidate.key) return Error::kRouteBusy;
    }
    return Append(tx, Event::kPrepare, Operation::kRename, candidate, previous);
}
Error RouteProvenanceStore::PrepareDelete(TransactionId tx,
        const RouteRecord& previous) {
    if (!tx.valid() || !previous.identity.Strong()) return Error::kIdentityMismatch;
    if (finalized_.contains(tx)) return Error::kTransactionConflict;
    const auto existing = pending_.find(tx);
    if (existing != pending_.end()) {
        return existing->second.operation == Operation::kDelete
                && existing->second.previous_record == previous
            ? Error::kNone : Error::kTransactionConflict;
    }
    const auto owner = committed_.find(previous.key);
    if (owner == committed_.end() || owner->second != previous) {
        return Error::kIdentityMismatch;
    }
    return Append(tx, Event::kPrepare, Operation::kDelete, previous, previous);
}
Error RouteProvenanceStore::Materialize(TransactionId tx, const ObjectIdentity& identity) {
    auto it = pending_.find(tx);
    if (it == pending_.end() || !identity.Strong()) return Error::kInvalidState;
    if (it->second.materialized) {
        return it->second.record.identity == identity
            ? Error::kNone : Error::kTransactionConflict;
    }
    RouteRecord record = it->second.record; record.identity = identity;
    return Append(tx, Event::kMaterialized, it->second.operation, record);
}
Error RouteProvenanceStore::Commit(TransactionId tx) {
    auto it = pending_.find(tx);
    if (it == pending_.end()) {
        const auto final = finalized_.find(tx);
        return final != finalized_.end() && final->second == Event::kCommit
            ? Error::kNone : Error::kInvalidState;
    }
    if (it->second.operation != Operation::kDelete && !it->second.materialized) {
        return Error::kInvalidState;
    }
    if (it->second.operation == Operation::kCreate
        && committed_.size() >= kMaxRecords) {
        return Error::kStoreLimitExceeded;
    }
    return Append(tx, Event::kCommit, it->second.operation, it->second.record,
                  it->second.previous_record);
}
Error RouteProvenanceStore::Abort(TransactionId tx) {
    auto it = pending_.find(tx);
    if (it == pending_.end()) {
        const auto final = finalized_.find(tx);
        return final == finalized_.end() || final->second == Event::kAbort
            ? Error::kNone : Error::kTransactionConflict;
    }
    return Append(tx, Event::kAbort, it->second.operation, std::nullopt);
}

ResolveResult RouteProvenanceStore::ResolveReverse(const RouteScope& scope, const RouteKey& key,
        const ObjectIdentity& identity, std::uint64_t generation) const {
    const auto it = committed_.find(key);
    if (it == committed_.end()) return {};
    const RouteRecord& record = it->second;
    if (record.scope != scope) return {};
    if (!identity.Strong() || record.identity != identity) return {ResolveStatus::kAmbiguous, Error::kIdentityMismatch, std::nullopt};
    if (record.bound_plan_generation != generation) return {ResolveStatus::kAmbiguous, Error::kPolicyStale, std::nullopt};
    return {ResolveStatus::kUnique, Error::kNone, record};
}

MutationResult ProvenanceCoordinator::Create(TransactionId tx, const RouteRecord& candidate,
        const std::function<int()>& create_no_replace,
        const std::function<std::optional<ObjectIdentity>()>& identity,
        const std::function<bool(const ObjectIdentity&)>& compensate_unlink) {
    MutationResult result; if (!store_) { result.provenance_error = Error::kUnavailable; return result; }
    result.provenance_error = store_->Prepare(tx, Operation::kCreate, candidate);
    if (result.provenance_error != Error::kNone) { result.error_number = result.provenance_error == Error::kRouteBusy ? EEXIST : 0; return result; }
    result.error_number = create_no_replace();
    if (result.error_number != 0) { store_->Abort(tx); return result; }
    const auto materialized = identity();
    if (!materialized || !materialized->Strong()) { result.provenance_error = Error::kIdentityMismatch; result.compensated = materialized && compensate_unlink(*materialized); store_->Abort(tx); result.error_number = EIO; return result; }
    result.provenance_error = store_->Materialize(tx, *materialized);
    if (result.provenance_error == Error::kNone) result.provenance_error = store_->Commit(tx);
    if (result.provenance_error != Error::kNone) { result.compensated = compensate_unlink(*materialized); store_->Abort(tx); result.error_number = EIO; return result; }
    result.committed = true; return result;
}

MutationResult ProvenanceCoordinator::Rename(TransactionId tx,
        const RouteRecord& previous, const RouteRecord& candidate,
        const std::function<int()>& rename_no_replace,
        const std::function<std::optional<ObjectIdentity>()>& new_identity,
        const std::function<bool(const ObjectIdentity&)>& compensate_rename) {
    MutationResult result;
    if (!store_) { result.provenance_error = Error::kUnavailable; return result; }
    result.provenance_error = store_->PrepareRename(tx, previous, candidate);
    if (result.provenance_error != Error::kNone) {
        result.error_number = result.provenance_error == Error::kRouteBusy ? EEXIST : 0;
        return result;
    }
    result.error_number = rename_no_replace();
    if (result.error_number != 0) { store_->Abort(tx); return result; }
    const auto identity = new_identity();
    if (!identity || !identity->Strong()) {
        result.provenance_error = Error::kIdentityMismatch;
        result.compensated = identity && compensate_rename(*identity);
        store_->Abort(tx); result.error_number = EIO; return result;
    }
    result.provenance_error = store_->Materialize(tx, *identity);
    if (result.provenance_error == Error::kNone) result.provenance_error = store_->Commit(tx);
    if (result.provenance_error != Error::kNone) {
        result.compensated = compensate_rename(*identity);
        store_->Abort(tx); result.error_number = EIO; return result;
    }
    result.committed = true; return result;
}

MutationResult ProvenanceCoordinator::Delete(TransactionId tx,
        const RouteRecord& previous, const std::function<int()>& remove,
        const std::function<void()>& mark_degraded) {
    MutationResult result;
    if (!store_) { result.provenance_error = Error::kUnavailable; return result; }
    result.provenance_error = store_->PrepareDelete(tx, previous);
    if (result.provenance_error != Error::kNone) return result;
    result.error_number = remove();
    if (result.error_number != 0) { store_->Abort(tx); return result; }
    result.provenance_error = store_->Commit(tx);
    if (result.provenance_error != Error::kNone) {
        mark_degraded();
        return result;
    }
    result.committed = true; return result;
}

}  // namespace pathguard::provenance
