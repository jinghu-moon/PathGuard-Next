#include "pathguard/mount_info_snapshot.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

namespace pathguard {
namespace {

constexpr size_t kMaxMountInfoBytes = 2u * 1024u * 1024u;
constexpr size_t kMaxMountInfoEntries = 4096u;

uint64_t NowNs() {
    struct timespec value {};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<uint64_t>(value.tv_sec) * 1000000000ull
        + static_cast<uint64_t>(value.tv_nsec);
}

size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

bool ParseUnsigned(const char* text, uint64_t* value) {
    if (text == nullptr || value == nullptr || text[0] == '\0') return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseDevice(const char* text, dev_t* device) {
    if (text == nullptr || device == nullptr) return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long major_value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != ':') return false;
    const char* minor_text = end + 1;
    errno = 0;
    const unsigned long minor_value = strtoul(minor_text, &end, 10);
    if (errno != 0 || end == minor_text || *end != '\0') return false;
    *device = makedev(static_cast<unsigned>(major_value),
                      static_cast<unsigned>(minor_value));
    return true;
}

char* NextToken(char** cursor, char* end) {
    if (cursor == nullptr || *cursor == nullptr) return nullptr;
    char* current = *cursor;
    while (current < end && *current == ' ') ++current;
    if (current >= end) {
        *cursor = end;
        return nullptr;
    }
    char* token = current;
    while (current < end && *current != ' ') ++current;
    if (current < end) *current++ = '\0';
    *cursor = current;
    return token;
}

bool DecodeToken(const char* begin, size_t length,
                 char* output, size_t capacity) {
    if (begin == nullptr || output == nullptr || capacity == 0) return false;
    size_t written = 0;
    for (size_t index = 0; index < length; ++index) {
        unsigned char value = static_cast<unsigned char>(begin[index]);
        if (value == '\\') {
            if (index + 3 >= length
                || begin[index + 1] < '0' || begin[index + 1] > '7'
                || begin[index + 2] < '0' || begin[index + 2] > '7'
                || begin[index + 3] < '0' || begin[index + 3] > '7') {
                return false;
            }
            value = static_cast<unsigned char>(
                ((begin[index + 1] - '0') << 6)
                | ((begin[index + 2] - '0') << 3)
                | (begin[index + 3] - '0'));
            if (value == 0) return false;
            index += 3;
        }
        if (written + 1 >= capacity) return false;
        output[written++] = static_cast<char>(value);
    }
    output[written] = '\0';
    return true;
}

bool IsPathOnMount(const char* path, const char* mountpoint) {
    if (path == nullptr || mountpoint == nullptr || path[0] != '/') return false;
    if (strcmp(mountpoint, "/") == 0) return true;
    const size_t length = strlen(mountpoint);
    return strncmp(path, mountpoint, length) == 0
        && (path[length] == '\0' || path[length] == '/');
}

int ParseSnapshot(MountInfoSnapshot* snapshot) {
    const uint64_t started = NowNs();
    char* cursor = snapshot->text;
    char* const text_end = snapshot->text + snapshot->text_size;
    while (cursor < text_end) {
        char* line = cursor;
        char* newline = static_cast<char*>(
            memchr(cursor, '\n', static_cast<size_t>(text_end - cursor)));
        char* line_end = newline != nullptr ? newline : text_end;
        cursor = newline != nullptr ? newline + 1 : text_end;
        if (line == line_end) continue;
        if (snapshot->entry_count >= kMaxMountInfoEntries) return EOVERFLOW;
        if (newline != nullptr) *newline = '\0';

        char* field_cursor = line;
        char* mount_id_text = NextToken(&field_cursor, line_end);
        char* parent_id_text = NextToken(&field_cursor, line_end);
        char* device_text = NextToken(&field_cursor, line_end);
        char* root_text = NextToken(&field_cursor, line_end);
        char* mountpoint_text = NextToken(&field_cursor, line_end);
        char* mount_options = NextToken(&field_cursor, line_end);
        if (mount_id_text == nullptr || parent_id_text == nullptr
            || device_text == nullptr || root_text == nullptr
            || mountpoint_text == nullptr || mount_options == nullptr) {
            return EBADMSG;
        }

        uint8_t propagation = kMountPropagationNone;
        char* separator = nullptr;
        while (char* optional = NextToken(&field_cursor, line_end)) {
            if (strcmp(optional, "-") == 0) {
                separator = optional;
                break;
            }
            if (strncmp(optional, "shared:", 7) == 0) {
                propagation |= kMountPropagationShared;
            } else if (strncmp(optional, "master:", 7) == 0) {
                propagation |= kMountPropagationMaster;
            } else if (strcmp(optional, "unbindable") == 0) {
                propagation |= kMountPropagationUnbindable;
            }
        }
        if (separator == nullptr) return EBADMSG;
        char* filesystem_text = NextToken(&field_cursor, line_end);
        char* source_text = NextToken(&field_cursor, line_end);
        char* super_options = NextToken(&field_cursor, line_end);
        if (filesystem_text == nullptr || source_text == nullptr
            || super_options == nullptr) {
            return EBADMSG;
        }

        MountInfoSnapshotEntry entry;
        if (!ParseUnsigned(mount_id_text, &entry.mount_id)
            || !ParseUnsigned(parent_id_text, &entry.parent_mount_id)
            || entry.mount_id == 0 || entry.parent_mount_id == 0
            || !ParseDevice(device_text, &entry.device)) {
            return EBADMSG;
        }
        entry.root_offset = static_cast<uint32_t>(root_text - snapshot->text);
        entry.root_length = static_cast<uint32_t>(strlen(root_text));
        entry.mountpoint_offset = static_cast<uint32_t>(
            mountpoint_text - snapshot->text);
        entry.mountpoint_length = static_cast<uint32_t>(strlen(mountpoint_text));
        entry.filesystem_offset = static_cast<uint32_t>(
            filesystem_text - snapshot->text);
        entry.filesystem_length = static_cast<uint32_t>(strlen(filesystem_text));
        entry.propagation = propagation;

        char decoded[PATH_MAX]{};
        if (!DecodeToken(root_text, entry.root_length, decoded, sizeof(decoded))
            || !DecodeToken(mountpoint_text, entry.mountpoint_length,
                            decoded, sizeof(decoded))
            || entry.filesystem_length == 0
            || entry.filesystem_length >= sizeof(MountPathIdentity::filesystem)) {
            return EBADMSG;
        }
        snapshot->entries[snapshot->entry_count++] = entry;
    }
    snapshot->parse_ns = NowNs() - started;
    return snapshot->entry_count == 0 ? ENODATA : 0;
}

int FindEntry(const MountInfoSnapshot& snapshot, const char* absolute_path,
              bool exact_mountpoint, size_t* found_index) {
    if (snapshot.mapping == nullptr || snapshot.text == nullptr
        || snapshot.entries == nullptr || absolute_path == nullptr
        || absolute_path[0] != '/' || found_index == nullptr) {
        return EINVAL;
    }
    bool found = false;
    size_t selected = 0;
    size_t selected_length = 0;
    for (size_t index = 0; index < snapshot.entry_count; ++index) {
        const MountInfoSnapshotEntry& entry = snapshot.entries[index];
        char mountpoint[PATH_MAX]{};
        if (!DecodeToken(snapshot.text + entry.mountpoint_offset,
                         entry.mountpoint_length, mountpoint,
                         sizeof(mountpoint))) {
            return EBADMSG;
        }
        const bool matches = exact_mountpoint
            ? strcmp(mountpoint, absolute_path) == 0
            : IsPathOnMount(absolute_path, mountpoint);
        const size_t length = strlen(mountpoint);
        if (!matches || (!exact_mountpoint && found
                         && length < selected_length)) {
            continue;
        }
        found = true;
        selected = index;
        selected_length = length;
    }
    if (!found) return ENOENT;
    *found_index = selected;
    return 0;
}

int ReadNamespaceIdentity(dev_t* device, ino_t* inode) {
    const int fd = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return errno;
    struct stat value {};
    const int result = fstat(fd, &value);
    const int error = result == 0 ? 0 : errno;
    close(fd);
    if (error != 0) return error;
    *device = value.st_dev;
    *inode = value.st_ino;
    return 0;
}

}  // namespace

int CaptureMountInfoSnapshot(MountInfoSnapshot* snapshot) {
    if (snapshot == nullptr) return EINVAL;
    DestroyMountInfoSnapshot(snapshot);
    const size_t text_capacity = kMaxMountInfoBytes + 1u;
    const size_t entries_offset = AlignUp(text_capacity, alignof(MountInfoSnapshotEntry));
    const size_t mapping_size = entries_offset
        + sizeof(MountInfoSnapshotEntry) * kMaxMountInfoEntries;
    void* mapping = mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return errno;
    snapshot->mapping = mapping;
    snapshot->mapping_size = mapping_size;
    snapshot->text = static_cast<char*>(mapping);
    snapshot->entries = reinterpret_cast<MountInfoSnapshotEntry*>(
        snapshot->text + entries_offset);

    int error = ReadNamespaceIdentity(
        &snapshot->namespace_device, &snapshot->namespace_inode);
    if (error != 0) {
        DestroyMountInfoSnapshot(snapshot);
        return error;
    }
    const int fd = open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        error = errno;
        DestroyMountInfoSnapshot(snapshot);
        return error;
    }
    const uint64_t started = NowNs();
    while (snapshot->text_size < kMaxMountInfoBytes) {
        const ssize_t received = read(
            fd, snapshot->text + snapshot->text_size,
            kMaxMountInfoBytes - snapshot->text_size);
        if (received < 0) {
            if (errno == EINTR) continue;
            error = errno;
            break;
        }
        if (received == 0) break;
        snapshot->text_size += static_cast<size_t>(received);
    }
    if (error == 0 && snapshot->text_size == kMaxMountInfoBytes) {
        char extra = 0;
        ssize_t received = 0;
        do {
            received = read(fd, &extra, 1);
        } while (received < 0 && errno == EINTR);
        if (received > 0) error = EFBIG;
        else if (received < 0) error = errno;
    }
    close(fd);
    snapshot->read_ns = NowNs() - started;
    if (error == 0) {
        snapshot->text[snapshot->text_size] = '\0';
        error = ParseSnapshot(snapshot);
    }
    if (error != 0) DestroyMountInfoSnapshot(snapshot);
    return error;
}

void DestroyMountInfoSnapshot(MountInfoSnapshot* snapshot) {
    if (snapshot == nullptr) return;
    if (snapshot->mapping != nullptr && snapshot->mapping != MAP_FAILED
        && snapshot->mapping_size != 0) {
        munmap(snapshot->mapping, snapshot->mapping_size);
    }
    *snapshot = {};
}

int FindMountInfoPath(const MountInfoSnapshot& snapshot,
                      const char* absolute_path, bool exact_mountpoint,
                      MountPathIdentity* identity) {
    if (identity == nullptr) return EINVAL;
    size_t index = 0;
    const int error = FindEntry(
        snapshot, absolute_path, exact_mountpoint, &index);
    if (error != 0) return error;
    const MountInfoSnapshotEntry& entry = snapshot.entries[index];
    MountPathIdentity value;
    value.mount_id = entry.mount_id;
    value.parent_mount_id = entry.parent_mount_id;
    value.device = entry.device;
    if (!DecodeToken(snapshot.text + entry.root_offset, entry.root_length,
                     value.root, sizeof(value.root))
        || !DecodeToken(snapshot.text + entry.mountpoint_offset,
                        entry.mountpoint_length, value.mountpoint,
                        sizeof(value.mountpoint))
        || entry.filesystem_length >= sizeof(value.filesystem)) {
        return EBADMSG;
    }
    memcpy(value.filesystem, snapshot.text + entry.filesystem_offset,
           entry.filesystem_length);
    value.filesystem[entry.filesystem_length] = '\0';
    *identity = value;
    return 0;
}

int MountInfoSnapshotRequiresPrivate(const MountInfoSnapshot& snapshot,
                                     const char* mountpoint,
                                     bool* requires_private) {
    if (requires_private == nullptr) return EINVAL;
    size_t index = 0;
    const int error = FindEntry(snapshot, mountpoint, true, &index);
    if (error != 0) return error;
    const uint8_t propagation = snapshot.entries[index].propagation;
    *requires_private = (propagation & kMountPropagationShared) != 0
        && (propagation & kMountPropagationMaster) == 0;
    return 0;
}

bool MountInfoSnapshotMatchesCurrentNamespace(
    const MountInfoSnapshot& snapshot) {
    dev_t device = 0;
    ino_t inode = 0;
    return snapshot.mapping != nullptr
        && ReadNamespaceIdentity(&device, &inode) == 0
        && device == snapshot.namespace_device
        && inode == snapshot.namespace_inode;
}

}  // namespace pathguard
