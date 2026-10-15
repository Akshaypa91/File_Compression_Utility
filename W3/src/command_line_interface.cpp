#include "command_line_interface.hpp"

#include <stdexcept>

using namespace std;

namespace worklet3 {
namespace {

uint8_t parseWindowLog2(const string& value) {
    size_t parsedCharacters = 0;
    const unsigned long windowBytes = stoul(value, &parsedCharacters);
    if (parsedCharacters != value.size() || windowBytes == 0 ||
        windowBytes > (1UL << cmp2::kMaxWindowLog2) ||
        (windowBytes & (windowBytes - 1UL)) != 0) {
        throw invalid_argument("-w must be a power-of-two byte size from 1 to 32768");
    }

    uint8_t log2Window = 0;
    unsigned long normalized = windowBytes;
    while (normalized > 1) {
        normalized >>= 1U;
        ++log2Window;
    }
    return log2Window;
}

} // namespace

string CommandLineInterface::usage() {
    return "Usage: compressor (-c|-d) input -o output [-w window_bytes]\n";
}

int CommandLineInterface::run(int argc,
                              char* argv[],
                              PipelineController& pipeline,
                              ostream& output,
                              ostream& errors) {
    bool compress = false;
    bool decompress = false;
    bool windowWasSpecified = false;
    string inputPath;
    string outputPath;
    uint8_t log2Window = kDefaultWindowLog2;

    try {
        for (int index = 1; index < argc; ++index) {
            const string argument = argv[index];
            if (argument == "-h" || argument == "--help") {
                output << usage();
                return 0;
            }
            if (argument == "-c") {
                compress = true;
            } else if (argument == "-d") {
                decompress = true;
            } else if (argument == "-o" || argument == "-w") {
                if (index + 1 >= argc) {
                    throw invalid_argument("Missing value after " + argument);
                }
                const string value = argv[++index];
                if (argument == "-o") {
                    outputPath = value;
                } else {
                    log2Window = parseWindowLog2(value);
                    windowWasSpecified = true;
                }
            } else if (!argument.empty() && argument[0] == '-') {
                throw invalid_argument("Unknown option: " + argument);
            } else if (inputPath.empty()) {
                inputPath = argument;
            } else {
                throw invalid_argument("Only one input path may be provided");
            }
        }

        if (compress == decompress || inputPath.empty() || outputPath.empty()) {
            throw invalid_argument("Select exactly one mode and provide input and output paths");
        }
        if (decompress && windowWasSpecified) {
            throw invalid_argument("-w is only valid when compressing");
        }

        if (compress) {
            pipeline.compressFile(inputPath, outputPath, log2Window, &output);
        } else {
            pipeline.decompressFile(inputPath, outputPath, &output);
        }
        output << (compress ? "Compression complete\n" : "Decompression complete\n");
        return 0;
    } catch (const exception& error) {
        errors << "Error: " << error.what() << '\n' << usage();
        return 1;
    }
}

} // namespace worklet3