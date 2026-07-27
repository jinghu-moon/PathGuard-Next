#include "pathguard/rules_control.h"

#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "pathguard/binary.h"

namespace pathguard::control {
namespace {

namespace fs = std::filesystem;

struct FileIdentity {
    std::uintmax_t size = 0;
    fs::file_time_type modified{};
#if !defined(_WIN32)
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
#endif

    bool operator==(const FileIdentity&) const = default;
};

constexpr std::array<std::uint32_t, 64> kSha256Round{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t RotateRight(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

std::string Sha256(std::string_view input) {
    std::vector<std::uint8_t> bytes(input.begin(), input.end());
    const std::uint64_t bit_count = static_cast<std::uint64_t>(bytes.size()) * 8;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56) bytes.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(bit_count >> shift));
    }
    std::array<std::uint32_t, 8> hash{
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t at = offset + index * 4;
            words[index] = static_cast<std::uint32_t>(bytes[at]) << 24
                | static_cast<std::uint32_t>(bytes[at + 1]) << 16
                | static_cast<std::uint32_t>(bytes[at + 2]) << 8
                | bytes[at + 3];
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const std::uint32_t s0 = RotateRight(words[index - 15], 7)
                ^ RotateRight(words[index - 15], 18)
                ^ (words[index - 15] >> 3);
            const std::uint32_t s1 = RotateRight(words[index - 2], 17)
                ^ RotateRight(words[index - 2], 19)
                ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        std::uint32_t a = hash[0];
        std::uint32_t b = hash[1];
        std::uint32_t c = hash[2];
        std::uint32_t d = hash[3];
        std::uint32_t e = hash[4];
        std::uint32_t f = hash[5];
        std::uint32_t g = hash[6];
        std::uint32_t h = hash[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t sigma1 = RotateRight(e, 6)
                ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sigma1 + choose
                + kSha256Round[index] + words[index];
            const std::uint32_t sigma0 = RotateRight(a, 2)
                ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }
    std::ostringstream output;
    output << "sha256:" << std::hex << std::setfill('0');
    for (const std::uint32_t value : hash) output << std::setw(8) << value;
    return output.str();
}

bool ReadIdentity(const fs::path& path, FileIdentity* identity,
                  std::string* message) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || !fs::is_regular_file(status)) {
        *message = "rules.toml must be a regular non-symlink file";
        return false;
    }
    identity->size = fs::file_size(path, error);
    if (error) {
        *message = "cannot read rules.toml size";
        return false;
    }
    identity->modified = fs::last_write_time(path, error);
    if (error) {
        *message = "cannot read rules.toml timestamp";
        return false;
    }
#if !defined(_WIN32)
    struct stat stat_buffer {};
    if (lstat(path.c_str(), &stat_buffer) != 0 || !S_ISREG(stat_buffer.st_mode)
        || stat_buffer.st_uid != geteuid() || (stat_buffer.st_mode & 0022) != 0) {
        *message = "rules.toml has unsafe owner or mode";
        return false;
    }
    identity->device = static_cast<std::uint64_t>(stat_buffer.st_dev);
    identity->inode = static_cast<std::uint64_t>(stat_buffer.st_ino);
#endif
    return true;
}

bool ReadAll(const fs::path& path, std::string* bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    *bytes = std::string(std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

#if !defined(_WIN32)
bool ReadFd(int fd, std::size_t size, std::string* bytes) {
    bytes->assign(size, '\0');
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t count = read(fd, bytes->data() + offset, size - offset);
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    char extra = 0;
    return read(fd, &extra, 1) == 0;
}
#endif

bool FlushFile(const fs::path& path) {
#if defined(_WIN32)
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const bool ok = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
    return ok;
#else
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
#endif
}

bool FlushDirectory(const fs::path& path) {
#if defined(_WIN32)
    (void)path;
    return true;
#else
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
#endif
}

std::string UniqueSuffix() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto ticks = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return std::to_string(static_cast<std::uint64_t>(ticks)) + "."
        + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

void RemoveIfExists(const fs::path& path) {
    std::error_code ignored;
    fs::remove(path, ignored);
}

bool ReadGeneration(const fs::path& path, std::uint64_t* generation) {
    std::string raw;
    if (!ReadAll(path, &raw)) return false;
    std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    pathguard::PolicyDocument document;
    pathguard::ParseError error;
    return pathguard::DecodePolicy(bytes, &document, generation, &error);
}

std::uint64_t ReadStatusNumber(const fs::path& path, std::string_view key) {
    std::string text;
    if (!ReadAll(path, &text)) return 0;
    const std::string prefix = std::string(key) + ": ";
    const std::size_t begin = text.find(prefix);
    if (begin == std::string::npos) return 0;
    const std::size_t value_begin = begin + prefix.size();
    const std::size_t end = text.find('\n', value_begin);
    const std::string value = text.substr(value_begin, end - value_begin);
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc()
            && result.ptr == value.data() + value.size()
        ? parsed : 0;
}

const char* StatusName(ControlStatus status) {
    switch (status) {
        case ControlStatus::kActive: return "active";
        case ControlStatus::kSourceInvalid: return "source_invalid";
        case ControlStatus::kEnvironmentUnsupported:
            return "environment_unsupported";
        case ControlStatus::kPublishFailed: return "publish_failed";
    }
    return "source_invalid";
}

std::string JsonEscape(std::string_view input) {
    std::string output;
    for (const char value : input) {
        if (value == '\\' || value == '"') output.push_back('\\');
        if (value == '\n') output.append("\\n");
        else if (value != '\r') output.push_back(value);
    }
    return output;
}

bool AtomicWriteText(const fs::path& path, std::string_view bytes) {
    const fs::path temporary = path.parent_path()
        / ("." + path.filename().string() + "." + UniqueSuffix());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            RemoveIfExists(temporary);
            return false;
        }
    }
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    RemoveIfExists(temporary);
    return false;
#else
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) RemoveIfExists(temporary);
    return !error;
#endif
}

}  // namespace

SourceLoadResult LoadRulesSource(const fs::path& config_directory,
                                 const rules::RulesLimits& limits,
                                 LoadOptions options) {
    SourceLoadResult output;
    const fs::path path = config_directory / kRulesFileName;
#if defined(_WIN32)
    FileIdentity before;
    if (!ReadIdentity(path, &before, &output.message)) {
        output.error_code = "PG-SOURCE-UNSAFE";
        return output;
    }
    if (before.size > limits.max_source_bytes) {
        output.error_code = std::string(rules::kResourceLimit);
        output.message = "rules.toml exceeds source byte limit";
        return output;
    }
    std::string bytes;
    if (!ReadAll(path, &bytes)) {
        output.error_code = "PG-SOURCE-READ";
        output.message = "cannot read rules.toml";
        return output;
    }
    if (options.after_read != nullptr) options.after_read(path);
    std::string stable_bytes;
    if (!ReadAll(path, &stable_bytes)) {
        output.error_code = "PG-SOURCE-UNSTABLE";
        output.message = "cannot complete stable rules.toml read";
        return output;
    }
    FileIdentity after;
    if (!ReadIdentity(path, &after, &output.message) || before != after
        || bytes.size() != after.size || stable_bytes != bytes) {
        output.error_code = "PG-SOURCE-UNSTABLE";
        output.message = "rules.toml changed during stable read";
        return output;
    }
#else
    const int directory_fd = open(config_directory.c_str(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        output.error_code = "PG-SOURCE-READ";
        output.message = "cannot open rules directory";
        return output;
    }
    const int file_fd = openat(directory_fd, kRulesFileName,
                               O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    close(directory_fd);
    if (file_fd < 0) {
        output.error_code = "PG-SOURCE-UNSAFE";
        output.message = "cannot securely open rules.toml";
        return output;
    }
    struct stat before {};
    if (fstat(file_fd, &before) != 0 || !S_ISREG(before.st_mode)
        || before.st_uid != geteuid() || (before.st_mode & 0022) != 0) {
        close(file_fd);
        output.error_code = "PG-SOURCE-UNSAFE";
        output.message = "rules.toml has unsafe type, owner, or mode";
        return output;
    }
    if (before.st_size < 0
        || static_cast<std::uintmax_t>(before.st_size)
            > limits.max_source_bytes) {
        close(file_fd);
        output.error_code = std::string(rules::kResourceLimit);
        output.message = "rules.toml exceeds source byte limit";
        return output;
    }
    std::string bytes;
    if (!ReadFd(file_fd, static_cast<std::size_t>(before.st_size), &bytes)) {
        close(file_fd);
        output.error_code = "PG-SOURCE-READ";
        output.message = "cannot read complete rules.toml";
        return output;
    }
    if (options.after_read != nullptr) options.after_read(path);
    if (lseek(file_fd, 0, SEEK_SET) < 0) {
        close(file_fd);
        output.error_code = "PG-SOURCE-UNSTABLE";
        output.message = "cannot repeat stable rules.toml read";
        return output;
    }
    std::string stable_bytes;
    struct stat after {};
    const bool stable = ReadFd(
        file_fd, static_cast<std::size_t>(before.st_size), &stable_bytes)
        && fstat(file_fd, &after) == 0
        && before.st_dev == after.st_dev && before.st_ino == after.st_ino
        && before.st_size == after.st_size
        && before.st_mtim.tv_sec == after.st_mtim.tv_sec
        && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
        && bytes == stable_bytes;
    close(file_fd);
    if (!stable) {
        output.error_code = "PG-SOURCE-UNSTABLE";
        output.message = "rules.toml changed during stable read";
        return output;
    }
#endif
    constexpr std::string_view bom{"\xef\xbb\xbf", 3};
    if (bytes.find(bom, bytes.starts_with(bom) ? bom.size() : 0)
        != std::string::npos) {
        output.error_code = "PG-SOURCE-BOM";
        output.message = "UTF-8 BOM is only allowed at byte zero";
        return output;
    }
    rules::Diagnostic source_error;
    auto source = rules::SourceBuffer::Create(
        kRulesFileName, std::move(bytes), limits, &source_error);
    if (!source.has_value()) {
        output.error_code = std::string(source_error.code);
        output.message = std::string(source_error.message_key);
        return output;
    }
    const std::string digest = Sha256(source->bytes());
    output.snapshot.emplace(SourceSnapshot{std::move(*source), digest});
    return output;
}

Publisher::Publisher(fs::path run_directory)
    : run_directory_(std::move(run_directory)) {}

PublishResult Publisher::Publish(const rules::PolicyBlob& blob,
                                 PublishOptions options) const {
    PublishResult output;
    output.content_generation = blob.content_generation;
    const fs::path active = run_directory_ / "policy.bin";
    std::uint64_t active_generation = 0;
    if (ReadGeneration(active, &active_generation)
        && active_generation == blob.content_generation) {
        output.unchanged = true;
        return output;
    }
    std::error_code error;
    fs::create_directories(run_directory_, error);
    if (error || options.fail_at == PublishFault::kCreate) {
        output.message = "cannot create policy candidate";
        return output;
    }
    const std::string suffix = UniqueSuffix();
    const fs::path temporary = run_directory_ / (".policy.bin.candidate." + suffix);
    const fs::path backup = run_directory_ / (".policy.bin.backup." + suffix);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file || options.fail_at == PublishFault::kWrite) {
            output.message = "cannot write policy candidate";
            RemoveIfExists(temporary);
            return output;
        }
        file.write(reinterpret_cast<const char*>(blob.bytes.data()),
                   static_cast<std::streamsize>(blob.bytes.size()));
        if (!file) {
            output.message = "cannot write complete policy candidate";
            RemoveIfExists(temporary);
            return output;
        }
    }
    if (options.fail_at == PublishFault::kSetMode) {
        output.message = "cannot set policy mode";
        RemoveIfExists(temporary);
        return output;
    }
    fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write
        | fs::perms::group_read | fs::perms::others_read,
        fs::perm_options::replace, error);
    if (error || options.fail_at == PublishFault::kSetOwner
        || options.fail_at == PublishFault::kSetContext) {
        output.message = "cannot set policy security attributes";
        RemoveIfExists(temporary);
        return output;
    }
    std::string written;
    const bool candidate_read = ReadAll(temporary, &written);
    const std::vector<std::uint8_t> written_bytes(written.begin(), written.end());
    if (options.fail_at == PublishFault::kVerify || !candidate_read
        || written_bytes != blob.bytes
        || !rules::VerifyPolicyBytes(written_bytes, blob.content_generation)) {
        output.message = "policy candidate verification failed";
        RemoveIfExists(temporary);
        return output;
    }
    if (options.fail_at == PublishFault::kFileSync || !FlushFile(temporary)) {
        output.message = "cannot fsync policy candidate";
        RemoveIfExists(temporary);
        return output;
    }
    const bool had_active = fs::is_regular_file(active, error) && !error;
    if (had_active) {
        fs::rename(active, backup, error);
        if (error) {
            output.message = "cannot preserve active policy";
            RemoveIfExists(temporary);
            return output;
        }
    }
    if (options.fail_at == PublishFault::kRename) {
        error = std::make_error_code(std::errc::io_error);
    } else {
        fs::rename(temporary, active, error);
    }
    if (error) {
        if (had_active) {
            std::error_code restore_error;
            fs::rename(backup, active, restore_error);
        }
        RemoveIfExists(temporary);
        output.message = "cannot atomically replace policy.bin";
        return output;
    }
    const bool directory_synced = options.fail_at != PublishFault::kDirectorySync
        && FlushDirectory(run_directory_);
    if (!directory_synced) {
        RemoveIfExists(active);
        if (had_active) {
            std::error_code restore_error;
            fs::rename(backup, active, restore_error);
            FlushDirectory(run_directory_);
        }
        output.message = "cannot fsync policy directory; previous policy restored";
        return output;
    }
    RemoveIfExists(backup);
    FlushDirectory(run_directory_);
    output.published = true;
    return output;
}

std::string RenderControlStatusText(const ControlState& state) {
    std::ostringstream output;
    output << "source: rules.toml\n"
           << "source_digest: " << state.source_digest << '\n'
           << "candidate_sequence: " << state.candidate_sequence << '\n'
           << "active_content_generation: "
           << state.active_content_generation << '\n'
           << "deployment_epoch: " << state.deployment_epoch << '\n'
           << "capability_generation: " << state.capability_generation << '\n'
           << "topology_generation: " << state.topology_generation << '\n'
           << "status: " << StatusName(state.status) << '\n'
           << "error_code: " << state.error_code << '\n'
           << "message: " << state.message << '\n';
    return output.str();
}

std::string RenderControlStatusJson(const ControlState& state) {
    std::ostringstream output;
    output << "{\"source\":\"rules.toml\",\"source_digest\":\""
           << JsonEscape(state.source_digest)
           << "\",\"candidate_sequence\":" << state.candidate_sequence
           << ",\"active_content_generation\":"
           << state.active_content_generation
           << ",\"deployment_epoch\":" << state.deployment_epoch
           << ",\"capability_generation\":" << state.capability_generation
           << ",\"topology_generation\":" << state.topology_generation
           << ",\"status\":\"" << StatusName(state.status)
           << "\",\"error_code\":\"" << JsonEscape(state.error_code)
           << "\",\"message\":\"" << JsonEscape(state.message) << "\"}\n";
    return output.str();
}

bool WriteControlStatus(const fs::path& run_directory,
                        const ControlState& state) {
    std::error_code error;
    fs::create_directories(run_directory, error);
    return !error
        && AtomicWriteText(run_directory / "rules-status.txt",
                           RenderControlStatusText(state))
        && AtomicWriteText(run_directory / "rules-status.json",
                           RenderControlStatusJson(state));
}

Reconciler::Reconciler(fs::path config_directory, fs::path run_directory,
                       rules::RulesLimits limits,
                       rules::DeviceSnapshot snapshot)
    : config_directory_(std::move(config_directory)),
      run_directory_(std::move(run_directory)),
      limits_(limits),
      snapshot_(snapshot) {
    ReadGeneration(run_directory_ / "policy.bin",
                   &state_.active_content_generation);
    if (state_.active_content_generation != 0) {
        const fs::path status = run_directory_ / "rules-status.txt";
        const std::uint64_t recorded_generation = ReadStatusNumber(
            status, "active_content_generation");
        const std::uint64_t recorded_epoch = ReadStatusNumber(
            status, "deployment_epoch");
        state_.deployment_epoch = recorded_generation
                == state_.active_content_generation && recorded_epoch != 0
            ? recorded_epoch : 1;
    }
}

void Reconciler::SetDeviceSnapshot(rules::DeviceSnapshot snapshot) {
    snapshot_ = snapshot;
    device_dirty_ = true;
}

ReconcileResult Reconciler::Reconcile(PublishOptions options) {
    ReconcileResult output;
    SourceLoadResult loaded = LoadRulesSource(config_directory_, limits_);
    if (!loaded.ok()) {
        ++state_.candidate_sequence;
        state_.status = ControlStatus::kSourceInvalid;
        state_.error_code = std::move(loaded.error_code);
        state_.message = std::move(loaded.message)
            + "; new configuration was not activated; previous policy remains active";
        WriteControlStatus(run_directory_, state_);
        output.state = state_;
        return output;
    }
    if (loaded.snapshot->digest == state_.source_digest && !device_dirty_) {
        state_.capability_generation = snapshot_.capability_generation;
        state_.topology_generation = snapshot_.topology_generation;
        WriteControlStatus(run_directory_, state_);
        output.state = state_;
        output.unchanged = true;
        return output;
    }
    state_.source_digest = loaded.snapshot->digest;
    ++state_.candidate_sequence;
    rules::RulesBuildResult built = rules::CompileRules(
        loaded.snapshot->source, limits_);
    output.compiled = true;
    device_dirty_ = false;
    if (!built.ok()) {
        state_.status = ControlStatus::kSourceInvalid;
        if (!built.diagnostics.empty()) {
            state_.error_code = std::string(built.diagnostics.front().code);
            state_.message = rules::RenderDiagnosticText(
                built.diagnostics.front(), loaded.snapshot->source);
        } else {
            state_.error_code = std::string(rules::kCompilerInternal);
            state_.message = "rules compiler failed";
        }
        state_.message.append(
            "; new configuration was not activated; previous policy remains active");
        WriteControlStatus(run_directory_, state_);
        output.state = state_;
        return output;
    }
    const rules::AdmissionResult admission = rules::AdmitPolicy(
        *built.canonical, built.requirements, snapshot_);
    state_.capability_generation = snapshot_.capability_generation;
    state_.topology_generation = snapshot_.topology_generation;
    if (!admission.admitted) {
        state_.status = ControlStatus::kEnvironmentUnsupported;
        state_.error_code = "PG-ADMISSION-UNSUPPORTED";
        state_.message = "device capabilities or topology do not satisfy policy requirements; new configuration was not activated; previous policy remains active";
        WriteControlStatus(run_directory_, state_);
        output.state = state_;
        return output;
    }
    const PublishResult published = Publisher(run_directory_).Publish(
        *built.blob, options);
    if (!published.ok()) {
        state_.status = ControlStatus::kPublishFailed;
        state_.error_code = "PG-PUBLISH-FAILED";
        state_.message = published.message
            + "; new configuration was not activated; previous policy remains active";
        WriteControlStatus(run_directory_, state_);
        output.state = state_;
        return output;
    }
    state_.status = ControlStatus::kActive;
    state_.error_code.clear();
    state_.message = published.published
        ? "policy activated" : "source validated; policy content unchanged";
    state_.active_content_generation = built.blob->content_generation;
    if (published.published) ++state_.deployment_epoch;
    WriteControlStatus(run_directory_, state_);
    output.state = state_;
    output.published = published.published;
    output.unchanged = published.unchanged;
    return output;
}

}  // namespace pathguard::control
