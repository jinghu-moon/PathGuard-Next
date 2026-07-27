#pragma once

#include <stddef.h>

namespace pathguard {

template <typename Entry, size_t Capacity>
class MutationJournal final {
public:
    bool Push(const Entry& entry) {
        if (size_ >= Capacity) return false;
        entries_[size_++] = entry;
        return true;
    }

    bool Pop(Entry* entry) {
        if (entry == nullptr || size_ == 0) return false;
        *entry = entries_[--size_];
        return true;
    }

    bool UpdateLast(const Entry& entry) {
        if (size_ == 0) return false;
        entries_[size_ - 1] = entry;
        return true;
    }

    bool UpdateAt(size_t index, const Entry& entry) {
        if (index >= size_) return false;
        entries_[index] = entry;
        return true;
    }

    const Entry* At(size_t index) const {
        return index < size_ ? &entries_[index] : nullptr;
    }

    void Clear() { size_ = 0; }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

private:
    Entry entries_[Capacity]{};
    size_t size_ = 0;
};

}  // namespace pathguard
