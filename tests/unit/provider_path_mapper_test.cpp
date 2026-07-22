#include <cstring>

#include "pathguard/provider_path_mapper.h"
#include "test_assert.h"

using pathguard::provider_redirect::PathRule;
using pathguard::provider_redirect::RewriteAbsolutePath;
using pathguard::provider_redirect::RestoreAbsolutePath;

int main() {
    PathRule rules[2]{};
    rules[0].caller_uid = 10382;
    rules[0].user_id = 0;
    std::strcpy(rules[0].visible_path, "Download/localsend-source");
    std::strcpy(rules[0].backing_path, "Download/localsend-redirect");
    rules[1].caller_uid = 1010382;
    rules[1].user_id = 10;
    std::strcpy(rules[1].visible_path, "Download/source");
    std::strcpy(rules[1].backing_path, "Download/target");

    char output[PATH_MAX]{};
    assert(RewriteAbsolutePath(rules, 2, 10382,
        "/storage/emulated/0/Download/localsend-source", output, sizeof(output)));
    assert(std::strcmp(output,
        "/storage/emulated/0/Download/localsend-redirect") == 0);

    assert(RewriteAbsolutePath(rules, 2, 10382,
        "/mnt/user/0/emulated/0/Download/localsend-source/a/b.txt",
        output, sizeof(output)));
    assert(std::strcmp(output,
        "/mnt/user/0/emulated/0/Download/localsend-redirect/a/b.txt") == 0);

    assert(RewriteAbsolutePath(rules, 2, 10382,
        "/data/media/0/Download/localsend-source/file", output, sizeof(output)));
    assert(std::strcmp(output,
        "/data/media/0/Download/localsend-redirect/file") == 0);

    assert(!RewriteAbsolutePath(rules, 2, 10383,
        "/storage/emulated/0/Download/localsend-source/file", output, sizeof(output)));
    assert(!RewriteAbsolutePath(rules, 2, 10382,
        "/storage/emulated/0/Download/localsend-source-other/file", output, sizeof(output)));
    assert(!RewriteAbsolutePath(rules, 2, 10382,
        "/storage/emulated/0/Download/localsend-redirect/file", output, sizeof(output)));
    assert(!RewriteAbsolutePath(rules, 2, 10382,
        "/storage/emulated/0/Download/localsend-source/../escape", output, sizeof(output)));
    assert(!RewriteAbsolutePath(rules, 2, 1010382,
        "/storage/emulated/0/Download/source/file", output, sizeof(output)));
    assert(RewriteAbsolutePath(rules, 2, 1010382,
        "/storage/emulated/10/Download/source/file", output, sizeof(output)));
    assert(std::strcmp(output,
        "/storage/emulated/10/Download/target/file") == 0);

    PathRule media_rule = rules[0];
    media_rule.caller_uid = -1;
    assert(RewriteAbsolutePath(&media_rule, 1, -1,
        "/storage/emulated/0/Download/localsend-source/media.jpg",
        output, sizeof(output)));
    assert(std::strcmp(output,
        "/storage/emulated/0/Download/localsend-redirect/media.jpg") == 0);
    assert(RestoreAbsolutePath(&media_rule, 1, -1,
        "/storage/emulated/0/Download/localsend-redirect/media.jpg",
        output, sizeof(output)));
    assert(std::strcmp(output,
        "/storage/emulated/0/Download/localsend-source/media.jpg") == 0);
    assert(!RestoreAbsolutePath(&media_rule, 1, -1,
        "/storage/emulated/0/Download/other/media.jpg", output, sizeof(output)));

    char small[8]{};
    assert(!RewriteAbsolutePath(rules, 2, 10382,
        "/storage/emulated/0/Download/localsend-source/file", small, sizeof(small)));

    PathRule nested[2]{};
    nested[0] = rules[0];
    nested[1].caller_uid = 10382;
    nested[1].user_id = 0;
    std::strcpy(nested[1].visible_path, "Download/localsend-source/private");
    std::strcpy(nested[1].backing_path, "Vault/private");
    assert(RewriteAbsolutePath(nested, 2, 10382,
        "/storage/emulated/0/Download/localsend-source/private/a", output, sizeof(output)));
    assert(std::strcmp(output, "/storage/emulated/0/Vault/private/a") == 0);
    return 0;
}
