#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "pathguard/policy_v6.h"
#include "pathguard/audit_protocol.h"
#include "pathguard/route_audit.h"
#include "pathguard/rules/diagnostic.h"
#include "pathguard/rules/semantic.h"
#include "pathguard/rules/source.h"
#include "pathguard/rules/tools.h"

#if defined(PATHGUARD_ANDROID)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static bool Read(const fs::path& path, std::string* out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    *out = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

static std::string JsonEscape(std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string output;
    output.reserve(value.size() + 2);
    output.push_back('"');
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': output.append("\\\""); break;
            case '\\': output.append("\\\\"); break;
            case '\b': output.append("\\b"); break;
            case '\f': output.append("\\f"); break;
            case '\n': output.append("\\n"); break;
            case '\r': output.append("\\r"); break;
            case '\t': output.append("\\t"); break;
            default:
                if (byte < 0x20) {
                    output.append("\\u00");
                    output.push_back(kHex[byte >> 4U]);
                    output.push_back(kHex[byte & 0x0fU]);
                } else {
                    output.push_back(static_cast<char>(byte));
                }
                break;
        }
    }
    output.push_back('"');
    return output;
}

static bool IsJsonInteger(std::string_view value) {
    if (value.empty()) return false;
    std::size_t offset = value.front() == '-' ? 1 : 0;
    if (offset == value.size()) return false;
    if (value[offset] == '0') return offset + 1 == value.size();
    if (value[offset] < '1' || value[offset] > '9') return false;
    for (++offset; offset < value.size(); ++offset) {
        if (value[offset] < '0' || value[offset] > '9') return false;
    }
    return true;
}

static std::string StatusTextToJson(std::string_view text) {
    std::string output = "{";
    bool first = true;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t newline = text.find('\n', begin);
        const std::size_t end = newline == std::string_view::npos
            ? text.size() : newline;
        std::string_view line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const std::size_t equals = line.find('=');
        if (equals != std::string_view::npos && equals != 0) {
            const std::string_view key = line.substr(0, equals);
            const std::string_view value = line.substr(equals + 1);
            if (!first) output.push_back(',');
            first = false;
            output.append(JsonEscape(key));
            output.push_back(':');
            if (IsJsonInteger(value) || value == "true" || value == "false"
                || value == "null") {
                output.append(value);
            } else {
                output.append(JsonEscape(value));
            }
        }
        if (newline == std::string_view::npos) break;
        begin = newline + 1;
    }
    output.push_back('}');
    return output;
}

static int PrintStatus(const fs::path& module_dir, const char* pid,
                       bool json) {
    const fs::path directory = module_dir / "run" / "status";
    if (pid != nullptr) {
        std::string text;
        if (!Read(directory / (std::string(pid) + ".status"), &text)) {
            std::cerr << "status not found\n";
            return 1;
        }
        std::cout << (json ? StatusTextToJson(text) : text);
        if (json) std::cout << '\n';
        return 0;
    }
    if (json) {
        std::string rules_json;
        const bool has_rules = Read(module_dir / "run" / "rules-status.json",
                                    &rules_json);
        std::error_code error;
        std::vector<fs::path> entries;
        if (fs::is_directory(directory, error)) {
            for (const fs::directory_entry& entry :
                 fs::directory_iterator(directory, error)) {
                if (entry.is_regular_file()
                    && entry.path().extension() == ".status") {
                    entries.push_back(entry.path());
                }
            }
        }
        if (error) return 1;
        std::sort(entries.begin(), entries.end());
        std::cout << "{\"schema\":\"pathguard.status.v1\",\"rules\":";
        std::cout << (has_rules ? rules_json : "null");
        std::cout << ",\"processes\":[";
        bool first = true;
        for (const fs::path& entry : entries) {
            std::string text;
            if (!Read(entry, &text)) continue;
            if (!first) std::cout << ',';
            first = false;
            std::cout << StatusTextToJson(text);
        }
        std::cout << "]}\n";
        return 0;
    }
    std::string rules_status;
    if (Read(module_dir / "run" / "rules-status.txt", &rules_status)) {
        std::cout << rules_status;
    }
    std::error_code error;
    if (!fs::is_directory(directory, error)) {
        std::cout << "no runtime status\n";
        return 0;
    }
    std::vector<fs::path> entries;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".status") {
            entries.push_back(entry.path());
        }
    }
    std::sort(entries.begin(), entries.end());
    for (const fs::path& entry : entries) {
        std::string text;
        if (Read(entry, &text)) std::cout << "[" << entry.stem().string() << "]\n" << text;
    }
    return error ? 1 : 0;
}

static const char* AuditOperationName(pathguard::audit::Operation operation) {
    using pathguard::audit::Operation;
    switch (operation) {
        case Operation::kUpsert: return "upsert";
        case Operation::kRename: return "rename";
        case Operation::kDelete: return "delete";
    }
    return "unknown";
}

static const char* AuditConfidenceName(pathguard::audit::Confidence confidence) {
    using pathguard::audit::Confidence;
    switch (confidence) {
        case Confidence::kPathOnly: return "path_only";
        case Confidence::kInodeMetadata: return "inode_metadata";
        case Confidence::kBirthTime: return "birth_time";
        case Confidence::kFileHandle: return "file_handle";
    }
    return "unknown";
}

static bool LoadAuditSnapshot(
        const fs::path& module_dir, std::vector<pathguard::audit::Record>* records,
        std::uint64_t* generation) {
    if (records == nullptr || generation == nullptr) return false;
#if defined(PATHGUARD_ANDROID)
    const auto call = [](const pathguard::audit_protocol::Request& request,
                         pathguard::audit_protocol::Response* response) {
        const int socket_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (socket_fd < 0) return false;
        timeval timeout{0, 500000};
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout));
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout));
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path,
                    pathguard::audit_protocol::kAndroidSocketPath,
                    sizeof(pathguard::audit_protocol::kAndroidSocketPath));
        const bool ok = connect(
                socket_fd, reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) == 0
            && send(socket_fd, &request, sizeof(request), MSG_NOSIGNAL)
                == static_cast<ssize_t>(sizeof(request))
            && recv(socket_fd, response, sizeof(*response), MSG_WAITALL)
                == static_cast<ssize_t>(sizeof(*response))
            && response->magic == pathguard::audit_protocol::kMagic
            && response->version == pathguard::audit_protocol::kVersion
            && response->error == pathguard::audit_protocol::Error::kNone;
        close(socket_fd);
        return ok;
    };
    pathguard::audit_protocol::Request request;
    request.command = pathguard::audit_protocol::Command::kSnapshotInfo;
    pathguard::audit_protocol::Response response;
    if (!call(request, &response)) return false;
    const std::uint64_t expected_generation = response.snapshot_generation;
    const std::uint32_t count = response.snapshot_count;
    records->clear();
    records->reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        request = {};
        request.command = pathguard::audit_protocol::Command::kSnapshotRecord;
        request.snapshot_index = index;
        if (!call(request, &response)
            || response.snapshot_generation != expected_generation) {
            return false;
        }
        const auto& wire = response.record;
        pathguard::audit::Record record;
        record.operation = static_cast<pathguard::audit::Operation>(wire.operation);
        record.confidence = static_cast<pathguard::audit::Confidence>(wire.confidence);
        record.caller_uid = wire.caller_uid;
        record.user_id = wire.user_id;
        record.rule_id = wire.rule_id;
        record.content_generation = wire.content_generation;
        record.plan_generation = wire.plan_generation;
        record.observed_realtime_ns = wire.observed_realtime_ns;
        record.observed_boottime_ns = wire.observed_boottime_ns;
        record.sequence = wire.sequence;
        record.logical_source_path = wire.logical_source;
        record.target_path = wire.target_path;
        record.previous_target_path = wire.previous_target_path;
        record.identity.device = wire.identity.device;
        record.identity.inode = wire.identity.inode;
        record.identity.size = wire.identity.size;
        record.identity.mode = wire.identity.mode;
        record.identity.modified_seconds = wire.identity.modified_seconds;
        record.identity.modified_nanoseconds = wire.identity.modified_nanoseconds;
        record.identity.changed_seconds = wire.identity.changed_seconds;
        record.identity.changed_nanoseconds = wire.identity.changed_nanoseconds;
        record.identity.has_birth_time = wire.identity.has_birth_time != 0;
        record.identity.birth_seconds = wire.identity.birth_seconds;
        record.identity.birth_nanoseconds = wire.identity.birth_nanoseconds;
        record.identity.handle_type = wire.identity.handle_type;
        record.identity.handle.assign(
            wire.identity.handle,
            wire.identity.handle + wire.identity.handle_size);
        records->push_back(std::move(record));
    }
    *generation = expected_generation;
    return true;
#else
    pathguard::audit::FileJournal journal(
        (module_dir / "run" / "audit-v1.wal").string());
    pathguard::audit::Store store(&journal);
    if (store.Recover() != pathguard::audit::Error::kNone) return false;
    records->clear();
    records->reserve(store.current_count());
    for (std::size_t index = 0; index < store.current_count(); ++index) {
        pathguard::audit::Record record;
        if (!store.CurrentAt(index, &record)) return false;
        records->push_back(std::move(record));
    }
    *generation = store.generation();
    return true;
#endif
}

static int PrintAudit(const fs::path& module_dir, bool json) {
    std::vector<pathguard::audit::Record> records;
    std::uint64_t generation = 0;
    if (!LoadAuditSnapshot(module_dir, &records, &generation)) {
        std::cerr << "cannot read private audit store\n";
        return 1;
    }
    if (json) {
        std::cout << "{\"schema\":\"pathguard.private-audit.v1\",\"generation\":"
                  << generation << ",\"records\":[";
    } else {
        std::cout << "generation=" << generation
                  << "\nrecord_count=" << records.size() << '\n';
    }
    for (std::size_t index = 0; index < records.size(); ++index) {
        const pathguard::audit::Record& record = records[index];
        if (json) {
            if (index != 0) std::cout << ',';
            std::cout << "{\"operation\":"
                      << JsonEscape(AuditOperationName(record.operation))
                      << ",\"confidence\":"
                      << JsonEscape(AuditConfidenceName(record.confidence))
                      << ",\"caller_uid\":" << record.caller_uid
                      << ",\"user_id\":" << record.user_id
                      << ",\"rule_id\":" << record.rule_id
                      << ",\"sequence\":" << record.sequence
                      << ",\"content_generation\":"
                      << record.content_generation
                      << ",\"plan_generation\":"
                      << record.plan_generation
                      << ",\"logical_source\":"
                      << JsonEscape(record.logical_source_path)
                      << ",\"target\":" << JsonEscape(record.target_path)
                      << ",\"previous_target\":"
                      << JsonEscape(record.previous_target_path)
                      << ",\"device\":" << record.identity.device
                      << ",\"inode\":" << record.identity.inode
                      << ",\"size\":" << record.identity.size
                      << ",\"mode\":" << record.identity.mode
                      << ",\"modified_seconds\":"
                      << record.identity.modified_seconds
                      << ",\"modified_nanoseconds\":"
                      << record.identity.modified_nanoseconds
                      << ",\"changed_seconds\":"
                      << record.identity.changed_seconds
                      << ",\"changed_nanoseconds\":"
                      << record.identity.changed_nanoseconds
                      << ",\"has_birth_time\":"
                      << (record.identity.has_birth_time ? "true" : "false")
                      << ",\"birth_seconds\":"
                      << record.identity.birth_seconds
                      << ",\"birth_nanoseconds\":"
                      << record.identity.birth_nanoseconds
                      << ",\"handle_type\":"
                      << record.identity.handle_type
                      << ",\"handle_size\":"
                      << record.identity.handle.size()
                      << ",\"observed_realtime_ns\":"
                      << record.observed_realtime_ns
                      << ",\"observed_boottime_ns\":"
                      << record.observed_boottime_ns << '}';
        } else {
            std::cout << "[" << index << "] operation="
                      << AuditOperationName(record.operation)
                      << " confidence="
                      << AuditConfidenceName(record.confidence)
                      << " uid=" << record.caller_uid
                      << " rule_id=" << record.rule_id
                      << " source=" << record.logical_source_path
                      << " target=" << record.target_path
                      << " dev=" << record.identity.device
                      << " ino=" << record.identity.inode
                      << " size=" << record.identity.size << '\n';
        }
    }
    if (json) std::cout << "]}\n";
    return 0;
}

static pathguard::rules::RulesBuildResult CompileRulesFile(
        const fs::path& path, std::optional<pathguard::rules::SourceBuffer>* source,
        std::string* load_error) {
    using namespace pathguard::rules;
    std::string text;
    if (!Read(path, &text)) {
        *load_error = "cannot read rules.toml";
        return {};
    }
    Diagnostic diagnostic;
    *source = SourceBuffer::Create(
        path.filename().string(), std::move(text), RulesLimits{}, &diagnostic);
    if (!source->has_value()) {
        RulesBuildResult result;
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    return CompileRules(**source, RulesLimits{});
}

static int ValidateOrCompile(const std::string& command, int argc, char** argv) {
    using namespace pathguard::rules;
    if (argc < 3) {
        std::cerr << "missing rules.toml\n";
        return 2;
    }
    bool json = false;
    bool device = false;
    for (int index = 3; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--json") json = true;
        if (option == "--device") device = true;
    }
    std::optional<SourceBuffer> source;
    std::string load_error;
    RulesBuildResult result = CompileRulesFile(argv[2], &source, &load_error);
    if (!load_error.empty()) {
        std::cerr << load_error << '\n';
        return 1;
    }
    if (!result.ok()) {
        if (source.has_value()) {
            for (const Diagnostic& diagnostic : result.diagnostics) {
                std::cerr << (json ? RenderDiagnosticJson(diagnostic, *source)
                                  : RenderDiagnosticText(diagnostic, *source))
                          << '\n';
            }
        }
        return 1;
    }
    if (device) {
        std::cerr << "environment_unsupported: use reload so the daemon can "
                     "validate the current device snapshot\n";
        return 1;
    }
    if (command == "validate") {
        std::cout << "valid: " << result.canonical_v2->apps.size()
                  << " package(s), content_generation="
                  << result.blob->content_generation << '\n';
        return 0;
    }
    if (argc < 4 || std::string(argv[3]).starts_with("--")) {
        std::cerr << "missing policy.bin output\n";
        return 2;
    }
    const fs::path output_path = argv[3];
    if (output_path.filename() == "policy.bin"
        && output_path.parent_path().filename() == "run") {
        std::cerr << "refusing to write an active policy path; use reload\n";
        return 1;
    }
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(result.blob->bytes.data()),
                 static_cast<std::streamsize>(result.blob->bytes.size()));
    if (!output) {
        std::cerr << "cannot write policy.bin\n";
        return 1;
    }
    std::cout << "compiled: " << result.blob->bytes.size()
              << " bytes, content_generation="
              << result.blob->content_generation << '\n';
    return 0;
}

static void PrintDiagnostics(
        const pathguard::rules::RulesBuildResult& result,
        const pathguard::rules::SourceBuffer& source,
        bool errors_only = false) {
    using namespace pathguard::rules;
    for (const Diagnostic& diagnostic : result.diagnostics) {
        if (errors_only
            && diagnostic.severity != DiagnosticSeverity::kError) continue;
        std::cerr << RenderDiagnosticText(diagnostic, source) << '\n';
    }
}

static int LintRulesFile(const fs::path& path) {
    using namespace pathguard::rules;
    std::optional<SourceBuffer> source;
    std::string load_error;
    const RulesBuildResult result = CompileRulesFile(path, &source, &load_error);
    if (!load_error.empty()) {
        std::cerr << load_error << '\n';
        return 1;
    }
    if (!source.has_value()) return 1;
    PrintDiagnostics(result, *source, true);
    for (const Diagnostic& diagnostic : LintRules(result, RulesLimits{})) {
        std::cout << RenderDiagnosticText(diagnostic, *source) << '\n';
    }
    return result.ok() ? 0 : 1;
}

static const char* ChangeName(pathguard::rules::PolicyChangeKind kind) {
    using pathguard::rules::PolicyChangeKind;
    switch (kind) {
        case PolicyChangeKind::kAdd: return "add";
        case PolicyChangeKind::kRemove: return "remove";
        case PolicyChangeKind::kModify: return "modify";
    }
    return "unknown";
}

static const char* RuleName(pathguard::rules::PolicyRuleKind kind) {
    using pathguard::rules::PolicyRuleKind;
    switch (kind) {
        case PolicyRuleKind::kDeny: return "deny";
        case PolicyRuleKind::kRedirect: return "redirect";
        case PolicyRuleKind::kObserve: return "observe";
        case PolicyRuleKind::kExport: return "export";
    }
    return "unknown";
}

static int PlanRulesFiles(const fs::path& before_path,
                          const fs::path& after_path) {
    using namespace pathguard::rules;
    std::optional<SourceBuffer> before_source;
    std::optional<SourceBuffer> after_source;
    std::string error;
    const RulesBuildResult before = CompileRulesFile(
        before_path, &before_source, &error);
    if (!error.empty() || !before_source.has_value()) {
        std::cerr << (error.empty() ? "invalid old rules.toml" : error) << '\n';
        return 1;
    }
    if (!before.ok()) {
        PrintDiagnostics(before, *before_source);
        return 1;
    }
    error.clear();
    const RulesBuildResult after = CompileRulesFile(
        after_path, &after_source, &error);
    if (!error.empty() || !after_source.has_value()) {
        std::cerr << (error.empty() ? "invalid new rules.toml" : error) << '\n';
        return 1;
    }
    if (!after.ok()) {
        PrintDiagnostics(after, *after_source);
        return 1;
    }
    for (const PolicyChange& change :
         BuildPolicyPlan(*before.canonical_v2, *after.canonical_v2)) {
        std::cout << ChangeName(change.kind) << ' ' << change.package << ' '
                  << RuleName(change.rule_kind) << ' ' << change.source;
        if (!change.before_target.empty()) {
            std::cout << " from=" << change.before_target;
        }
        if (!change.after_target.empty()) {
            std::cout << " to=" << change.after_target;
        }
        std::cout << '\n';
    }
    return 0;
}

static int ExplainRulesPath(const fs::path& rules_path,
                            std::string_view package,
                            std::string_view path) {
    using namespace pathguard::rules;
    std::optional<SourceBuffer> source;
    std::string error;
    const RulesBuildResult result = CompileRulesFile(
        rules_path, &source, &error);
    if (!error.empty() || !source.has_value()) {
        std::cerr << (error.empty() ? "invalid rules.toml" : error) << '\n';
        return 1;
    }
    if (!result.canonical_v2.has_value()) {
        PrintDiagnostics(result, *source);
        return 1;
    }
    const PathExplanation explanation = ExplainPath(
        *result.canonical_v2, package, path, RulesLimits{});
    std::cout << "package=" << explanation.package
              << "\npath=" << explanation.query << '\n';
    if (!explanation.source.has_value()) {
        std::cout << "match=none\nshadowed_parent=none\n";
        return 0;
    }
    std::cout << "match=" << RuleName(*explanation.action) << ' '
              << *explanation.source;
    if (explanation.target.has_value()) {
        std::cout << " -> " << *explanation.target;
    }
    std::cout << '\n';
    if (explanation.shadowed_parents.empty()) {
        std::cout << "shadowed_parent=none\n";
    } else {
        for (const std::string& parent : explanation.shadowed_parents) {
            std::cout << "shadowed_parent=" << parent << '\n';
        }
    }
    return 0;
}

static const char* ActionName(pathguard::PolicyActionKind action) {
    switch (action) {
        case pathguard::PolicyActionKind::kDeny: return "deny";
        case pathguard::PolicyActionKind::kRedirect: return "redirect";
        case pathguard::PolicyActionKind::kObserve: return "observe";
        case pathguard::PolicyActionKind::kExport: return "export";
    }
    return "unknown";
}

static const char* DomainName(pathguard::PolicyExecutionDomain domain) {
    switch (domain) {
        case pathguard::PolicyExecutionDomain::kMount: return "mount";
        case pathguard::PolicyExecutionDomain::kAppPath: return "app_path";
        case pathguard::PolicyExecutionDomain::kProvider: return "provider";
        case pathguard::PolicyExecutionDomain::kCompleteVfs: return "complete_vfs";
        case pathguard::PolicyExecutionDomain::kEvent: return "event";
    }
    return "unknown";
}

static void AppendEscapedPatternLiteral(std::string_view value,
                                        std::string* output) {
    static constexpr char kHex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
        if (byte < 0x20 || byte == 0x7f) {
            output->append("\\x");
            output->push_back(kHex[byte >> 4U]);
            output->push_back(kHex[byte & 0x0fU]);
        } else {
            if (byte == '\\' || byte == '*' || byte == '?' || byte == '['
                || byte == ']' || byte == '!') {
                output->push_back('\\');
            }
            output->push_back(static_cast<char>(byte));
        }
    }
}

static std::string RenderPattern(
        const pathguard::pattern::PatternProgram& program) {
    std::string output;
    for (std::size_t component_index = 0;
         component_index < program.components.size(); ++component_index) {
        if (component_index != 0) output.push_back('/');
        const auto& component = program.components[component_index];
        if (component.globstar) {
            output.append("**");
            continue;
        }
        for (const auto& token : component.tokens) {
            switch (token.kind) {
                case pathguard::pattern::PatternTokenKind::kLiteral:
                    AppendEscapedPatternLiteral(token.literal, &output);
                    break;
                case pathguard::pattern::PatternTokenKind::kStarComponent:
                    output.push_back('*');
                    break;
                case pathguard::pattern::PatternTokenKind::kOneComponentChar:
                    output.push_back('?');
                    break;
                case pathguard::pattern::PatternTokenKind::kCharacterClass: {
                    if (token.character_class >= program.character_classes.size()) {
                        return "<invalid-character-class>";
                    }
                    const auto& character_class =
                        program.character_classes[token.character_class];
                    output.push_back('[');
                    if (character_class.negated) output.push_back('!');
                    for (unsigned value = 0; value < 128; ++value) {
                        if ((character_class.bitmap[value / 64U]
                             & (UINT64_C(1) << (value % 64U))) == 0) {
                            continue;
                        }
                        const char byte = static_cast<char>(value);
                        if (byte == '\\' || byte == ']' || byte == '-') {
                            output.push_back('\\');
                        }
                        if (value < 0x20 || value == 0x7f) {
                            static constexpr char kHex[] = "0123456789abcdef";
                            output.append("\\x");
                            output.push_back(kHex[value >> 4U]);
                            output.push_back(kHex[value & 0x0fU]);
                        } else {
                            output.push_back(byte);
                        }
                    }
                    output.push_back(']');
                    break;
                }
            }
        }
    }
    return output;
}

static int ExplainPolicy(const fs::path& policy, const std::string& package,
                         bool json) {
    std::string raw;
    if (!Read(policy, &raw)) {
        std::cerr << "cannot read policy.bin\n";
        return 1;
    }
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    pathguard::PolicyV6 document;
    const pathguard::PolicyV6DecodeResult decoded =
        pathguard::DecodePolicyV6(bytes, &document);
    if (!decoded.ok) {
        std::cerr << "invalid policy.bin: " << decoded.error << '\n';
        return 1;
    }
    for (const pathguard::PolicyPackageV6& app : document.packages) {
        if (app.package != package) continue;
        std::uint64_t capability_union = 0;
        std::uint64_t operation_union = 0;
        for (const auto& action : app.actions) {
            capability_union |= action.required_capabilities;
            operation_union |= action.required_operations;
        }
        if (json) {
            std::cout << "{\"schema\":\"pathguard.explain.v1\",\"package\":"
                      << JsonEscape(app.package)
                      << ",\"content_generation\":" << decoded.content_generation
                      << ",\"plan_generation\":" << app.plan_generation
                      << ",\"provider_intent\":"
                      << (app.provider_enabled ? "true" : "false")
                      << ",\"required_capabilities_union\":"
                      << capability_union
                      << ",\"required_operations_union\":"
                      << operation_union
                      << ",\"admission\":\"not_evaluated\",\"actions\":[";
            bool first_action = true;
            for (const pathguard::PolicyActionV6& action : app.actions) {
                if (action.selector_index >= app.selectors.size()) {
                    std::cerr << "invalid selector reference\n";
                    return 1;
                }
                const auto& selector = app.selectors[action.selector_index];
                if (!first_action) std::cout << ',';
                first_action = false;
                std::cout << "{\"action\":" << JsonEscape(ActionName(action.kind))
                          << ",\"domain\":" << JsonEscape(DomainName(action.domain))
                          << ",\"rule_id\":" << action.rule_id
                          << ",\"selector_id\":" << action.selector_index
                          << ",\"root\":" << JsonEscape(selector.root)
                          << ",\"object_type\":"
                          << static_cast<unsigned>(selector.object_type)
                          << ",\"match_kind\":"
                          << JsonEscape(selector.match_kind
                                  == pathguard::PolicyMatchKind::kGlob
                              ? "glob" : "literal_prefix");
                if (selector.match_kind == pathguard::PolicyMatchKind::kGlob) {
                    std::cout << ",\"glob\":"
                              << JsonEscape(RenderPattern(selector.base_pattern))
                              << ",\"except\":[";
                    bool first_except = true;
                    for (const auto& except : selector.except_patterns) {
                        if (!first_except) std::cout << ',';
                        first_except = false;
                        std::cout << JsonEscape(RenderPattern(except));
                    }
                    std::cout << ']';
                }
                if (!action.target.empty()) {
                    std::cout << ",\"target\":" << JsonEscape(action.target);
                }
                std::cout << ",\"priority\":" << action.priority
                          << ",\"required_capabilities\":"
                          << action.required_capabilities
                          << ",\"required_operations\":"
                          << action.required_operations
                          << ",\"options\":" << action.options << '}';
            }
            std::cout << "]}\n";
            return 0;
        }
        std::cout << "package=" << app.package
                  << "\ncontent_generation=" << decoded.content_generation
                  << "\nplan_generation=" << app.plan_generation
                  << "\nprovider_intent=" << (app.provider_enabled ? 1 : 0)
                  << "\nrequired_capabilities_union="
                  << capability_union
                  << "\nrequired_operations_union="
                  << operation_union << '\n';
        for (const pathguard::PolicyActionV6& action : app.actions) {
            if (action.selector_index >= app.selectors.size()) {
                std::cerr << "invalid selector reference\n";
                return 1;
            }
            const auto& selector = app.selectors[action.selector_index];
            std::cout << "action=" << ActionName(action.kind)
                      << " domain=" << DomainName(action.domain)
                      << " rule_id=" << action.rule_id
                      << " selector=" << action.selector_index
                      << " root=" << selector.root;
            if (selector.match_kind == pathguard::PolicyMatchKind::kGlob) {
                std::cout << " glob=" << RenderPattern(selector.base_pattern);
                for (const auto& except : selector.except_patterns) {
                    std::cout << " except=" << RenderPattern(except);
                }
            }
            if (!action.target.empty()) std::cout << " target=" << action.target;
            std::cout << " priority=" << action.priority
                      << " required_capabilities=" << action.required_capabilities
                      << " required_operations=" << action.required_operations
                      << " options=" << action.options << '\n';
        }
        return 0;
    }
    std::cerr << "package not found\n";
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: pathguardctl validate <rules.toml> --host|--device [--json]\n"
                     "       pathguardctl compile <rules.toml> <output-policy.bin>\n"
                     "       pathguardctl lint <rules.toml>\n"
                     "       pathguardctl plan <old-rules.toml> <new-rules.toml>\n"
                     "       pathguardctl explain --path <rules.toml> <package> <path>\n"
                     "       pathguardctl reload <module-dir>\n"
                     "       pathguardctl explain <policy.bin> <package> [--json]\n"
                     "       pathguardctl status <module-dir> [pid] [--json]\n"
                     "       pathguardctl audit <module-dir> [--json]\n";
        return 2;
    }
    const std::string command = argv[1];
    if (command == "lint") {
        if (argc != 3) { std::cerr << "missing rules.toml\n"; return 2; }
        return LintRulesFile(argv[2]);
    }
    if (command == "plan") {
        if (argc != 4) {
            std::cerr << "missing old or new rules.toml\n";
            return 2;
        }
        return PlanRulesFiles(argv[2], argv[3]);
    }
    if (command == "explain" && argc >= 3
        && std::string_view(argv[2]) == "--path") {
        if (argc != 6) {
            std::cerr << "missing rules.toml, package, or path\n";
            return 2;
        }
        return ExplainRulesPath(argv[3], argv[4], argv[5]);
    }
    if (command == "status") {
        if (argc < 3) { std::cerr << "missing module directory\n"; return 2; }
        const char* pid = nullptr;
        bool json = false;
        for (int index = 3; index < argc; ++index) {
            if (std::string_view(argv[index]) == "--json") json = true;
            else if (pid == nullptr) pid = argv[index];
            else { std::cerr << "unexpected status argument\n"; return 2; }
        }
        return PrintStatus(argv[2], pid, json);
    }
    if (command == "audit") {
        if (argc < 3 || argc > 4
            || (argc == 4 && std::string_view(argv[3]) != "--json")) {
            std::cerr << "usage: pathguardctl audit <module-dir> [--json]\n";
            return 2;
        }
        return PrintAudit(argv[2], argc == 4);
    }
    if (command == "explain") {
        if (argc < 4) { std::cerr << "missing policy.bin or package\n"; return 2; }
        if (argc > 5 || (argc == 5
            && std::string_view(argv[4]) != "--json")) {
            std::cerr << "unexpected explain argument\n";
            return 2;
        }
        return ExplainPolicy(argv[2], argv[3], argc == 5);
    }
    if (command == "reload") {
        if (argc < 3) { std::cerr << "missing module directory\n"; return 2; }
        const fs::path source = fs::path(argv[2]) / "config" / "rules.toml";
        std::error_code error;
        fs::last_write_time(source, fs::file_time_type::clock::now(), error);
        if (error) {
            std::cerr << "cannot request reload: " << error.message() << '\n';
            return 1;
        }
        std::cout << "reload requested\n";
        return 0;
    }
    if (command == "validate" || command == "compile") {
        return ValidateOrCompile(command, argc, argv);
    }
    std::cerr << "unknown command\n";
    return 2;
}
