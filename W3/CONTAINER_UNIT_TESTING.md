# CMP2 Container Format: Architecture, Invariants, and Unit Testing Report

**Module:** `cmp2_container`  
**Source Files:** [`W3/include/container_format.hpp`](file:///Users/varadkale/temp/File_Compression_Utility/W3/include/container_format.hpp) & [`W3/src/container_format.cpp`](file:///Users/varadkale/temp/File_Compression_Utility/W3/src/container_format.cpp)  
**Test Suites:** [`W3/tests/test_container_extended.cpp`](file:///Users/varadkale/temp/File_Compression_Utility/W3/tests/test_container_extended.cpp) & [`W3/tests/test_container_format.cpp`](file:///Users/varadkale/temp/File_Compression_Utility/W3/tests/test_container_format.cpp)  
**Branch:** `dev_varad`  
**Specification:** CMP2 Block-Oriented Framing Protocol (Version 0x01)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [What is the CMP2 Container and Why is it Necessary?](#2-what-is-the-cmp2-container-and-why-is-it-necessary)
3. [Internal Mechanics: Binary Wire Format & Invariants](#3-internal-mechanics-binary-wire-format--invariants)
   - [3.1 Binary Stream Layout](#31-binary-stream-layout)
   - [3.2 The 1-Byte Block Flags Layout](#32-the-1-byte-block-flags-layout)
   - [3.3 LEB128 Varint Encoding & Security Guarantees](#33-leb128-varint-encoding--security-guarantees)
   - [3.4 Stored vs. Compressed Block Invariants](#34-stored-vs-compressed-block-invariants)
   - [3.5 Resource Limits & Denial-of-Service Defense](#35-resource-limits--denial-of-service-defense)
4. [Public API Specification](#4-public-api-specification)
5. [Why Unit Testing is Critical for Container Framing](#5-why-unit-testing-is-critical-for-container-framing)
6. [Detailed Analysis of Conducted Unit Tests](#6-detailed-analysis-of-conducted-unit-tests)
   - [6.1 ContainerWriter Tests (CW-U-01 through CW-U-12)](#61-containerwriter-tests-cw-u-01-through-cw-u-12)
   - [6.2 ContainerReader Tests (CR-U-01 through CR-U-16)](#62-containerreader-tests-cr-u-01-through-cr-u-16)
7. [Comprehensive Input vs. Output Reference Matrix](#7-comprehensive-input-vs-output-reference-matrix)
8. [Test Build & Execution Verification](#8-test-build--execution-verification)
9. [Conclusion & Technical Defense Guide](#9-conclusion--technical-defense-guide)

---

## 1. Executive Summary

This document provides the definitive technical specification and unit testing report for the **CMP2 Container Format** (`cmp2_container`). The container layer serves as the persistent envelope and streaming framing protocol for the File Compression Utility.

The unit testing implementation in [`W3/tests/test_container_extended.cpp`](file:///Users/varadkale/temp/File_Compression_Utility/W3/tests/test_container_extended.cpp) executes **28 distinct test scenarios evaluating 42 individual assertions**. Combined with the base container test suite, all tests achieve a **100% pass rate** across all 16 defined error codes, verifying complete state machine integrity, defensive memory bounding, canonical varint security, and error-resilient deserialization.

---

## 2. What is the CMP2 Container and Why is it Necessary?

### The Problem with Bare Compressed Bitstreams
When raw data is compressed using LZ77 and Huffman coding:
- Huffman generates variable-length bit sequences (e.g. 11 bits, 3 bits) that end mid-byte. A file system can only store whole bytes. Without container metadata, the decompressor cannot know how many bits in the final byte are valid data versus padding.
- LZ77 requires a history window (e.g. $2^{15} = 32\text{ KiB}$). If the decompressor uses a different window size than the compressor, decompression produces garbage memory.
- If a multi-gigabyte file is compressed as a single continuous stream, the decompressor must allocate enormous memory buffers.

### How CMP2 Solves This
1. **Self-Describing Header:** The file begins with a 6-byte header declaring the format signature (`CMP2`), protocol version (`0x01`), and window size (`log2Window`).
2. **Discrete Block-Wise Framing:** Large inputs are partitioned into discrete blocks (maximum 1 MiB uncompressed). Each block is framed with uncompressed length (`sourceLength`), compressed byte count (`payloadBytes`), valid bit count (`payloadBits`), payload data, and an independent CRC-32 checksum.
3. **Bounded Memory Allocation:** A decompressor only needs to allocate memory for one block at a time (bounded at 1 MiB source / 16 MiB payload), preventing malicious archives from causing out-of-memory denial-of-service crashes.
4. **Adaptive Block Selection:** Each block can independently be `Compressed` or `Stored` (raw fallback when compression would expand incompressible data).

---

## 3. Internal Mechanics: Binary Wire Format & Invariants

### 3.1 Binary Stream Layout

```
+--------------------------------------------------------------------------+
|                             FILE HEADER (6 Bytes)                        |
+--------------------------------------------------------------------------+
| Bytes 0..3: Magic Signature  | Byte 4: Version | Byte 5: log2Window      |
| 'C' , 'M' , 'P' , '2'        | 0x01            | 15 (2^15 = 32 KiB)      |
+--------------------------------------------------------------------------+

Followed by 1 or more sequentially framed blocks:

+--------------------------------------------------------------------------+
|                            BLOCK FRAMING LAYOUT                          |
+--------------------------------------------------------------------------+
| Field Name      | Encoding / Type   | Description                        |
+-----------------+-------------------+------------------------------------+
| Flags           | 1 Byte (uint8_t)  | Bit 0: isLast                      |
|                 |                   | Bits 1..2: BlockType (0=Stored,    |
|                 |                   |                      1=Compressed) |
|                 |                   | Bits 3..7: Reserved (must be 0)    |
| sourceLength    | LEB128 Varint     | Uncompressed size of this block    |
| payloadBytes    | LEB128 Varint     | Compressed/stored payload byte len |
| payloadBits     | LEB128 Varint     | Number of valid bits in payload    |
| Payload Buffer  | Raw Byte Array    | Exactly payloadBytes of data       |
| Block CRC-32    | 4 Bytes (uint32)  | Little-endian checksum of payload  |
+--------------------------------------------------------------------------+
```

### 3.2 The 1-Byte Block Flags Layout
Every block begins with a single flag byte packed as follows:
```
 Bit 7   Bit 6   Bit 5   Bit 4   Bit 3   Bit 2   Bit 1   Bit 0
+-------+-------+-------+-------+-------+-------+-------+-------+
|   0   |   0   |   0   |   0   |   0   |   Block Type  |isLast |
+-------+-------+-------+-------+-------+-------+-------+-------+
 \_____________________________________/ \_____________/ \______/
           Reserved (Must Be 0)           00 = Stored     0 = False
           Mask: 0xF8                     01 = Compressed 1 = True
                                          Mask: 0x06      Mask: 0x01
```
* **Security Rule:** If any bit in `0xF8` (bits 3–7) is non-zero, the reader immediately rejects the block with `ErrorCode::InvalidFlags`. This guarantees forward compatibility for future flags without silent misinterpretation.
* **Block Type Rule:** If bits 1–2 evaluate to `2` (`10_2`) or `3` (`11_2`), the reader throws `ErrorCode::InvalidBlockType`.

### 3.3 LEB128 Varint Encoding & Security Guarantees
CMP2 serializes integer lengths using **Unsigned Little-Endian Base 128 (LEB128)** varints:
- Each byte stores 7 data bits (bits 0–6).
- Bit 7 is the **continuation bit**: `1` means more bytes follow; `0` marks the final byte.

```
Example: 300
Binary:  00000001 00101100
Split 7: [0000010] [0101100]
Byte 0:  1 0101100 = 0xAC (Continuation bit set)
Byte 1:  0 0000010 = 0x02 (Final byte)
Result:  0xAC 0x02 (2 bytes)
```

#### Security Defenses in `read_varint()`:
1. **Non-Canonical Rejection:** Value `0` can theoretically be encoded as `0x80 0x00`. CMP2 explicitly rejects redundant leading zeros (`index > 0 && data == 0`) with `ErrorCode::CorruptHeader`, preventing hash collision or evasion exploits.
2. **Unterminated Rejection:** If 10 consecutive bytes have continuation bit `1`, the loop terminates and throws `ErrorCode::CorruptHeader` instead of reading infinitely.
3. **64-bit Overflow Defense:** A 64-bit unsigned integer holds at most $10 \times 7 = 70$ bits. On the 10th byte (index 9), the upper 6 bits must not exceed 1 (`data > 1U`). If violated, `ErrorCode::CorruptHeader` is thrown.

### 3.4 Stored vs. Compressed Block Invariants

The container strictly enforces mathematical invariants in `validate_block_for_write()` and `parse_block()`:

#### For `BlockType::Stored`:
1. `sourceLength == payloadBytes`: An uncompressed block cannot change size.
2. `payloadBits == payloadBytes * 8`: Every byte in an uncompressed block contains exactly 8 valid bits.
*Violation $\rightarrow$ `ErrorCode::InvalidBlock` or `ErrorCode::InvalidBitLength`.*

#### For `BlockType::Compressed`:
1. `sourceLength > 0`: A compressed block must represent actual source data.
2. `payloadBytes > 0`: A compressed block cannot have empty payload data.
3. `payloadBits > 0`: A compressed block cannot have zero valid bits.
4. `payloadBits <= payloadBytes * 8`: The valid bit count cannot exceed the physical capacity of the allocated payload bytes.
*Violation $\rightarrow$ `ErrorCode::InvalidBlock` or `ErrorCode::InvalidBitLength`.*

### 3.5 Resource Limits & Denial-of-Service Defense
- **Maximum Source Length:** `kMaxSourceBytesPerBlock = 1 MiB` (`1048576` bytes).
- **Maximum Payload Length:** `kMaxPayloadBytesPerBlock = 16 MiB` (`16777216` bytes).
- **Maximum Window Log2:** `kMaxWindowLog2 = 15` ($32\text{ KiB}$).

If any parsed header field exceeds these constants, deserialization immediately aborts with `ErrorCode::CorruptHeader` or `ErrorCode::InvalidWindow` before allocating memory.

---

## 4. Public API Specification

The container API is declared in [`W3/include/container_format.hpp`](file:///Users/varadkale/temp/File_Compression_Utility/W3/include/container_format.hpp):

```cpp
namespace cmp2 {

enum class BlockType : std::uint8_t {
    Stored = 0,
    Compressed = 1,
};

enum class ErrorCode {
    FileOpenFailed, FileReadFailed, FileWriteFailed,
    InvalidArgument, InvalidSignature, UnsupportedVersion,
    InvalidWindow, TruncatedInput, CorruptHeader,
    InvalidFlags, InvalidBlockType, InvalidBitLength,
    InvalidBlock, InvalidBlockOrder, MissingFinalBlock,
    TrailingData,
};

struct ContainerHeader {
    std::uint8_t version = 1;
    std::uint8_t log2Window = 15;
};

struct ContainerBlock {
    BlockType type = BlockType::Stored;
    bool isLast = false;
    std::uint64_t sourceLength = 0;
    std::uint64_t payloadBits = 0;
    std::vector<std::uint8_t> payload;
    std::uint32_t crc32 = 0;
};

class ContainerWriter final {
public:
    static ContainerWriter open(const std::string& path, std::uint8_t log2Window);
    void writeBlock(const ContainerBlock& block);
    void writeBlock(BlockType type, bool isLast, std::uint64_t sourceLength,
                    std::uint64_t payloadBits, const std::uint8_t* data,
                    std::size_t size, std::uint32_t crc32);
    void finish();
};

class ContainerReader final {
public:
    static ContainerReader open(const std::string& path);
    const ContainerHeader& header() const noexcept;
    bool hasNextBlock() const;
    ContainerBlock readNextBlock();
    ContainerBlock readNextBlock(std::vector<std::uint8_t>& reusablePayload);
    void finish();
};

} // namespace cmp2
```

---

## 5. Why Unit Testing is Critical for Container Framing

The container is the outer protective shell of the entire archive. Bugs in this module create severe vulnerabilities:
1. **Unbounded Allocations (OOM Bombs):** If a reader trusts an unchecked 64-bit varint for `payloadBytes`, a 10-byte malformed file could request `malloc(16 GB)`, crashing the OS or process.
2. **Infinite Loops:** An unterminated varint with continuation bits could trap the deserializer in an endless loop.
3. **Data Loss via Silent Truncation:** If a writer allows writing after `finish()`, blocks are silently lost without reaching disk.
4. **Garbage Decompression:** If `isLast` flags are misread or missing, decompression pipelines fail to terminate, reading trailing noise as data.

---

## 6. Detailed Analysis of Conducted Unit Tests

All unit tests are implemented in [`W3/tests/test_container_extended.cpp`](file:///Users/varadkale/temp/File_Compression_Utility/W3/tests/test_container_extended.cpp).

---

### 6.1 ContainerWriter Tests (CW-U-01 through CW-U-12)

#### `CW-U-01`: Write Block After `finish()`
* **Objective:** Verify that `writeBlock()` cannot be invoked once the archive has been finalized.
* **Mechanism:** Open writer, write a valid final block, call `finish()`, then invoke `writeBlock()` again.
* **Expected vs Actual:** Throws `ErrorCode::InvalidBlockOrder` **[PASS]**

#### `CW-U-02`: Write Block After Final Block (`isLast=true`)
* **Objective:** Ensure no block can ever be appended after an `isLast=true` block, even before `finish()` is called.
* **Mechanism:** Write `Block A` with `isLast=true`. Attempt to write `Block B`.
* **Expected vs Actual:** Throws `ErrorCode::InvalidBlockOrder` **[PASS]**

#### `CW-U-03`: Empty Payload with `payloadBits > 0`
* **Objective:** Prevent inconsistent block metadata where a block has no allocated bytes but claims valid bits.
* **Mechanism:** Create Stored block with `payload = {}` (0 bytes) and `payloadBits = 32`.
* **Expected vs Actual:** Throws `ErrorCode::InvalidBitLength` ($32 > 0 \times 8$) **[PASS]**

#### `CW-U-04`: Source Length Exceeds 1 MiB Limit
* **Objective:** Enforce the 1 MiB framing limit to prevent memory exhaustion during decompression.
* **Mechanism:** Pass `sourceLength = kMaxSourceBytesPerBlock + 1` (`1,048,577` bytes).
* **Expected vs Actual:** Throws `ErrorCode::InvalidBlock` **[PASS]**

#### `CW-U-05`: Payload Bits Exceeds Physical Byte Capacity
* **Objective:** Reject blocks claiming more bits than their byte buffer can store.
* **Mechanism:** Pass `payload = {0x00, 0x00}` (2 bytes = 16 bits capacity) with `payloadBits = 17`.
* **Expected vs Actual:** Throws `ErrorCode::InvalidBitLength` ($17 > 16$) **[PASS]**

#### `CW-U-06`: Compressed Block with `sourceLength = 0`
* **Objective:** Enforce that compressed blocks must represent actual source data.
* **Mechanism:** Construct a `BlockType::Compressed` block with `sourceLength = 0`.
* **Expected vs Actual:** Throws `ErrorCode::InvalidBlock` **[PASS]**

#### `CW-U-07`: `log2Window > 15` on Open
* **Objective:** Ensure window sizes exceeding 32 KiB are rejected on writer creation.
* **Mechanism:** Call `ContainerWriter::open(path, 16)`.
* **Expected vs Actual:** Throws `ErrorCode::InvalidWindow` **[PASS]**

#### `CW-U-08`: Raw Header Byte Verification
* **Objective:** Verify byte-exact header formatting on disk.
* **Mechanism:** Write an archive with `log2Window = 12`. Read the raw file bytes directly from disk.
* **Expected vs Actual:**
  * Bytes 0..3: `'C','M','P','2'` **[PASS]**
  * Byte 4: `0x01` (`kSupportedVersion`) **[PASS]**
  * Byte 5: `0x0C` (12) **[PASS]**

#### `CW-U-09`: Three-Block Stream Flag Sequencing
* **Objective:** Prove multi-block files serialize `isLast` flags with exact positional fidelity.
* **Mechanism:** Write 3 blocks: Block 0 (`isLast=false`), Block 1 (`isLast=false`), Block 2 (`isLast=true`).
* **Expected vs Actual:** Read back: `blocks[0].isLast == false`, `blocks[1].isLast == false`, `blocks[2].isLast == true` **[PASS]**

#### `CW-U-10`: Verbatim CRC Pass-Through
* **Objective:** Prove the container stores caller-provided CRCs verbatim without modifying or recalculating them.
* **Mechanism:** Store block with intentional dummy `crc32 = 0xDEADBEEFU`.
* **Expected vs Actual:** Read back returns exactly `0xDEADBEEFU` **[PASS]**

#### `CW-U-11`: Multi-Byte Varint Preservation
* **Objective:** Ensure source lengths $> 127$ requiring multi-byte LEB128 varints round-trip accurately.
* **Mechanism:** Write block with `sourceLength = 300` (encodes as `0xAC 0x02`).
* **Expected vs Actual:** Read back returns `sourceLength == 300` **[PASS]**

#### `CW-U-12`: Null Pointer Validation (`InvalidArgument`)
* **Objective:** Guard against null pointer buffer arguments in the raw pointer overload.
* **Mechanism:** Call `writeBlock(BlockType::Stored, true, 10, 80, nullptr, 10, 0)`.
* **Expected vs Actual:** Throws `ErrorCode::InvalidArgument` **[PASS]**

---

### 6.2 ContainerReader Tests (CR-U-01 through CR-U-16)

#### `CR-U-01`: Open Non-Existent File
* **Objective:** Catch file I/O errors gracefully.
* **Mechanism:** Call `ContainerReader::open("/no/such/file/ever.cmp2")`.
* **Expected vs Actual:** Throws `ErrorCode::FileOpenFailed` **[PASS]**

#### `CR-U-02`: Invalid `log2Window` in File Header
* **Objective:** Reject files modified with unsupported window sizes.
* **Mechanism:** Create valid archive; patch Byte 5 to `16`.
* **Expected vs Actual:** Throws `ErrorCode::InvalidWindow` **[PASS]**

#### `CR-U-03`: Invalid Block Type Bits in Flags
* **Objective:** Reject unknown block types.
* **Mechanism:** Patch Byte 6 (flags) bits 1–2 to `10_2` ($2$).
* **Expected vs Actual:** Throws `ErrorCode::InvalidBlockType` **[PASS]**

#### `CR-U-04`: Reserved Flag Bits Set
* **Objective:** Strictly enforce that reserved bits 3–7 in flags byte must be zero.
* **Mechanism:** Patch Byte 6 (flags) to set bit 3 (`flags | 0x08`).
* **Expected vs Actual:** Throws `ErrorCode::InvalidFlags` **[PASS]**

#### `CR-U-05`: Read Block After Final Block
* **Objective:** Enforce reader state machine termination.
* **Mechanism:** Read final block; call `readNextBlock()` again.
* **Expected vs Actual:** Throws `ErrorCode::InvalidBlockOrder` **[PASS]**

#### `CR-U-06`: Finish Without Final Block Seen
* **Objective:** Prevent closing/accepting a truncated stream missing its final block.
* **Mechanism:** Patch final block's `isLast` bit to `0`; call `reader.finish()`.
* **Expected vs Actual:** Throws `ErrorCode::MissingFinalBlock` **[PASS]**

#### `CR-U-07`: Stored Block Length Mismatch
* **Objective:** Detect corruption where stored `sourceLength` disagrees with `payloadBytes`.
* **Mechanism:** Patch `sourceLength` varint byte from `4` to `5` while payload is 4 bytes.
* **Expected vs Actual:** Throws `ErrorCode::InvalidBlock` **[PASS]**

#### `CR-U-08`: Non-Canonical Varint Rejection
* **Objective:** Enforce canonical LEB128 representation (reject redundant continuation bytes).
* **Mechanism:** Inject `0x80 0x00` (non-canonical encoding of 0) into source length field.
* **Expected vs Actual:** Throws `ErrorCode::CorruptHeader` **[PASS]**

#### `CR-U-09`: Unterminated Varint Rejection
* **Objective:** Prevent infinite loops on streams of continuation bytes.
* **Mechanism:** Inject 10 consecutive bytes all having bit 7 set (`0x81 0x81 ...`).
* **Expected vs Actual:** Throws `ErrorCode::CorruptHeader` **[PASS]**

#### `CR-U-10`: CRC Zero Round-Trip Boundary
* **Objective:** Verify CRC value `0x00000000` deserializes accurately.
* **Expected vs Actual:** Read back matches `0x00000000` **[PASS]**

#### `CR-U-11`: CRC Max Round-Trip Boundary
* **Objective:** Verify CRC value `0xFFFFFFFF` deserializes accurately without sign-extension corruption.
* **Expected vs Actual:** Read back matches `0xFFFFFFFF` **[PASS]**

#### `CR-U-12`: `hasNextBlock()` State Transitions
* **Objective:** Prove stream traversal lifecycle behaves predictably.
* **Expected vs Actual:** `hasNextBlock()` is `true` before reading, `false` after final block. `finish()` succeeds **[PASS]**

#### `CR-U-13`: Varint 64-Bit Integer Overflow
* **Objective:** Prevent integer overflow exploits on 10th varint byte.
* **Mechanism:** Inject 9 continuation bytes of `0x80` followed by 10th byte `0x02` ($2 > 1$).
* **Expected vs Actual:** Throws `ErrorCode::CorruptHeader` ("overflows uint64_t") **[PASS]**

#### `CR-U-14`: Trailing Data After Final Block
* **Objective:** Reject archives with extra junk appended after the final block.
* **Mechanism:** Append 2 garbage bytes (`0xDE, 0xAD`) after final block payload and CRC.
* **Expected vs Actual:** Throws `ErrorCode::TrailingData` **[PASS]**

#### `CR-U-15`: Truncated Payload Detection
* **Objective:** Ensure partial/cut-short files fail immediately without reading uninitialized memory.
* **Mechanism:** Truncate valid file 3 bytes before the end.
* **Expected vs Actual:** Throws `ErrorCode::TruncatedInput` **[PASS]**

#### `CR-U-16`: Reusable Buffer Overload (Zero-Copy)
* **Objective:** Verify `readNextBlock(reusablePayload)` populates the caller's pre-allocated buffer directly.
* **Mechanism:** Pass external buffer into reader overload.
* **Expected vs Actual:** External buffer populated with 4 bytes; returned `block.payload` remains empty (zero-copy) **[PASS]**

---

## 7. Comprehensive Input vs. Output Reference Matrix

| Test ID | Category | Scenario Under Test | Injected Input / Mechanism | Expected Outcome / ErrorCode | Status |
| :--- | :--- | :--- | :--- | :--- | :---: |
| **CW-U-01** | State Machine | Write after `finish()` | Call `writeBlock()` post `finish()` | `ErrorCode::InvalidBlockOrder` | **PASS** |
| **CW-U-02** | State Machine | Write after `isLast=true` | Append block after final block | `ErrorCode::InvalidBlockOrder` | **PASS** |
| **CW-U-03** | Invariant | Empty payload with `bits > 0` | `payload={}, payloadBits=32` | `ErrorCode::InvalidBitLength` | **PASS** |
| **CW-U-04** | Limit | Source length $> 1\text{ MiB}$ | `sourceLength = 1048577` | `ErrorCode::InvalidBlock` | **PASS** |
| **CW-U-05** | Invariant | Bits exceed capacity | `size=2, payloadBits=17` | `ErrorCode::InvalidBitLength` | **PASS** |
| **CW-U-06** | Invariant | Compressed with `srcLen=0` | `Compressed, sourceLength=0` | `ErrorCode::InvalidBlock` | **PASS** |
| **CW-U-07** | Header | `log2Window > 15` on open | `ContainerWriter::open(..., 16)`| `ErrorCode::InvalidWindow` | **PASS** |
| **CW-U-08** | Header | Raw header byte check | Disk inspection with `win=12` | `'C','M','P','2', 0x01, 12` | **PASS** |
| **CW-U-09** | Flags | 3-block flag sequencing | 2 non-last + 1 last block | `isLast: [false, false, true]` | **PASS** |
| **CW-U-10** | CRC | CRC stored verbatim | `crc32 = 0xDEADBEEFU` | `0xDEADBEEFU` | **PASS** |
| **CW-U-11** | Varint | Multi-byte varint round-trip | `sourceLength = 300` | `sourceLength == 300` | **PASS** |
| **CW-U-12** | Validation | Null pointer with `size > 0` | `writeBlock(..., nullptr, 10, ...)`| `ErrorCode::InvalidArgument` | **PASS** |
| **CR-U-01** | I/O | Non-existent file | Open `"/no/such/file.cmp2"` | `ErrorCode::FileOpenFailed` | **PASS** |
| **CR-U-02** | Header | `log2Window = 16` in file | Byte 5 patched to `16` | `ErrorCode::InvalidWindow` | **PASS** |
| **CR-U-03** | Flags | Corrupt block type in file | Byte 6 bits[2:1] patched to `2` | `ErrorCode::InvalidBlockType` | **PASS** |
| **CR-U-04** | Flags | Reserved flag bits set | Byte 6 bit 3 patched to `1` | `ErrorCode::InvalidFlags` | **PASS** |
| **CR-U-05** | State Machine | Read after final block | Call `readNextBlock()` twice | `ErrorCode::InvalidBlockOrder` | **PASS** |
| **CR-U-06** | State Machine | `finish()` without final block | File with `isLast=0`, call finish | `ErrorCode::MissingFinalBlock` | **PASS** |
| **CR-U-07** | Invariant | Stored length mismatch | Byte 7 patched from 4 to 5 | `ErrorCode::InvalidBlock` | **PASS** |
| **CR-U-08** | Varint | Non-canonical varint | Varint injected as `0x80 0x00` | `ErrorCode::CorruptHeader` | **PASS** |
| **CR-U-09** | Varint | Unterminated varint | 10 continuation bytes injected | `ErrorCode::CorruptHeader` | **PASS** |
| **CR-U-10** | CRC | CRC zero round-trip | Write `crc32 = 0x00000000` | `0x00000000` | **PASS** |
| **CR-U-11** | CRC | CRC max round-trip | Write `crc32 = 0xFFFFFFFF` | `0xFFFFFFFF` | **PASS** |
| **CR-U-12** | State Machine | `hasNextBlock()` lifecycle | Before reading vs after final | `true` $\rightarrow$ `false` | **PASS** |
| **CR-U-13** | Varint | Varint 64-bit overflow | Byte 9 data $= 2 > 1$ | `ErrorCode::CorruptHeader` | **PASS** |
| **CR-U-14** | Robustness | Trailing data detection | 2 junk bytes appended to file | `ErrorCode::TrailingData` | **PASS** |
| **CR-U-15** | Robustness | Truncated payload | File cut short 3 bytes | `ErrorCode::TruncatedInput` | **PASS** |
| **CR-U-16** | Overload | Reusable buffer overload | `readNextBlock(callerBuffer)` | Caller buffer filled, block empty | **PASS** |

---

## 8. Test Build & Execution Verification

### Build & Run Commands
```bash
cd /Users/varadkale/temp/File_Compression_Utility/W3/build_test
cmake --build . --target test_container_extended
./test_container_extended
```

### Verbatim Test Execution Output
```text
==========================================================
  W3 Extended Container Unit Tests (test_container_extended)
==========================================================

[CW-U-01] Write after finish()
  PASS  CW-U-01

[CW-U-02] Write after last block
  PASS  CW-U-02

[CW-U-03] Empty payload vector but payloadBits > 0
  PASS  CW-U-03

[CW-U-04] sourceLength > per-block limit
  PASS  CW-U-04

[CW-U-05] payloadBits > payloadBytes*8
  PASS  CW-U-05

[CW-U-06] Compressed block with sourceLength = 0
  PASS  CW-U-06

[CW-U-07] log2Window > 15 on open()
  PASS  CW-U-07

[CW-U-08] Raw header bytes
  PASS  CW-U-08a: file has at least 6 header bytes
  PASS  CW-U-08b: magic[0] == 'C'
  PASS  CW-U-08c: magic[1] == 'M'
  PASS  CW-U-08d: magic[2] == 'P'
  PASS  CW-U-08e: magic[3] == '2'
  PASS  CW-U-08f: version byte == kSupportedVersion
  PASS  CW-U-08g: log2Window byte matches argument

[CW-U-09] Three-block file ordering
  PASS  CW-U-09a: three blocks read back
  PASS  CW-U-09b: block 0 isLast == false
  PASS  CW-U-09c: block 1 isLast == false
  PASS  CW-U-09d: block 2 isLast == true

[CW-U-10] CRC stored verbatim
  PASS  CW-U-10: container preserves caller-supplied CRC verbatim

[CW-U-11] Large sourceLength (multi-byte varint)
  PASS  CW-U-11: large sourceLength survives write->read round-trip

[CW-U-12] Null pointer with size > 0 -> InvalidArgument
  PASS  CW-U-12

[CR-U-01] Open nonexistent file
  PASS  CR-U-01

[CR-U-02] log2Window = 16 in file
  PASS  CR-U-02

[CR-U-03] BlockType = 2 in flags byte
  PASS  CR-U-03

[CR-U-04] Reserved flag bits set
  PASS  CR-U-04

[CR-U-05] readNextBlock() after final block
  PASS  CR-U-05

[CR-U-06] finish() with no final block
  PASS  CR-U-06

[CR-U-07] Stored block: sourceLength != payloadBytes
  PASS  CR-U-07

[CR-U-08] Non-canonical varint (0x80 0x00)
  PASS  CR-U-08

[CR-U-09] Unterminated varint (10 continuation bytes)
  PASS  CR-U-09

[CR-U-10] CRC == 0x00000000 round-trips
  PASS  CR-U-10: CRC 0x00000000 survives round-trip

[CR-U-11] CRC == 0xFFFFFFFF round-trips
  PASS  CR-U-11: CRC 0xFFFFFFFF survives round-trip

[CR-U-12] hasNextBlock() == false after final block
  PASS  CR-U-12a: hasNextBlock() true before reading
  PASS  CR-U-12b: block is marked as last
  PASS  CR-U-12c: hasNextBlock() false after final block
  PASS  CR-U-12d: finish() after last block does not throw

[CR-U-13] Varint 64-bit overflow -> CorruptHeader
  PASS  CR-U-13

[CR-U-14] Trailing data after final block -> TrailingData
  PASS  CR-U-14

[CR-U-15] Truncated payload -> TruncatedInput
  PASS  CR-U-15

[CR-U-16] Reusable payload buffer overload
  PASS  CR-U-16a: caller buffer received 4 bytes
  PASS  CR-U-16b: block.payload remains empty (zero-copy)
  PASS  CR-U-16c: caller buffer payload bytes match

----------------------------------------------------------
  Passed: 42  |  Failed: 0
----------------------------------------------------------
  All Container extended tests PASSED.
```

---

## 9. Conclusion & Technical Defense Guide

When presenting this work in technical reviews, highlight these four core software engineering disciplines:

1. **State Machine Correctness:**
   > *"We proved that both ContainerWriter and ContainerReader strictly enforce valid lifecycle states. A writer cannot append after finalization, and a reader rejects streams missing their final block or containing trailing junk bytes."*

2. **Defensive Memory Boundaries (Anti-Exploit Design):**
   > *"The container guards against memory exhaustion attacks by validating that `sourceLength <= 1 MiB` and `payloadBytes <= 16 MiB` before any memory allocation occurs. Truncated inputs are caught before attempting to read off the end of the file."*

3. **LEB128 Varint Security:**
   > *"We validated three critical varint failure modes: non-canonical zero encodings (`0x80 0x00`), unterminated byte sequences (infinite loop guard), and 64-bit unsigned integer overflow (`data > 1` on index 9)."*

4. **Fault Injection via Raw-Byte Patching:**
   > *"Rather than relying only on happy-path API round-trips, we tested the reader defensively by generating valid archives and deliberately injecting single-byte corruptions directly into the file on disk (corrupt window sizes, invalid block types, flipped reserved bits, and truncated payloads)."*
