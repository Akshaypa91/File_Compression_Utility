// min_heap.h - the min-heap that drives the greedy Huffman construction.
//
// A fixed-size binary heap on a plain array (no allocation, no pointers). Items are ordered
// by (weight, id), so equal weights always break the same way on every compiler and platform.
// That makes the generated tree, and therefore the whole compressed file, reproducible.
#pragma once

#include <cstddef>
#include <cstdint>

using namespace std;

namespace w1 {

struct HeapItem {
    uint64_t weight;
    uint32_t id;
};

// strict ordering: smaller weight first, then smaller id
inline bool heapLess(const HeapItem& a, const HeapItem& b) noexcept {
    return a.weight < b.weight || (a.weight == b.weight && a.id < b.id);
}

class MinHeap {
public:
    // Large enough for the biggest alphabet (286 leaves); the heap never holds more leaves
    // than that because each merge removes two items and adds one.
    static constexpr size_t kCapacity = 320;

    bool empty() const noexcept { return size_ == 0; }
    size_t size() const noexcept { return size_; }
    const HeapItem& top() const noexcept { return items_[0]; }

    bool push(const HeapItem& item) noexcept;   // false if the heap is full
    HeapItem pop() noexcept;                    // removes and returns the smallest; heap must not be empty

private:
    HeapItem items_[kCapacity];
    size_t size_ = 0;
};

}  // namespace w1
