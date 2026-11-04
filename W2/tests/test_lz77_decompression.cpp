#include "../include/LZ77.h"
#include "../include/LZ77DecodeSink.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// Decode compact TokenBlock words using W2's decoder.
bool decodeBlock(
    const cmp::TokenBlock& block,
    cmp::LZ77DecodeSink& decoder,
    ostream& output
) {
    decoder.setOutput(output);

    for (uint32_t i = 0; i < block.n; ++i) {

        if (cmp::isMatch(block.tok[i])) {

            const uint32_t length =
                cmp::lenOf(block.tok[i]);

            const uint32_t distance =
                cmp::distOf(block.tok[i]);

            if (!decoder.match(length, distance)) {
                return false;
            }

            // The next compact word must be nextChar.
            ++i;

            if (i >= block.n || cmp::isMatch(block.tok[i])) {
                return false;
            }

            decoder.lit(cmp::litOf(block.tok[i]));
        }
        else {
            decoder.lit(cmp::litOf(block.tok[i]));
        }
    }

    return true;
}


// Compress, decompress and compare against original input.
bool testRoundTrip(
    const string& testName,
    const vector<uint8_t>& input
) {
    cmp::TokenBlock block{};

    const uint8_t* source =
        input.empty() ? nullptr : input.data();

    if (!cmp::compressLZ77(
            block,
            source,
            static_cast<uint32_t>(input.size()))) {
        cout << "FAIL: " << testName
             << " - Compression failed\n";
        return false;
    }

    cmp::LZ77DecodeSink decoder;
    ostringstream output(ios::out | ios::binary);

    if (!decodeBlock(block, decoder, output)) {
        cout << "FAIL: " << testName
             << " - Decompression failed\n";
        return false;
    }

    const string decoded = output.str();

    if (decoded.size() != input.size() ||
        !equal(
            input.begin(),
            input.end(),
            reinterpret_cast<const uint8_t*>(decoded.data())
        )) {
        cout << "FAIL: " << testName
             << " - Output does not match original\n";
        return false;
    }

    if (decoder.bytesProduced() != input.size()) {
        cout << "FAIL: " << testName
             << " - Incorrect output byte count\n";
        return false;
    }

    cout << "PASS: " << testName << '\n';
    return true;
}


// Test overlapping match reconstruction directly.
bool testOverlappingMatch() {
    cmp::LZ77DecodeSink decoder;
    ostringstream output(ios::out | ios::binary);

    decoder.setOutput(output);

    decoder.lit('A');

    if (!decoder.match(5, 1)) {
        cout << "FAIL: Overlapping match rejected\n";
        return false;
    }

    if (output.str() != "AAAAAA") {
        cout << "FAIL: Overlapping match output incorrect\n";
        return false;
    }

    cout << "PASS: Overlapping match\n";
    return true;
}


// Test invalid match distances.
bool testInvalidDistance() {
    cmp::LZ77DecodeSink decoder;
    ostringstream output(ios::out | ios::binary);

    decoder.setOutput(output);

    // No previous byte exists.
    if (decoder.match(3, 1)) {
        cout << "FAIL: Invalid distance accepted\n";
        return false;
    }

    decoder.lit('A');

    // Distance zero is invalid.
    if (decoder.match(3, 0)) {
        cout << "FAIL: Zero distance accepted\n";
        return false;
    }

    // Distance exceeds available history.
    if (decoder.match(3, 2)) {
        cout << "FAIL: Excessive distance accepted\n";
        return false;
    }

    cout << "PASS: Invalid distance rejection\n";
    return true;
}


int main() {
    int failures = 0;

    cout << "\n===== LZ77 DECOMPRESSION TESTS =====\n\n";

    failures += !testRoundTrip(
        "Empty input",
        {}
    );

    failures += !testRoundTrip(
        "Single byte",
        {'A'}
    );

    failures += !testRoundTrip(
        "No repeated characters",
        {'A','B','C','D','E','F','G','H'}
    );

    failures += !testRoundTrip(
        "Repeated characters",
        {'A','A','A','A','A','A','A','A','A','A'}
    );

    failures += !testRoundTrip(
        "Repeated sequence",
        {'A','B','A','B','A','B','A','B','C','D'}
    );

    failures += !testRoundTrip(
        "Binary data",
        {0, 255, 0, 255, 0, 255, 1, 2, 3, 0}
    );

    failures += !testOverlappingMatch();

    failures += !testInvalidDistance();

    cout << "\n==================================\n";

    if (failures == 0) {
        cout << "ALL DECOMPRESSION TESTS PASSED!\n";
        return 0;
    }

    cout << failures << " TEST(S) FAILED\n";
    return 1;
}