#include "min_heap.h"

namespace w1 {

bool MinHeap::push(const HeapItem& item) noexcept {
    if (size_ == kCapacity) return false;
    std::size_t i = size_++;
    while (i > 0) {                              // sift up
        const std::size_t parent = (i - 1) / 2;
        if (!heapLess(item, items_[parent])) break;
        items_[i] = items_[parent];
        i = parent;
    }
    items_[i] = item;
    return true;
}

HeapItem MinHeap::pop() noexcept {
    const HeapItem result = items_[0];
    const HeapItem last = items_[--size_];
    if (size_ > 0) {
        std::size_t i = 0;
        for (;;) {                               // sift down
            std::size_t child = 2 * i + 1;
            if (child >= size_) break;
            if (child + 1 < size_ && heapLess(items_[child + 1], items_[child])) ++child;
            if (!heapLess(items_[child], last)) break;
            items_[i] = items_[child];
            i = child;
        }
        items_[i] = last;
    }
    return result;
}

}  // namespace w1
