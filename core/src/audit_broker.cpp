#include "pathguard/audit_broker.h"

#include <cstring>

namespace pathguard::audit {
namespace {

template <std::size_t Size>
bool ReadText(const char (&input)[Size], std::string* output, bool required) {
    const void* end = std::memchr(input, '\0', Size);
    if (end == nullptr) return false;
    const auto size = static_cast<std::size_t>(
        static_cast<const char*>(end) - input);
    if (required && size == 0) return false;
    output->assign(input, size);
    return true;
}

template <std::size_t Size>
bool WriteText(const std::string& input, char (&output)[Size], bool required) {
    if ((required && input.empty()) || input.size() >= Size) return false;
    std::memcpy(output, input.c_str(), input.size() + 1);
    return true;
}

bool DecodeRecord(const audit_protocol::Record& input, Record* output) {
    if (output == nullptr || input.identity.handle_size
            > audit_protocol::kHandleCapacity
        || input.identity.reserved != 0
        || !ReadText(input.logical_source, &output->logical_source_path, true)
        || !ReadText(input.target_path, &output->target_path, true)
        || !ReadText(input.previous_target_path,
                     &output->previous_target_path, false)) {
        return false;
    }
    for (const std::uint8_t value : input.reserved) {
        if (value != 0) return false;
    }
    output->operation = static_cast<Operation>(input.operation);
    output->confidence = static_cast<Confidence>(input.confidence);
    output->caller_uid = input.caller_uid;
    output->user_id = input.user_id;
    output->rule_id = input.rule_id;
    output->content_generation = input.content_generation;
    output->plan_generation = input.plan_generation;
    output->observed_realtime_ns = input.observed_realtime_ns;
    output->observed_boottime_ns = input.observed_boottime_ns;
    output->sequence = input.sequence;
    output->identity.device = input.identity.device;
    output->identity.inode = input.identity.inode;
    output->identity.size = input.identity.size;
    output->identity.mode = input.identity.mode;
    output->identity.modified_seconds = input.identity.modified_seconds;
    output->identity.modified_nanoseconds = input.identity.modified_nanoseconds;
    output->identity.changed_seconds = input.identity.changed_seconds;
    output->identity.changed_nanoseconds = input.identity.changed_nanoseconds;
    output->identity.has_birth_time = input.identity.has_birth_time != 0;
    output->identity.birth_seconds = input.identity.birth_seconds;
    output->identity.birth_nanoseconds = input.identity.birth_nanoseconds;
    output->identity.handle_type = input.identity.handle_type;
    output->identity.handle.assign(
        input.identity.handle,
        input.identity.handle + input.identity.handle_size);
    return input.identity.has_birth_time <= 1
        && output->confidence == output->identity.confidence();
}

bool EncodeRecord(const Record& input, audit_protocol::Record* output) {
    if (output == nullptr || input.identity.handle.size()
            > audit_protocol::kHandleCapacity) {
        return false;
    }
    *output = {};
    output->caller_uid = input.caller_uid;
    output->user_id = input.user_id;
    output->rule_id = input.rule_id;
    output->content_generation = input.content_generation;
    output->plan_generation = input.plan_generation;
    output->observed_realtime_ns = input.observed_realtime_ns;
    output->observed_boottime_ns = input.observed_boottime_ns;
    output->sequence = input.sequence;
    output->operation = static_cast<audit_protocol::Operation>(input.operation);
    output->confidence = static_cast<audit_protocol::Confidence>(input.confidence);
    output->identity.device = input.identity.device;
    output->identity.inode = input.identity.inode;
    output->identity.size = input.identity.size;
    output->identity.mode = input.identity.mode;
    output->identity.modified_seconds = input.identity.modified_seconds;
    output->identity.modified_nanoseconds = input.identity.modified_nanoseconds;
    output->identity.changed_seconds = input.identity.changed_seconds;
    output->identity.changed_nanoseconds = input.identity.changed_nanoseconds;
    output->identity.has_birth_time = input.identity.has_birth_time ? 1 : 0;
    output->identity.birth_seconds = input.identity.birth_seconds;
    output->identity.birth_nanoseconds = input.identity.birth_nanoseconds;
    output->identity.handle_type = input.identity.handle_type;
    output->identity.handle_size = static_cast<std::uint16_t>(
        input.identity.handle.size());
    if (!input.identity.handle.empty()) {
        std::memcpy(output->identity.handle, input.identity.handle.data(),
                    input.identity.handle.size());
    }
    return WriteText(input.logical_source_path, output->logical_source, true)
        && WriteText(input.target_path, output->target_path, true)
        && WriteText(input.previous_target_path,
                     output->previous_target_path, false);
}

}  // namespace

bool Broker::Handle(const audit_protocol::Request& request,
                    audit_protocol::Response* response) {
    if (store_ == nullptr || response == nullptr
        || request.magic != audit_protocol::kMagic
        || request.version != audit_protocol::kVersion
        || request.reserved != 0) {
        return false;
    }
    *response = {};
    Error error = Error::kInvalidRecord;
    switch (request.command) {
        case audit_protocol::Command::kObserve: {
            if (request.snapshot_index != 0 || request.record.sequence != 0) {
                return false;
            }
            Record record;
            if (!DecodeRecord(request.record, &record)) return false;
            error = store_->Observe(std::move(record));
            break;
        }
        case audit_protocol::Command::kSnapshotInfo:
            if (request.snapshot_index != 0) return false;
            error = Error::kNone;
            break;
        case audit_protocol::Command::kSnapshotRecord: {
            Record record;
            if (!store_->CurrentAt(request.snapshot_index, &record)
                || !EncodeRecord(record, &response->record)) {
                return false;
            }
            response->snapshot_index = request.snapshot_index;
            error = Error::kNone;
            break;
        }
        default:
            return false;
    }
    response->snapshot_generation = store_->generation();
    response->snapshot_count = static_cast<std::uint32_t>(
        store_->current_count());
    response->error = static_cast<audit_protocol::Error>(error);
    return true;
}

}  // namespace pathguard::audit
