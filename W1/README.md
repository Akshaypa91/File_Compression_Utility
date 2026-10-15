# Worklet 1: Bit-level I/O + Huffman coding

W1 turns symbol counts into canonical Huffman codes and reads/writes real bits. It knows
nothing about LZ77: it only sees symbols, frequencies and extra bits. W3 calls it; W2 never does.

## Layout

```
shared/include/symbols.hpp   shared contract (alphabet, Tok, TokenBlock, bucket tables)  <- PROPOSED, needs all 3 leads' sign-off
W1/include/bit_io.h          BitWriter / BitReader (MSB-first, hard stop at payloadBits)
W1/include/min_heap.h        fixed-size array min-heap, ordered by (weight, id)
W1/include/huffman_tree.h    lengths, length limiting, canonical codes, prefix-free check, trie walk
W1/include/huffman_types.h   Status, Plan, DecodeTable, EntropyReport
W1/include/huffman.h         the public API (below)
W1/src/*.cpp                 implementations
W1/tests/                    test_bit_io, test_tree, test_huffman (+ test_util.h)
W1/examples/demo.cpp         the golden example, printed step by step
```

## API (what W3 calls)

```cpp
w1::Plan   plan(const cmp::TokenBlock&);                         // lengths + codes + exact size, no bits written
w1::Status emit(const cmp::TokenBlock&, const w1::Plan&, w1::BitWriter&);   // header + tokens + EOB
w1::Status readTables(w1::BitReader&, w1::HuffTables&);          // decoder side: read the code lengths
template<class Sink> w1::Status decodeBlock(w1::BitReader&, const w1::HuffTables&, Sink&);
w1::EntropyReport entropyReport(const cmp::TokenBlock&, const w1::Plan&);
bool       verifyPrefixFree(const w1::Plan&);
```

**Compress** (per block):

```cpp
w1::Plan p = w1::plan(tb);                          // exact size known before writing a bit
if (p.totalBits() >= uint64_t(tb.srcLen) * 8) { /* write a STORED block instead */ }
else {
    std::vector<uint8_t> buf(p.totalBytes());       // or a reusable buffer of at least that size
    w1::BitWriter w(buf.data(), buf.size());
    w1::emit(tb, p, w);
    w.finish();
    // block payload = buf[0 .. totalBytes), payloadBits = w.bitCount()
}
```

**Decompress** (per block):

```cpp
w1::BitReader r(payload, payloadBytes, payloadBits);
w1::HuffTables tables;                              // ~5 KB, reuse it across blocks
if (w1::readTables(r, tables) != w1::Status::Ok) { /* corrupt */ }
MySink sink;                                        // W2's decoder: void lit(uint8_t); bool match(len, dist);
w1::Status s = w1::decodeBlock(r, tables, sink);    // runs to EOB, no intermediate buffer
```

`match` must return `false` for an impossible reference (distance larger than the bytes
produced so far); decoding then stops with `Status::InvalidToken`.

## Block format produced by `emit`

```
[ 286 literal/length code lengths, 4 bits each ][ 30 distance code lengths, 4 bits each ]   = 158 bytes
[ token codes + extra bits ... ][ EOB code ]                                                zero padded
```

All bits MSB-first. `payloadBits` (= `BitWriter::bitCount()`) must be stored by W3; the reader refuses
to read past it, and after EOB no meaningful bits may remain (`TrailingBits` otherwise).

## Decisions made in this implementation

| Topic | Choice | Why |
|---|---|---|
| Codes stored | lengths only, canonical codes | smallest header; decoder rebuilds identical codes |
| Max code length | 15, zlib-style fix-up | bounded tables, fits `uint16_t` codes |
| Tie-break | (weight, node id) | the same tree on every compiler, so output is reproducible |
| Heap | hand-written array heap | foundations-first, no allocation |
| Decoder | 10-bit lookup + canonical fallback | fast; `trieDecode` (textbook trie walk) is kept as a reference and tested against it |
| Frequencies | counted by W2 at emit time (`TokenBlock::addLiteral/addMatch`) | no counting pass in W1 |
| Single used symbol | length 1, code `0` | never an empty code |
| Empty block | only EOB, 1-bit code | handled, though W3 stores empty files as a stored block |

## Build and test

```
cmake -S W1 -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/w1_demo
# with sanitizers:
cmake -S W1 -B build_san -DCMAKE_BUILD_TYPE=Debug -DW1_SANITIZE=ON && cmake --build build_san && ctest --test-dir build_san
```

## What the tests prove

- Bit I/O: exact patterns (`101`,`11` -> `10111000`; the 13-bit example -> `B6 A8`), padding, 0 and 32-bit writes, hard stop at `payloadBits`.
- Tree: textbook example (cost 224), Huffman cost equals an independent reference on 500 random alphabets, Kraft sum, prefix-free verifier (accepts valid, rejects over-subscribed, prefix and duplicate codes), length limiting under skewed alphabets, trie walk and table decoder agree on every bit stream.
- End to end: the golden vector (payload bytes `97 2E 00`), every legal length and distance bucket, 300 random blocks of 6 shapes, a block that really reaches the 15-bit limit, error statuses, 4000 corrupted or truncated blocks, malformed headers.

## Open items for the three leads

1. `symbols.hpp` is a proposal: freeze the bucket tables and the `Tok` layout.
2. Code-length header is plain 4-bit values (158 bytes per block); run-length coding of zero runs would shrink it for small blocks.
3. `Sink::match` returns `bool` (the earlier design draft had `void`), so W2 can reject bad distances.
4. W2 must stop a block at 32,768 tokens or the container's per-block source limit, whichever comes first.
5. W1 has no global or static mutable state, so segment-parallel compression (needed for very large files) only requires one `TokenBlock` and one `BitWriter` per thread.
