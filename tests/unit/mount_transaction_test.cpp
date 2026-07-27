#include <atomic>
#include <thread>
#include <vector>

#include "pathguard/mutation_journal.h"
#include "pathguard/mount_transaction.h"
#include "test_assert.h"

namespace {

using pathguard::MountTransactionState;
using pathguard::MountPreflightPhase;

std::uint32_t Value(MountTransactionState state) {
    return static_cast<std::uint32_t>(state);
}

bool Transition(std::atomic<std::uint32_t>* state, MountTransactionState from,
                MountTransactionState to) {
    assert(pathguard::IsMountTransitionAllowed(from, to));
    std::uint32_t expected = Value(from);
    return state->compare_exchange_strong(
        expected, Value(to), std::memory_order_acq_rel, std::memory_order_acquire);
}

}  // namespace

int main() {
    static_assert(!pathguard::ShouldExtendPendingMountWait(
        MountTransactionState::kPending, MountPreflightPhase::kIdle, false));
    static_assert(pathguard::ShouldExtendPendingMountWait(
        MountTransactionState::kPending,
        MountPreflightPhase::kCapabilityProbe, false));
    static_assert(!pathguard::ShouldExtendPendingMountWait(
        MountTransactionState::kPending,
        MountPreflightPhase::kCapabilityProbe, true));
    static_assert(!pathguard::ShouldExtendPendingMountWait(
        MountTransactionState::kApplying,
        MountPreflightPhase::kCapabilityProbe, false));
    static_assert(!pathguard::ShouldExtendPendingMountWait(
        MountTransactionState::kComplete,
        MountPreflightPhase::kCapabilityProbe, false));

    static_assert(pathguard::IsMountTransitionAllowed(
        MountTransactionState::kPending, MountTransactionState::kApplying));
    static_assert(pathguard::IsMountTransitionAllowed(
        MountTransactionState::kApplying,
        MountTransactionState::kCancelRequested));
    static_assert(pathguard::IsMountTransitionAllowed(
        MountTransactionState::kCancelRequested,
        MountTransactionState::kRollbackComplete));
    static_assert(pathguard::IsMountTransitionAllowed(
        MountTransactionState::kApplying,
        MountTransactionState::kRollbackComplete));
    static_assert(!pathguard::IsMountTransitionAllowed(
        MountTransactionState::kCancelRequested,
        MountTransactionState::kComplete));
    static_assert(!pathguard::IsMountTransitionAllowed(
        MountTransactionState::kComplete, MountTransactionState::kApplying));
    static_assert(!pathguard::IsMountTransitionAllowed(
        MountTransactionState::kApplying, MountTransactionState::kFailed));
    static_assert(pathguard::IsMountTransitionAllowed(
        MountTransactionState::kApplying,
        MountTransactionState::kNamespaceTainted));
    static_assert(pathguard::IsMountTransitionAllowed(
        MountTransactionState::kCancelRequested,
        MountTransactionState::kNamespaceTainted));
    static_assert(!pathguard::IsMountTransitionAllowed(
        MountTransactionState::kNamespaceTainted,
        MountTransactionState::kRollbackComplete));
    static_assert(pathguard::IsMountTransactionTerminal(
        MountTransactionState::kNamespaceTainted));

    for (int iteration = 0; iteration < 10000; ++iteration) {
        std::atomic<std::uint32_t> state{Value(MountTransactionState::kPending)};
        bool lease_acquired = false;
        bool pre_mutation_cancelled = false;
        std::thread helper([&] {
            lease_acquired = Transition(&state, MountTransactionState::kPending,
                                        MountTransactionState::kApplying);
        });
        std::thread app([&] {
            pre_mutation_cancelled = Transition(
                &state, MountTransactionState::kPending,
                MountTransactionState::kCancelRequested);
        });
        helper.join();
        app.join();
        assert(lease_acquired != pre_mutation_cancelled);
        if (pre_mutation_cancelled) {
            assert(!Transition(&state, MountTransactionState::kPending,
                               MountTransactionState::kApplying));
            assert(Transition(&state, MountTransactionState::kCancelRequested,
                              MountTransactionState::kCancelled));
        } else {
            assert(Transition(&state, MountTransactionState::kApplying,
                              MountTransactionState::kCancelRequested));
            assert(Transition(&state, MountTransactionState::kCancelRequested,
                              MountTransactionState::kRollbackComplete));
        }
        assert(pathguard::IsMountTransactionTerminal(
            static_cast<MountTransactionState>(state.load())));
    }

    std::atomic<std::uint32_t> success{Value(MountTransactionState::kPending)};
    assert(Transition(&success, MountTransactionState::kPending,
                      MountTransactionState::kApplying));
    assert(Transition(&success, MountTransactionState::kApplying,
                      MountTransactionState::kComplete));
    assert(pathguard::MountTransactionHasResult(
        static_cast<MountTransactionState>(success.load())));

    std::atomic<std::uint32_t> preflight_failure{
        Value(MountTransactionState::kPending)};
    assert(Transition(&preflight_failure, MountTransactionState::kPending,
                      MountTransactionState::kFailed));
    assert(pathguard::MountTransactionHasResult(
        static_cast<MountTransactionState>(preflight_failure.load())));

    struct FakeMutation { int operation = -1; };
    for (int failure = 0; failure < 4; ++failure) {
        pathguard::MutationJournal<FakeMutation, 4> journal;
        for (int operation = 0; operation < 4; ++operation) {
            assert(journal.Push({operation}));
            if (operation == failure) break;  // mount happened, verify failed.
        }
        std::vector<int> rolled_back;
        FakeMutation mutation;
        while (journal.Pop(&mutation)) rolled_back.push_back(mutation.operation);
        assert(rolled_back.size() == static_cast<std::size_t>(failure + 1));
        for (int index = 0; index <= failure; ++index) {
            assert(rolled_back[index] == failure - index);
        }
        assert(journal.empty());
    }

    pathguard::MutationJournal<FakeMutation, 1> bounded;
    assert(bounded.Push({1}));
    assert(bounded.UpdateLast({7}));
    assert(bounded.At(0) != nullptr);
    assert(bounded.At(0)->operation == 7);
    assert(bounded.At(1) == nullptr);
    assert(!bounded.Push({2}));
    bounded.Clear();
    assert(bounded.empty());
    assert(!bounded.UpdateLast({9}));
    return 0;
}
