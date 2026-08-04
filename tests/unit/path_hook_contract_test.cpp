#include <fcntl.h>

#include "pathguard/path_hook_contract.h"
#include "test_assert.h"

int main() {
    using namespace pathguard;
    using namespace pathguard::path_hook_contract;
    assert(OperationFor(Api::kLookupStat) == kOperationLookupStat);
    assert(OperationFor(Api::kRename) == kOperationRename);
    assert(OpenOperation(O_RDONLY) == kOperationOpenRead);
    assert(OpenOperation(O_WRONLY) == kOperationOpenWrite);
    assert(OpenOperation(O_RDWR | O_CREAT)
           == (kOperationOpenWrite | kOperationCreate));

    storage_path_adapter::RewriteResult pass;
    auto first = PlanOperand("/source/a", nullptr, pass);
    assert(first.disposition == Disposition::kPass);

    storage_path_adapter::RewriteResult deny;
    deny.disposition = storage_path_adapter::RewriteDisposition::kDeny;
    first = PlanOperand("/source/a", nullptr, deny);
    assert(first.disposition == Disposition::kReject);
    assert(first.error_number == EACCES);

    storage_path_adapter::RewriteResult redirect;
    redirect.disposition = storage_path_adapter::RewriteDisposition::kRedirect;
    redirect.reverse_mode = 1;
    assert(PlanCanonicalPath(redirect)
           == CanonicalPathDisposition::kStaticLogicalPath);
    redirect.reverse_mode = 2;
    assert(PlanCanonicalPath(redirect)
           == CanonicalPathDisposition::kProvenanceLookup);
    assert(!ShouldCoordinateProvenanceMutation(
        redirect, kOperationCreate | kOperationOpenWrite));
    assert(ShouldCoordinateProvenanceMutation(
        redirect, kOperationCreate | kOperationOpenWrite
            | kOperationReverseMapping));
    const int provider_create_flags = O_RDWR | O_CREAT;
    const RedirectOpenPlan forward_create = PlanRedirectOpen(
        redirect, kOperationCreate | kOperationOpenWrite,
        provider_create_flags);
    const RedirectOpenPlan fuse_reopen = PlanRedirectOpen(
        redirect, kOperationCreate | kOperationOpenWrite,
        provider_create_flags);
    assert(!forward_create.coordinate_provenance);
    assert(!fuse_reopen.coordinate_provenance);
    assert(forward_create.effective_flags == provider_create_flags);
    assert(fuse_reopen.effective_flags == provider_create_flags);
    assert((forward_create.effective_flags & O_EXCL) == 0);
    const RedirectOpenPlan admitted_create = PlanRedirectOpen(
        redirect,
        kOperationCreate | kOperationOpenWrite | kOperationReverseMapping,
        provider_create_flags);
    assert(admitted_create.coordinate_provenance);
    redirect.reverse_mode = 0;
    assert(PlanCanonicalPath(redirect)
           == CanonicalPathDisposition::kStaticLogicalPath);
    redirect.reverse_mode = 1;
    first = PlanOperand("/source/a", "/target/a", redirect);
    auto second = PlanOperand("/source/b", "/target/b", redirect);
    auto rename = PlanRename(first, second);
    assert(rename.disposition == Disposition::kCall);
    assert(rename.first == first.path && rename.second == second.path);

    second = PlanOperand("/outside/b", nullptr, pass);
    rename = PlanRename(first, second);
    assert(rename.disposition == Disposition::kReject);
    assert(rename.error_number == EXDEV);

    second = PlanOperand("/source/b", nullptr, deny);
    rename = PlanRename(first, second);
    assert(rename.disposition == Disposition::kReject);
    assert(rename.error_number == EACCES);
    return 0;
}
