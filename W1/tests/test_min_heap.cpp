// test_min_heap.cpp - unit, white-box and black-box tests for min_heap.h / min_heap.cpp.
//
//   Black-box: the comparator's mathematical properties, push/pop contracts, capacity
//              boundaries, extreme values, and a random comparison against a reference
//              (std::multiset ordered by heapLess) used as the oracle.
//   White-box: hand-built cases that force every branch of push() (full / sift-up stops /
//              sift-up moves) and pop() (last element / left child only / right child
//              smaller / stop early / keep sifting).
//
// Not tested on purpose: pop() on an EMPTY heap. The header says "heap must not be empty";
// doing it would underflow size_ (undefined behaviour), so it is a caller precondition.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <vector>

#include "min_heap.h"
#include "test_util.h"

using w1::HeapItem;
using w1::MinHeap;

static HeapItem mk(std::uint64_t w, std::uint32_t id) { return HeapItem{w, id}; }

static std::vector<HeapItem> drain(MinHeap& h) {
    std::vector<HeapItem> out;
    while (!h.empty()) out.push_back(h.pop());
    return out;
}

static bool sameItem(const HeapItem& a, const HeapItem& b) { return a.weight == b.weight && a.id == b.id; }

// ---------- black-box: the comparator ----------------------------------------------------
static void testHeapLessProperties() {
    const std::uint64_t W[] = {0, 1, 2, std::numeric_limits<std::uint64_t>::max()};
    const std::uint32_t I[] = {0, 1, 2, std::numeric_limits<std::uint32_t>::max()};
    std::vector<HeapItem> items;
    for (auto w : W) for (auto i : I) items.push_back(mk(w, i));

    for (const auto& a : items) {
        CHECK(!w1::heapLess(a, a));                                   // irreflexive
        for (const auto& b : items) {
            if (w1::heapLess(a, b)) CHECK(!w1::heapLess(b, a));       // asymmetric
            if (!sameItem(a, b)) CHECK(w1::heapLess(a, b) != w1::heapLess(b, a));   // total: exactly one way
            for (const auto& c : items) {
                if (w1::heapLess(a, b) && w1::heapLess(b, c)) CHECK(w1::heapLess(a, c));   // transitive
            }
        }
    }
    CHECK(w1::heapLess(mk(1, 99), mk(2, 0)));        // weight decides first, whatever the ids are
    CHECK(!w1::heapLess(mk(2, 0), mk(1, 99)));
    CHECK(w1::heapLess(mk(5, 3), mk(5, 4)));         // equal weight: smaller id first
    CHECK(!w1::heapLess(mk(5, 4), mk(5, 3)));
    CHECK(w1::heapLess(mk(0, 0), mk(std::numeric_limits<std::uint64_t>::max(), 0)));   // extremes
}

// ---------- black-box: empty / single element ------------------------------------------
static void testEmptyAndSingle() {
    MinHeap h;
    CHECK(h.empty());
    CHECK(h.size() == 0);
    CHECK(h.push(mk(42, 7)));
    CHECK(!h.empty());
    CHECK(h.size() == 1);
    CHECK(sameItem(h.top(), mk(42, 7)));
    CHECK(h.size() == 1);                              // top() does not remove
    const HeapItem r = h.pop();
    CHECK(sameItem(r, mk(42, 7)));
    CHECK(h.empty() && h.size() == 0);                 // pop of the last item: the size_ > 0 guard is false
    CHECK(h.push(mk(1, 1)));                           // still usable afterwards
    CHECK(sameItem(h.pop(), mk(1, 1)));
}

static void testKnownOrderAndTies() {
    MinHeap h;
    const std::uint64_t w[] = {5, 3, 8, 1, 9, 2};
    for (std::uint32_t i = 0; i < 6; ++i) CHECK(h.push(mk(w[i], i)));
    CHECK(sameItem(h.top(), mk(1, 3)));
    const std::vector<HeapItem> got = drain(h);
    const std::uint32_t expectIds[] = {3, 5, 1, 0, 2, 4};           // weights 1,2,3,5,8,9
    CHECK(got.size() == 6);
    for (std::size_t i = 0; i < got.size(); ++i) CHECK(got[i].id == expectIds[i]);

    for (std::uint32_t id = 10; id-- > 0;) CHECK(h.push(mk(7, id)));   // equal weights, pushed in reverse id order
    const std::vector<HeapItem> ties = drain(h);
    for (std::uint32_t i = 0; i < 10; ++i) CHECK(ties[i].id == i);     // ... must leave in id order
}

static void testExtremeValues() {
    MinHeap h;
    const std::uint64_t big = std::numeric_limits<std::uint64_t>::max();
    const std::uint32_t bigId = std::numeric_limits<std::uint32_t>::max();
    CHECK(h.push(mk(big, bigId)));
    CHECK(h.push(mk(0, 0)));
    CHECK(h.push(mk(big, 0)));
    CHECK(h.push(mk(0, bigId)));
    const std::vector<HeapItem> got = drain(h);
    CHECK(sameItem(got[0], mk(0, 0)) && sameItem(got[1], mk(0, bigId)));
    CHECK(sameItem(got[2], mk(big, 0)) && sameItem(got[3], mk(big, bigId)));
}

// ---------- black-box: capacity boundary (BVA) --------------------------------------------
static void testCapacityBoundary() {
    CHECK(MinHeap::kCapacity == 320);
    MinHeap h;
    for (std::uint32_t i = 0; i < MinHeap::kCapacity - 1; ++i) CHECK(h.push(mk(1000 - i, i)));
    CHECK(h.size() == 319);                            // capacity - 1
    CHECK(h.push(mk(0, 5000)));                        // exactly capacity: accepted
    CHECK(h.size() == 320);
    CHECK(sameItem(h.top(), mk(0, 5000)));
    CHECK(!h.push(mk(0, 5001)));                       // capacity + 1: refused
    CHECK(h.size() == 320);                            // ... and nothing changed
    CHECK(sameItem(h.top(), mk(0, 5000)));
    CHECK(sameItem(h.pop(), mk(0, 5000)));             // one slot free again
    CHECK(h.push(mk(0, 5002)));
    const std::vector<HeapItem> got = drain(h);
    CHECK(got.size() == 320);
    for (std::size_t i = 1; i < got.size(); ++i) CHECK(!w1::heapLess(got[i], got[i - 1]));   // sorted
}

// ---------- white-box: force each branch of pop() / push() --------------------------------
static void testPopBranches() {
    {   // left child only (no right child): array [1,2,3], pop leaves size 2
        MinHeap h; h.push(mk(1, 0)); h.push(mk(2, 1)); h.push(mk(3, 2));
        const auto g = drain(h);
        CHECK(g[0].weight == 1 && g[1].weight == 2 && g[2].weight == 3);
    }
    {   // right child smaller than left child: [1,5,3,6]
        MinHeap h; h.push(mk(1, 0)); h.push(mk(5, 1)); h.push(mk(3, 2)); h.push(mk(6, 3));
        const auto g = drain(h);
        CHECK(g[0].weight == 1 && g[1].weight == 3 && g[2].weight == 5 && g[3].weight == 6);
    }
    {   // sift-down stops early: the moved item is not larger than its children
        MinHeap h; h.push(mk(1, 0)); h.push(mk(5, 2)); h.push(mk(5, 1));
        const auto g = drain(h);
        CHECK(sameItem(g[0], mk(1, 0)) && sameItem(g[1], mk(5, 1)) && sameItem(g[2], mk(5, 2)));
    }
    {   // sift-down keeps going several levels
        MinHeap h;
        for (std::uint32_t i = 0; i < 15; ++i) h.push(mk(i + 1, i));
        h.push(mk(100, 99));                            // goes to the bottom
        CHECK(sameItem(h.pop(), mk(1, 0)));             // the last (largest) item sinks all the way down
        const auto g = drain(h);
        for (std::size_t i = 1; i < g.size(); ++i) CHECK(!w1::heapLess(g[i], g[i - 1]));
    }
}

static void testPushBranches() {
    {   // sift-up stops immediately: new item larger than its parent
        MinHeap h; h.push(mk(1, 0)); h.push(mk(9, 1));
        CHECK(sameItem(h.top(), mk(1, 0)));
    }
    {   // sift-up all the way to the root: strictly decreasing pushes
        MinHeap h;
        for (std::uint32_t i = 0; i < 40; ++i) CHECK(h.push(mk(100 - i, i)));
        CHECK(sameItem(h.top(), mk(61, 39)));
        CHECK(h.push(mk(0, 0)));
        CHECK(sameItem(h.top(), mk(0, 0)));
    }
    {   // sift-up stops on equal weight because the parent has the smaller id
        MinHeap h; h.push(mk(4, 0)); h.push(mk(4, 1));
        CHECK(sameItem(h.top(), mk(4, 0)));
    }
}

// ---------- oracle: random operations against a reference model --------------------------
static void testAgainstReference() {
    std::mt19937 rng(2026);
    for (int round = 0; round < 300; ++round) {
        MinHeap h;
        auto cmp = [](const HeapItem& a, const HeapItem& b) { return w1::heapLess(a, b); };
        std::multiset<HeapItem, decltype(cmp)> ref(cmp);
        std::uint32_t nextId = 0;
        for (int step = 0; step < 600; ++step) {
            const bool doPush = ref.empty() || (rng() % 100 < 55);
            if (doPush) {
                const HeapItem it = mk(rng() % 8, nextId++);          // few distinct weights: many ties
                const bool accepted = h.push(it);
                CHECK(accepted == (ref.size() < MinHeap::kCapacity));
                if (accepted) ref.insert(it);
            } else {
                CHECK(sameItem(h.top(), *ref.begin()));
                CHECK(sameItem(h.pop(), *ref.begin()));
                ref.erase(ref.begin());
            }
            CHECK(h.size() == ref.size());
            CHECK(h.empty() == ref.empty());
        }
        const auto rest = drain(h);
        std::size_t i = 0;
        for (const auto& it : ref) CHECK(sameItem(rest[i++], it));
    }
}

int main() {
    testHeapLessProperties();
    testEmptyAndSingle();
    testKnownOrderAndTies();
    testExtremeValues();
    testCapacityBoundary();
    testPopBranches();
    testPushBranches();
    testAgainstReference();
    return finishTests("test_min_heap");
}
