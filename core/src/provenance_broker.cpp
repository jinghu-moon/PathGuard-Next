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

ObjectIdentity DecodeIdentity(const provenance_protocol::Identity& input) {
    ObjectIdentity output;
    output.kind = IdentityKind::kStatxBirthTime;
    output.inode = input.inode;
    output.birth_seconds = input.birth_seconds;
    output.birth_nanoseconds = input.birth_nanoseconds;
    output.object_type = input.object_type;
    ReadText(input.volume, &output.volume, false);
    return output;
}

bool DecodeRecord(const provenance_protocol::Record& input,
                  RouteRecord* output) {
    if (output == nullptr || input.caller_uid < 10000 || input.rule_id == 0
        || !ReadText(input.storage_root, &output->key.storage_root_id, true)
        || !ReadText(input.target_relative, &output->key.target_relative_path, true)
        || !ReadText(input.logical_source, &output->logical_source_path, true)) {
        return false;
    }
    output->scope.caller_uid = input.caller_uid;
    output->scope.user_id = input.user_id;
    output->scope.identity_epoch = input.identity_epoch;
    output->identity = DecodeIdentity(input.identity);
    output->rule_id = input.rule_id;
    output->content_generation = input.content_generation;
    output->created_plan_generation = input.created_plan_generation;
    output->bound_plan_generation = input.bound_plan_generation;
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
        && request.command != Command::kMaterialize;
    if (needs_record && !DecodeRecord(request.record, &record)) return false;
    Error error = Error::kInvalidState;
    switch (request.command) {
        case Command::kPrepareCreate:
            error = store_->Prepare(transaction, Operation::kCreate, record);
            break;
        case Command::kMaterialize:
            error = store_->Materialize(transaction,
                                        DecodeIdentity(request.record.identity));
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
        default:
            return false;
    }
    EncodeError(error, response);
    return true;
}

}  // namespace pathguard::provenance
