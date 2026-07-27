#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pathguard/policy_format.h"
#include "pathguard/policy_index.h"

namespace {

const char* PolicyString(const std::uint8_t* data, std::size_t size,
                         std::uint32_t string_offset,
                         std::uint32_t relative) {
    if (string_offset > size || relative >= size - string_offset) return nullptr;
    const char* value = reinterpret_cast<const char*>(
        data + string_offset + relative);
    const std::size_t remaining = size - string_offset - relative;
    return std::memchr(value, '\0', remaining) == nullptr ? nullptr : value;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace pathguard::binary_format;
    if (argc != 5 && argc != 7) return 2;
    const int fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat metadata {};
    if (fd < 0 || fstat(fd, &metadata) != 0
        || metadata.st_size < static_cast<off_t>(kHeaderSize)) return 3;
    const std::size_t size = static_cast<std::size_t>(metadata.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) return 4;
    const auto* data = static_cast<const std::uint8_t*>(mapping);
    const std::uint32_t package_count = ReadLe32(data + kPackageCountOffset);
    const std::uint32_t mount_count = ReadLe32(data + kMountRuleCountOffset);
    const std::uint32_t package_offset = ReadLe32(data + kPackageTableOffset);
    const std::uint32_t mount_offset = ReadLe32(data + kMountRuleTableOffset);
    const std::uint32_t string_offset = ReadLe32(data + kStringTableOffset);
    const bool header_ok = ReadLe32(data) == kMagic
        && ReadLe32(data + kFileSizeOffset) == size
        && package_offset == kHeaderSize
        && mount_offset == package_offset + package_count * kPackageSize
        && string_offset >= mount_offset + mount_count * kMountRuleSize
        && string_offset < size
        && Crc32(data + kHeaderSize, size - kHeaderSize)
            == ReadLe32(data + kPayloadChecksumOffset);
    const PolicyIndexView index{data, size, package_count, package_offset,
                                mount_offset, string_offset};
    const std::size_t package_length = std::strlen(argv[2]);
    const std::uint8_t* package = header_ok
        ? FindPackageEntry(index, argv[2], package_length) : nullptr;
    bool plan_ok = package != nullptr;
    if (plan_ok) {
        const std::uint32_t first = ReadLe32(package + kPackageFirstMountOffset);
        const std::uint32_t count = ReadLe32(package + kPackageMountCountOffset);
        plan_ok = count == (argc == 7 ? 3U : 1U)
            && first <= mount_count && count <= mount_count - first;
        bool redirect_found = false;
        bool first_deny_found = argc != 7;
        bool second_deny_found = argc != 7;
        for (std::uint32_t index = 0; plan_ok && index < count; ++index) {
            const std::uint8_t* rule = data + mount_offset
                + (first + index) * kMountRuleSize;
            const char* visible = PolicyString(
                data, size, string_offset,
                ReadLe32(rule + kMountVisiblePathOffset));
            const char* backing = PolicyString(
                data, size, string_offset,
                ReadLe32(rule + kMountBackingPathOffset));
            plan_ok = visible != nullptr && backing != nullptr;
            if (!plan_ok) break;
            if (rule[kMountActionOffset] == 1
                && std::strcmp(visible, argv[3]) == 0
                && std::strcmp(backing, argv[4]) == 0) {
                redirect_found = true;
            } else if (rule[kMountActionOffset] == 0
                       && backing[0] == '\0' && argc == 7
                       && std::strcmp(visible, argv[5]) == 0) {
                first_deny_found = true;
            } else if (rule[kMountActionOffset] == 0
                       && backing[0] == '\0' && argc == 7
                       && std::strcmp(visible, argv[6]) == 0) {
                second_deny_found = true;
            } else {
                plan_ok = false;
            }
        }
        plan_ok = plan_ok && redirect_found
            && first_deny_found && second_deny_found;
    }
    if (plan_ok) {
        std::printf("generation=%llu package=%s redirect=%s->%s denies=%u\n",
                    static_cast<unsigned long long>(
                        ReadLe64(data + kContentGenerationOffset)),
                    argv[2], argv[3], argv[4], argc == 7 ? 2U : 0U);
    }
    munmap(mapping, size);
    return plan_ok ? 0 : 5;
}
