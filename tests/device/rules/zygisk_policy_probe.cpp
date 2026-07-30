#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pathguard/policy_format.h"
#include "pathguard/policy_v6_view.h"

int main(int argc, char** argv) {
    if (argc != 5 && argc != 7) return 2;
    const int fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat metadata {};
    if (fd < 0 || fstat(fd, &metadata) != 0
        || metadata.st_size
            < static_cast<off_t>(pathguard::binary_format::kHeaderSize)) {
        if (fd >= 0) close(fd);
        return 3;
    }
    const std::size_t size = static_cast<std::size_t>(metadata.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) return 4;
    const auto* data = static_cast<const std::uint8_t*>(mapping);
    pathguard::policy_v6_view::PolicyV6View policy;
    pathguard::policy_v6_view::PackageRef package;
    bool plan_ok = policy.Initialize(data, size)
        && policy.FindPackage(argv[2], std::strlen(argv[2]), &package);
    bool redirect_found = false;
    bool first_deny_found = argc != 7;
    bool second_deny_found = argc != 7;
    std::uint32_t mount_count = 0;
    if (plan_ok) {
        for (std::uint32_t index = 0; index < package.action_count; ++index) {
            pathguard::policy_v6_view::ActionRef action;
            if (!policy.ActionAt(package.first_action + index, &action)) {
                plan_ok = false;
                break;
            }
            if (action.domain != 0) {
                continue;
            }
            ++mount_count;
            pathguard::policy_v6_view::SelectorRef selector;
            pathguard::policy_v6_view::StringRef root;
            pathguard::policy_v6_view::StringRef target;
            if (!policy.SelectorAt(action.selector_id, &selector)
                || !policy.StringAt(selector.root_id, &root)
                || selector.match_kind != 0) {
                plan_ok = false;
                break;
            }
            if (action.kind == 1
                && policy.StringAt(action.target_id, &target)
                && root.Equals(argv[3], std::strlen(argv[3]))
                && target.Equals(argv[4], std::strlen(argv[4]))) {
                redirect_found = true;
            } else if (action.kind == 0 && argc == 7
                       && root.Equals(argv[5], std::strlen(argv[5]))) {
                first_deny_found = true;
            } else if (action.kind == 0 && argc == 7
                       && root.Equals(argv[6], std::strlen(argv[6]))) {
                second_deny_found = true;
            } else {
                plan_ok = false;
            }
        }
        plan_ok = plan_ok && mount_count == (argc == 7 ? 3U : 1U)
            && redirect_found && first_deny_found && second_deny_found;
    }
    if (plan_ok) {
        std::printf("generation=%llu package=%s redirect=%s->%s denies=%u\n",
                    static_cast<unsigned long long>(policy.content_generation()),
                    argv[2], argv[3], argv[4], argc == 7 ? 2U : 0U);
    }
    munmap(mapping, size);
    return plan_ok ? 0 : 5;
}
