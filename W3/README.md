# Worklet 3 — CMP2 Container Module

This directory contains the container and metadata portion of Worklet 3.
The module owns the byte-level representation of the revised block-wise
`CMP2` format. It does not implement LZ77, Huffman coding, bit packing, or the
CRC32 algorithm.

The container module is deliberately independent of Worklets 1 and 2. It
accepts completed block data from the pipeline and returns parsed block data to
the pipeline.

## Building and running the complete utility

The W3 CMake project builds the `compressor` executable when the W1, W2, and
shared symbol sources are present. From this directory, configure and build it
with:

```sh
cmake -S . -B build
cmake --build build --target compressor
```

Compress and decompress files with:

```sh
./build/compressor -c input.bin -o input.cmp2
./build/compressor -d input.cmp2 -o restored.bin
```

Compression optionally accepts a power-of-two window size in bytes from 1 to
32768 with `-w`; the default is 32768. The executable wires W2's LZ77 block
producer and the output-history decoder sink into W3's pipeline, while W3 uses
W1 for Huffman encoding and decoding.

## 1. Responsibilities

### Owned by this container module

- `CMP2` file header serialization and parsing
- Block flags and block framing
- Little-endian integer serialization
- Unsigned LEB128 varint serialization
- Stored and compressed block representation
- Payload byte-length and meaningful-bit-length validation
- Source-size and payload-size bounds checking
- File signature and version validation
- Missing-final-block detection
- Trailing-byte rejection
- Serialization and deserialization of the stored CRC32 value
- Stable container error categories

### Not owned by this container module

- LZ77 matching or decoding
- Huffman tree construction
- Huffman encoding or decoding
- BitWriter or BitReader implementation
- CRC32 calculation
- CRC32 verification against reconstructed bytes
- CLI argument parsing
- Top-level compression/decompression orchestration

The CRC32 owner calculates and verifies the checksum. This module only writes
the checksum value into a block and returns it when reading a block.

## 2. CMP2 wire format

The earlier `CMP1` design used one global payload and one global code table.
`CMP2` is the revised block-wise format and is not backward-compatible with
`CMP1`.

```text
FILE := magic[4] | version[1] | log2Window[1] | BLOCK*
```

The file must end immediately after a block marked as the final block.

```text
BLOCK := flags[1]
       | sourceLength(varint)
       | payloadBytes(varint)
       | payloadBits(varint)
       | payload[payloadBytes]
       | blockCRC32[4]
```

All fixed-width multi-byte integers are little-endian. A `varint` is an
unsigned LEB128 value: seven data bits per byte, with the high bit indicating
continuation.

### File-header fields

| Field | Size | Meaning |
|---|---:|---|
| Magic | 4 bytes | ASCII `CMP2`; identifies this format |
| Version | 1 byte | Currently `0x01` |
| `log2Window` | 1 byte | LZ77 window size exponent; maximum value is 15, representing 32 KiB |

### Block fields

| Field | Encoding | Meaning |
|---|---|---|
| Flags | 1 byte | Final-block and block-type flags |
| `sourceLength` | Varint | Number of original bytes represented by this block |
| `payloadBytes` | Varint | Number of stored payload bytes |
| `payloadBits` | Varint | Number of meaningful bits in the payload |
| Payload | Byte array | Raw bytes or Huffman-packed bytes |
| `blockCRC32` | 4-byte little-endian | CRC32 supplied by the CRC32 owner |

The total original size is calculated as the sum of `sourceLength` across all
blocks. No original-size trailer is stored after the final block.

## 3. Block flags

```text
bit 0      final block flag
bits 1–2   block type
           0 = stored/raw
           1 = LZ77 + Huffman compressed
bits 3–7   reserved; must be zero
```

The public API represents these flags as typed fields:

```cpp
enum class BlockType : std::uint8_t {
    Stored = 0,
    Compressed = 1,
};
```

The container module converts `BlockType` and `isLast` to the on-disk flags.
Callers do not construct raw flag bytes.

## 4. Stored and compressed blocks

### Stored block

A stored block contains original bytes without Huffman encoding:

```text
payload       = original bytes
sourceLength  = payloadBytes
payloadBits   = payloadBytes * 8
blockCRC32    = CRC32(original bytes)
```

The `payloadBits` field is mathematically derivable for stored blocks, but it
is retained so every block has the same layout. The reader validates that:

```text
payloadBits == payloadBytes * 8
```

Stored blocks are used for random data, already-compressed data, or any block
where compression metadata would cost more than it saves.

### Compressed block

A compressed block contains the payload produced by Worklet 1:

```text
payload = Huffman metadata | encoded token stream | EOB | zero padding
```

For compressed blocks:

- `sourceLength` is the expected number of reconstructed bytes.
- `payloadBits` excludes final-byte padding.
- `payloadBytes` includes all bytes containing the compressed payload.
- `blockCRC32` is calculated over the reconstructed/original block bytes.

The container module does not inspect the Huffman metadata or token stream.

## 5. Public data structures

Header declaration:

```cpp
struct ContainerHeader {
    std::uint8_t version = kSupportedVersion;
    std::uint8_t log2Window = 15;
};
```

### `ContainerHeader` attributes

| Attribute | Type | Written by | Read by | Meaning |
|---|---|---|---|---|
| `version` | `uint8_t` | ContainerWriter | Pipeline/controller | Format version; writer currently sets version 1 |
| `log2Window` | `uint8_t` | Pipeline/controller through `open()` | W2/controller | Window size used by LZ77, represented as `2^log2Window` |

The header is exposed through:

```cpp
const ContainerHeader& ContainerWriter::header() const;
const ContainerHeader& ContainerReader::header() const;
```

Block declaration:

```cpp
struct ContainerBlock {
    BlockType type = BlockType::Stored;
    bool isLast = false;
    std::uint64_t sourceLength = 0;
    std::uint64_t payloadBits = 0;
    std::vector<std::uint8_t> payload;
    std::uint32_t crc32 = 0;
};
```

### `ContainerBlock` attributes

| Attribute | Type | Producer | Consumer | Meaning |
|---|---|---|---|---|
| `type` | `BlockType` | Pipeline/controller | Container writer/reader and controller | Stored or compressed block |
| `isLast` | `bool` | Pipeline/controller | Container writer/reader | Marks the only final block |
| `sourceLength` | `uint64_t` | W2/controller | Container and decoder | Expected original byte count for this block |
| `payloadBits` | `uint64_t` | W1/controller | Container and W1 | Meaningful compressed bits; excludes padding |
| `payload` | `vector<uint8_t>` | W1 or raw input reader | Container and W1/W2 pipeline | Stored bytes or compressed bytes |
| `crc32` | `uint32_t` | CRC32 owner | Container and controller | Precomputed checksum value; not calculated here |

`crc32` is intentionally only a value in this structure. The container code
does not know which CRC32 implementation produced it.

## 6. Public writer API

```cpp
class ContainerWriter {
public:
    static ContainerWriter open(
        const std::string& path,
        std::uint8_t log2Window
    );

    const ContainerHeader& header() const noexcept;
    void writeBlock(const ContainerBlock& block);
    void finish();
};
```

### `ContainerWriter::open()`

Creates/truncates the output file and immediately writes:

```text
CMP2 | version | log2Window
```

It validates that `log2Window <= 15`.

### `ContainerWriter::writeBlock()`

Serializes one complete block. The payload must already be complete because
the writer needs `payloadBytes` and `payloadBits` before writing the payload.

The method validates:

- source length is within the configured per-block limit
- payload length is within the configured per-block limit
- `payloadBits <= payloadBytes * 8`
- stored-block source length equals payload length
- stored-block bit length equals payload length times eight
- compressed blocks are non-empty
- no block is written after a final block

The method does not compute CRC32. It writes the `crc32` value supplied in the
`ContainerBlock`.

### `ContainerWriter::finish()`

Flushes and closes the output file. It succeeds only when a block marked
`isLast = true` has been written.

Calling `finish()` without a final block produces `MISSING_FINAL_BLOCK`.

## 7. Public reader API

```cpp
class ContainerReader {
public:
    static ContainerReader open(const std::string& path);

    const ContainerHeader& header() const noexcept;
    bool hasNextBlock() const;
    ContainerBlock readNextBlock();
    void finish();
};
```

### `ContainerReader::open()`

Opens the file and validates:

- file is readable
- enough bytes exist for the fixed header
- magic equals `CMP2`
- version equals the supported version
- `log2Window` is valid

### `hasNextBlock()`

Returns true when another block may be read. It returns false after a final
block has been read.

### `readNextBlock()`

Reads and structurally validates one block. It returns the stored CRC32 value
without calculating or verifying it.

Validation includes:

- reserved flags are zero
- block type is supported
- all varints terminate and fit in `uint64_t`
- varints use canonical encodings
- source and payload lengths are within limits
- payload fits within the remaining file
- `payloadBits <= payloadBytes * 8`
- stored-block lengths are consistent
- compressed blocks are not empty
- block CRC32 bytes are present

If the block has `isLast = true`, the reader immediately requires the file
position to equal end-of-file. Any additional bytes produce `TRAILING_DATA`.

### `ContainerReader::finish()`

Confirms that:

- a final block was read
- no bytes remain after it

This should be called after the caller finishes consuming blocks.

## 8. Convenience APIs

For callers that already have all blocks in a bounded vector:

```cpp
void write_container(
    const std::string& path,
    std::uint8_t log2Window,
    const std::vector<ContainerBlock>& blocks
);

struct ContainerFile {
    ContainerHeader header;
    std::vector<ContainerBlock> blocks;
};

ContainerFile read_container(const std::string& path);
```

The block-wise pipeline should prefer `ContainerWriter` and
`ContainerReader`, because they allow one block at a time to be written and
read. The convenience functions are useful for unit tests and small fixtures.

## 9. CRC32 integration boundary

The CRC32 owner provides:

```cpp
std::uint32_t compute_crc32(
    const std::vector<std::uint8_t>& originalBytes
);

bool verify_crc32(
    const std::vector<std::uint8_t>& originalBytes,
    std::uint32_t storedChecksum
);
```

### Compression

```text
W2/W3 obtains original block bytes
        ↓
CRC32 owner computes CRC32(original block bytes)
        ↓
W3 constructs ContainerBlock with crc32
        ↓
ContainerWriter::writeBlock()
```

### Decompression

```text
ContainerReader::readNextBlock()
        ↓
returns compressed payload and stored crc32
        ↓
W1 decodes and W2 reconstructs the block
        ↓
CRC32 owner verifies reconstructed bytes
        ↓
W3 writes bytes to the final output only after verification succeeds
```

For a stored block, the payload already is the original data, so the CRC can
be verified before committing that block to the output. For a compressed block,
CRC verification occurs after decoding into a temporary block buffer.

## 10. Error categories

The module exposes `ContainerError` with a stable `ErrorCode`:

| Error code | Meaning |
|---|---|
| `FILE_OPEN_FAILED` | Input or output file could not be opened |
| `FILE_READ_FAILED` | An I/O error occurred while reading |
| `FILE_WRITE_FAILED` | An I/O error occurred while writing or flushing |
| `INVALID_SIGNATURE` | Magic is not `CMP2` |
| `UNSUPPORTED_VERSION` | Format version is not supported |
| `INVALID_WINDOW` | Window encoding exceeds the 32 KiB limit |
| `TRUNCATED_INPUT` | Required header, payload, or checksum bytes are missing |
| `CORRUPT_HEADER` | Malformed varint or invalid structural metadata |
| `INVALID_FLAGS` | Reserved flag bits are set |
| `INVALID_BLOCK_TYPE` | Unknown block type was encountered |
| `INVALID_BIT_LENGTH` | Meaningful bits exceed payload capacity or stored-block rules |
| `INVALID_BLOCK` | Block fields are internally inconsistent |
| `INVALID_BLOCK_ORDER` | A block was read/written after the final block |
| `MISSING_FINAL_BLOCK` | File does not contain a block marked final |
| `TRAILING_DATA` | Bytes exist after the final block |

The CRC32 owner should report checksum failures separately, for example as
`CHECKSUM_MISMATCH`, because this module does not verify the checksum.

## 11. Worklet access matrix

| Component | Uses | Does not access |
|---|---|---|
| W1 Huffman | `ContainerBlock.payload`, `payloadBits` during integration | Container file bytes, flags, varint encoding |
| W2 LZ77 | `sourceLength` and reconstructed block bytes | Container serialization and CRC field encoding |
| W3 pipeline/controller | All public container APIs; chooses block type and final flag | Internal file stream and serialization helpers |
| W3 container owner | `ContainerHeader`, `ContainerBlock`, writer/reader classes | LZ77/Huffman algorithms and CRC32 mathematics |
| CRC32 owner | `ContainerBlock.crc32`, reconstructed/original bytes | Container byte layout and file I/O internals |

Other worklets should call `writeBlock()` and `readNextBlock()` rather than
writing directly to the underlying file stream.

## 12. Recommended integration example

### Compression

```cpp
auto writer = cmp2::ContainerWriter::open(outputPath, log2Window);

while (pipeline.hasNextBlock()) {
    ContainerBlock block;

    block.type = pipeline.shouldStoreBlock()
                     ? cmp2::BlockType::Stored
                     : cmp2::BlockType::Compressed;
    block.isLast = pipeline.isLastBlock();
    block.sourceLength = pipeline.sourceLength();
    block.payload = pipeline.payload();
    block.payloadBits = pipeline.payloadBits();
    block.crc32 = crc32_owner.compute_crc32(pipeline.originalBytes());

    writer.writeBlock(block);
}

writer.finish();
```

### Decompression

```cpp
auto reader = cmp2::ContainerReader::open(inputPath);

while (reader.hasNextBlock()) {
    ContainerBlock block = reader.readNextBlock();

    auto originalBytes = pipeline.decodeBlock(block);
    crc32_owner.verify_crc32(originalBytes, block.crc32);
    output.write(originalBytes);
}

reader.finish();
```

The actual pipeline names may differ, but the ownership boundary should remain
the same.

## 13. Tests

The independent container test suite covers:

- stored and compressed block round trips
- multiple blocks
- empty-file representation
- streaming writer and reader APIs
- invalid signature
- unsupported version
- truncated input
- invalid bit length
- reserved flags
- missing final block
- trailing data
- invalid stored-block metadata

The tests use fake CRC32 values. This verifies that the container preserves the
checksum field without coupling the container module to CRC32 implementation.

## 14. Benchmarking module

The benchmarking component of Worklet 3 measures performance and compression
effectiveness at both the per-block level and the whole-file level.

### Metrics calculated per block (`BlockBenchmark`):

- **Original size**: uncompressed source bytes (`sourceLength`)
- **Payload size**: bytes stored in container (`payloadBytes`)
- **Meaningful bits**: exact bit count (`payloadBits`)
- **Framing overhead**: 1 byte flags + LEB128 varints + 4 bytes CRC32
- **Final block size**: framing overhead + payload bytes (exact wire size)
- **Compression & decompression time**: measured with high-resolution timers (`std::chrono::high_resolution_clock`) in nanoseconds/milliseconds
- **Throughput**: compression and decompression speed in MB/s
- **Compression ratio**: `originalSize / finalBlockSize`
- **Percentage reduction**: `((originalSize - finalBlockSize) / originalSize) * 100%`
- **CRC-32 integrity**: verification status

### Combined whole-file metrics (`FileBenchmark`):

- **Total original & final sizes**
- **Container header size**: 6 bytes (`CMP2` magic [4] + version [1] + log2Window [1])
- **Total payload vs framing overhead**
- **Overall compression ratio** & **percentage reduction**
- **Overall throughput** (MB/s)
- **Block counts**: stored vs compressed block breakdown
- **Lossless round-trip verification**

Formatters export reports to text tables, CSV, and JSON.

## 15. Worklet 3 instruction requirements test suite

In addition to `test_container_format`, the suite includes `test_w3_instructions` covering:

1. **Empty file, 1 byte, 2 bytes**: edge cases round-trip (empty file = 1 stored block with 0 bytes and `isLast = true`).
2. **Text & repeated pattern**: verifies compression ratio > 1.0 and positive percentage reduction.
3. **Random bytes (incompressible)**: fallback to stored blocks with wire overhead < 0.1%.
4. **1 MiB and 1 MiB + 1 byte**: validates block boundary transitions (1 block vs 2 blocks).
5. **Bit-flip detection**: CRC-32 detects corrupted payload bytes cleanly with no crash.
6. **Truncation detection**: reader detects truncated input cleanly with no crash.
7. **Streaming handoff & benchmarking report**: multi-block streaming with per-block breakdown and summary report.

## 16. Build and test

From this directory:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or compile and run directly with clang++ / g++:

```sh
clang++ -std=c++17 -Wall -Wextra -Wpedantic -IW3/include W3/src/container_format.cpp W3/src/benchmarking.cpp W3/tests/test_container_format.cpp -o test_container && ./test_container
clang++ -std=c++17 -Wall -Wextra -Wpedantic -IW3/include W3/src/container_format.cpp W3/src/benchmarking.cpp W3/tests/test_benchmarking.cpp -o test_benchmarking && ./test_benchmarking
clang++ -std=c++17 -Wall -Wextra -Wpedantic -IW3/include W3/src/container_format.cpp W3/src/benchmarking.cpp W3/tests/test_w3_instructions.cpp -o test_w3_instructions && ./test_w3_instructions
```
