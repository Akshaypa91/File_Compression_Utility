#include "huffman_tree.h"

#include <algorithm>

#include "min_heap.h"
#include "symbols.hpp"

namespace w1 {

namespace {
constexpr unsigned kMaxSymbols = cmp::kNumLitLen;   // largest alphabet we ever build
}

void buildLengths(const std::uint32_t* freq, unsigned n, unsigned maxLen, std::uint8_t* lens) {
    for (unsigned i = 0; i < n; ++i) lens[i] = 0;
    if (maxLen > cmp::kMaxCodeLen) maxLen = cmp::kMaxCodeLen;

    // Leaves are numbered 0..m-1 in increasing symbol order.
    std::uint16_t leafSym[kMaxSymbols];
    unsigned m = 0;
    for (unsigned i = 0; i < n && i < kMaxSymbols; ++i) {
        if (freq[i] != 0) leafSym[m++] = static_cast<std::uint16_t>(i);
    }
    if (m == 0) return;
    if (m == 1) {                       // single symbol: give it the 1-bit code "0"
        lens[leafSym[0]] = 1;
        return;
    }

    // Greedy merge with the min-heap. Node ids: leaves 0..m-1, internal nodes m, m+1, ...
    std::uint64_t weight[2 * kMaxSymbols];
    std::uint32_t parent[2 * kMaxSymbols];
    std::uint16_t depth[2 * kMaxSymbols];
    MinHeap heap;
    for (unsigned k = 0; k < m; ++k) {
        weight[k] = freq[leafSym[k]];
        heap.push(HeapItem{weight[k], k});
    }
    std::uint32_t next = m;
    while (heap.size() > 1) {
        const HeapItem a = heap.pop();
        const HeapItem b = heap.pop();
        weight[next] = a.weight + b.weight;
        parent[a.id] = next;
        parent[b.id] = next;
        heap.push(HeapItem{weight[next], next});
        ++next;
    }

    // Leaf depth = code length. Parents have larger ids, so one downward pass is enough.
    const std::uint32_t root = next - 1;
    depth[root] = 0;
    for (std::uint32_t id = root; id-- > 0;) depth[id] = static_cast<std::uint16_t>(depth[parent[id]] + 1);

    // `overflow` counts EVERY node deeper than maxLen, internal nodes included (as zlib does).
    // Each subtree hanging below depth maxLen with k leaves has k - 2 internal nodes, so it
    // adds 2k - 2 to the count while its clamped leaves exceed the Kraft budget by k - 1.
    // That is why the fix-up below can remove them two at a time.
    int overflow = 0;
    for (std::uint32_t id = 0; id < root; ++id) {
        if (depth[id] > maxLen) ++overflow;
    }
    unsigned blCount[cmp::kMaxCodeLen + 1] = {};
    for (unsigned k = 0; k < m; ++k) {
        ++blCount[depth[k] > maxLen ? maxLen : depth[k]];
    }

    if (overflow == 0) {                // the normal case: the tree already fits
        for (unsigned k = 0; k < m; ++k) lens[leafSym[k]] = static_cast<std::uint8_t>(depth[k]);
        return;
    }

    // Length limiting (zlib's method): each step moves one leaf pair down a level, which
    // removes two overflowing leaves and keeps the Kraft sum at most 1.
    do {
        unsigned bits = maxLen - 1;
        while (bits > 1 && blCount[bits] == 0) --bits;
        --blCount[bits];
        blCount[bits + 1] += 2;
        --blCount[maxLen];
        overflow -= 2;
    } while (overflow > 0);

    // Hand the lengths back out: the rarest symbols get the longest codes.
    unsigned order[kMaxSymbols];
    for (unsigned k = 0; k < m; ++k) order[k] = k;
    std::sort(order, order + m, [&](unsigned a, unsigned b) {
        return weight[a] != weight[b] ? weight[a] < weight[b] : a > b;
    });
    unsigned idx = 0;
    for (unsigned bits = maxLen; bits >= 1; --bits) {
        for (unsigned c = 0; c < blCount[bits]; ++c) {
            lens[leafSym[order[idx++]]] = static_cast<std::uint8_t>(bits);
        }
    }
}

void assignCanonical(const std::uint8_t* lens, unsigned n, std::uint16_t* codes) {
    unsigned blCount[cmp::kMaxCodeLen + 1] = {};
    for (unsigned i = 0; i < n; ++i) {
        if (lens[i] != 0) ++blCount[lens[i]];
    }
    std::uint32_t nextCode[cmp::kMaxCodeLen + 2] = {};
    std::uint32_t code = 0;
    for (unsigned bits = 1; bits <= cmp::kMaxCodeLen; ++bits) {
        code = (code + blCount[bits - 1]) << 1;
        nextCode[bits] = code;
    }
    for (unsigned i = 0; i < n; ++i) {
        codes[i] = lens[i] != 0 ? static_cast<std::uint16_t>(nextCode[lens[i]]++) : std::uint16_t{0};
    }
}

std::uint64_t kraftSum(const std::uint8_t* lens, unsigned n) {
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (lens[i] != 0) sum += std::uint64_t{1} << (cmp::kMaxCodeLen - lens[i]);
    }
    return sum;
}

bool verifyPrefixFree(const std::uint8_t* lens, const std::uint16_t* codes, unsigned n) {
    if (kraftSum(lens, n) > (std::uint64_t{1} << cmp::kMaxCodeLen)) return false;
    for (unsigned i = 0; i < n; ++i) {
        if (lens[i] == 0) continue;
        for (unsigned j = i + 1; j < n; ++j) {
            if (lens[j] == 0) continue;
            const unsigned l = std::min<unsigned>(lens[i], lens[j]);
            if ((codes[i] >> (lens[i] - l)) == (codes[j] >> (lens[j] - l))) return false;
        }
    }
    return true;
}

bool buildTrie(const std::uint8_t* lens, const std::uint16_t* codes, unsigned n, CodeTrie& trie) {
    trie.child0.assign(1, -1);
    trie.child1.assign(1, -1);
    trie.symbol.assign(1, -1);
    for (unsigned s = 0; s < n; ++s) {
        if (lens[s] == 0) continue;
        int node = 0;
        for (int bit = static_cast<int>(lens[s]) - 1; bit >= 0; --bit) {
            if (trie.symbol[static_cast<std::size_t>(node)] >= 0) return false;   // a code is a prefix of this one
            std::vector<int>& side = ((codes[s] >> bit) & 1u) ? trie.child1 : trie.child0;
            if (side[static_cast<std::size_t>(node)] < 0) {
                side[static_cast<std::size_t>(node)] = static_cast<int>(trie.symbol.size());
                trie.child0.push_back(-1);
                trie.child1.push_back(-1);
                trie.symbol.push_back(-1);
            }
            // note: push_back above may reallocate, so re-read through the member vectors
            node = (((codes[s] >> bit) & 1u) ? trie.child1 : trie.child0)[static_cast<std::size_t>(node)];
        }
        const std::size_t at = static_cast<std::size_t>(node);
        if (trie.symbol[at] >= 0 || trie.child0[at] >= 0 || trie.child1[at] >= 0) return false;
        trie.symbol[at] = static_cast<int>(s);
    }
    return true;
}

int trieDecode(const CodeTrie& trie, BitReader& reader) {
    int node = 0;
    for (;;) {
        std::uint32_t bit = 0;
        if (!reader.readBits(1, bit)) return -1;
        node = (bit ? trie.child1 : trie.child0)[static_cast<std::size_t>(node)];
        if (node < 0) return -1;
        const int sym = trie.symbol[static_cast<std::size_t>(node)];
        if (sym >= 0) return sym;
    }
}

}  // namespace w1