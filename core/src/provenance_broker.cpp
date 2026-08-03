#include "pathguard/provenance_broker.h"

#include <cstring>
#include <string_view>

namespace pathguard::provenance {
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

bool DecodeIdentity(const provenance_protocol::Identity& input,
                    ObjectIdentity* output) {
    if (output == nullptr
        || input.handle_size > provenance_protocol::kIdentityHandleCapacity
        || (input.kind != provenance_protocol::IdentityKind::kNone
            && input.kind
                != provenance_protocol::IdentityKind::kFileHandle
            && input.kind
                != provenance_protocol::IdentityKind::kStatxBirthTime)) {
        return false;
    }
    *output = {};
    output->kind = input.kind == provenance_protocol::IdentityKind::kFileHandle
        ? IdentityKind::kFileHandle : IdentityKind::kStatxBirthTime;
    output->inode = input.inode;
    output->birth_seconds = input.birth_seconds;
    output->birth_nanoseconds = input.birth_nanoseconds;
    output->handle_type = input.handle_type;
    output->object_type = input.object_type;
    if (!ReadText(input.volume, &output->volume, false)) return false;
    output->handle.assign(input.handle, input.handle + input.handle_size);
    const bool empty = input.kind == provenance_protocol::IdentityKind::kNone
        && output->volume.empty()
        && output->handle.empty() && output->handle_type == 0
        && output->inode == 0 && output->birth_seconds == 0
        && output->birth_nanoseconds == 0;
    return empty || output->Strong();
}

template <std::size_t Size>
bool WriteText(const std::string& input, char (&output)[Size], bool required) {
    if ((required && input.empty()) || input.size() >= Size) return false;
    std::memcpy(output, input.c_str(), input.size() + 1);
    return true;
}

bool EncodeRecord(const RouteRecord& input,
                  provenance_protocol::Record* output) {
    if (output == nullptr || input.scope.caller_uid < 10000
        || input.rule_id == 0 || !input.identity.Strong()) {
        return false;
    }
    *output = {};
    output->caller_uid = input.scope.caller_uid;
    output->user_id = input.scope.user_id;
    output->identity_epoch = input.scope.identity_epoch;
    output->rule_id = input.rule_id;
    output->content_generation = input.content_generation;
    output->created_plan_generation = input.created_plan_generation;
    output->bound_plan_generation = input.bound_plan_generation;
    output->commit_sequence = input.commit_sequence;
    output->identity.inode = input.identity.inode;
    output->identity.birth_seconds = input.identity.birth_seconds;
    output->identity.birth_nanoseconds = input.identity.birth_nanoseconds;
    output->identity.handle_type = input.identity.handle_type;
    output->identity.kind = input.identity.kind == IdentityKind::kFileHandle
        ? provenance_protocol::IdentityKind::kFileHandle
        : provenance_protocol::IdentityKind::kStatxBirthTime;
    output->identity.object_type = input.identity.object_type;
    if (input.identity.handle.size()
            > provenance_protocol::kIdentityHandleCapacity) {
        return false;
    }
    output->identity.handle_size = static_cast<std::uint16_t>(
        input.identity.handle.size());
    if (!input.identity.handle.empty()) {
        std::memcpy(output->identity.handle, input.identity.handle.data(),
                    input.identity.handle.size());
    }
    return WriteText(input.identity.volume, output->identity.volume, true)
        && WriteText(input.key.storage_root_id, output->storage_root, true)
        && WriteText(input.key.target_relative_path,
                     output->target_relative, true)
        && WriteText(input.logical_source_path,
                     output->logical_source, true)
        && WriteText(input.provider_uri, output->provider_uri, false)
        && WriteText(input.stable_document_id,
                     output->stable_document_id, false);
}

bool DecodeRecord(const provenance_protocol::Record& input,
                  RouteRecord* output) {
    if (output == nullptr || input.caller_uid < 10000 || input.rule_id == 0
        || !ReadText(input.storage_root, &output->key.storage_root_id, true)
        || !ReadText(input.target_relative, &output->key.target_relative_path, true)
        || !ReadText(input.logical_source, &output->logical_source_path, true)
        || !ReadText(input.provider_uri, &output->provider_uri, false)
        || !ReadText(input.stable_document_id,
                     &output->stable_document_id, false)) {
        return false;
    }
    ObjectIdentity identity;
    if (!DecodeIdentity(input.identity, &identity)) return false;
    output->scope.caller_uid = input.caller_uid;
    output->scope.user_id = input.user_id;
    output->scope.identity_epoch = input.identity_epoch;
    output->identity = std::move(identity);
    output->rule_id = input.rule_id;
    output->content_generation = input.content_generation;
    output->created_plan_generation = input.created_plan_generation;
    output->bound_plan_generation = input.bound_plan_generation;
    output->commit_sequence = input.commit_sequence;
    return true;
}

void EncodeError(Error error, provenance_protocol::Response* response) {
    response->provenance_error = static_cast<provenance_protocol::Error>(error);
}

}  // namespace

bool ProvenanceBroker::Handle(const provenance_protocol::Request& request,
                              provenance_protocol::Response* response) {
    using Command = provenance_protocol::Command;
    if (response == nullptr || store_ == nullptr || request.magic != provenance_protocol::kMagic
        || request.version != provenance_protocol::kVersion) return false;
    *response = {};
    const TransactionId transaction{
        request.transaction_high, request.transaction_low};
    RouteRecord record;
    const bool needs_record = request.command != Command::kCommit
        && request.command != Command::kAbort
        && request.command != Command::kMaterialize
        && request.command != Command::kSnapshotInfo
        && request.command != Command::kSnapshotRecord;
    if (needs_record && !DecodeRecord(request.record, &record)) return false;
    Error error = Error::kInvalidState;
    switch (request.command) {
        case Command::kPrepareCreate:
            error = store_->Prepare(transaction, Operation::kCreate, record);
            break;
        case Command::kMaterialize:
            if (!DecodeIdentity(request.record.identity, &record.identity)) {
                return false;
            }
            error = store_->Materialize(transaction, record.identity);
            break;
        case Command::kCommit:
            error = store_->Commit(transaction);
            break;
        case Command::kAbort:
            error = store_->Abort(transaction);
            break;
        case Command::kResolve: {
            const ResolveResult result = store_->ResolveReverse(
                record.scope, record.key, record.identity,
                record.bound_plan_generation);
            error = result.error;
            response->resolve_status = static_cast<provenance_protocol::ResolveStatus>(
                result.status);
            if (result.record) {
                const std::string& source = result.record->logical_source_path;
                if (source.size() >= sizeof(response->logical_source)) return false;
                std::memcpy(response->logical_source, source.c_str(), source.size() + 1);
            }
            break;
        }
        case Command::kPrepareRename: {
            RouteRecord previous;
            if (!DecodeRecord(request.previous, &previous)) return false;
            error = store_->PrepareRename(transaction, previous, record);
            break;
        }
        case Command::kPrepareDelete: {
            RouteRecord previous;
            if (!DecodeRecord(request.previous, &previous)) return false;
            error = store_->PrepareDelete(transaction, previous);
            break;
        }
        case Command::kSnapshotInfo:
            response->snapshot_generation = store_->snapshot_generation();
            response->snapshot_count = static_cast<std::uint32_t>(
                store_->committed_count());
            error = Error::kNone;
            break;
        case Command::kSnapshotRecord: {
            RouteRecord snapshot_record;
            const std::size_t index = static_cast<std::size_t>(
                request.transaction_low);
            if (request.transaction_high != 0
                || !store_->CommittedAt(index, &snapshot_record)
                || !EncodeRecord(snapshot_record, &response->record)) {
                return false;
            }
            response->snapshot_generation = store_->snapshot_generation();
            response->snapshot_count = static_cast<std::uint32_t>(
                store_->committed_count());
            response->snapshot_index = static_cast<std::uint32_t>(index);
            error = Error::kNone;
            break;
        }
        case Command::kBindExternalIdentity:
            error = store_->BindExternalIdentity(
                transaction, record.scope, record.key, record.identity,
                record.bound_plan_generation, std::move(record.provider_uri),
                std::move(record.stable_document_id));
            break;
        default:
            return false;
    }
    EncodeError(error, response);
    return true;
}

}  // namespace pathguard::provenance
