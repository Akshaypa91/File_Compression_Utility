#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>

#include "include/LZ77.h"
#include "include/Token.h"

static std::uint8_t* readFile(
    const char* filename,
    std::uint32_t* fileSize
)
{
    FILE* file = fopen(filename, "rb");

    if (file == nullptr)
    {
        return nullptr;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return nullptr;
    }

    long size = ftell(file);

    if (size < 0 ||
        static_cast<unsigned long>(size) > INT32_MAX)
    {
        fclose(file);
        return nullptr;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return nullptr;
    }

    std::size_t allocationSize =
        size == 0 ? 1 : static_cast<std::size_t>(size);

    auto* buffer = static_cast<std::uint8_t*>(
        malloc(allocationSize)
    );

    if (buffer == nullptr)
    {
        fclose(file);
        return nullptr;
    }

    std::size_t bytesRead = fread(
        buffer,
        1,
        static_cast<std::size_t>(size),
        file
    );

    fclose(file);

    if (bytesRead != static_cast<std::size_t>(size))
    {
        free(buffer);
        return nullptr;
    }

    *fileSize = static_cast<std::uint32_t>(size);

    return buffer;
}


static void printByte(std::uint8_t byte)
{
    if (byte >= 32 && byte <= 126 &&
        byte != '\'' && byte != '\\')
    {
        printf("'%c'", static_cast<int>(byte));
    }
    else
    {
        printf("0x%02X", static_cast<unsigned>(byte));
    }
}


static void printTokens(const cmp::TokenBlock& block)
{
    std::uint32_t index = 0;
    int tokenNumber = 0;

    while (index < block.n)
    {
        cmp::Tok word = block.tok[index];

        if (cmp::isMatch(word))
        {
            // A match is followed by its next literal word.
            if (index + 1 >= block.n ||
                cmp::isMatch(block.tok[index + 1]))
            {
                printf("Error: Invalid compact token sequence.\n");
                return;
            }

            std::uint32_t distance = cmp::distOf(word);
            std::uint32_t length = cmp::lenOf(word);
            std::uint8_t nextChar =
                cmp::litOf(block.tok[index + 1]);

            printf(
                "Token %d : (%u, %u, ",
                tokenNumber,
                distance,
                length
            );

            printByte(nextChar);
            printf(")\n");

            index += 2;
        }
        else
        {
            // A literal is represented by one compact word.
            std::uint8_t literal = cmp::litOf(word);

            printf("Token %d : (0, 0, ", tokenNumber);

            printByte(literal);
            printf(")\n");

            index++;
        }

        tokenNumber++;
    }
}


int main(int argc, char* argv[])
{
    std::uint8_t* input = nullptr;
    std::uint32_t inputSize = 0;

    if (argc >= 2)
    {
        input = readFile(argv[1], &inputSize);

        if (input == nullptr)
        {
            printf("Error: Could not read input file.\n");
            return 1;
        }
    }
    else
    {
        const char* test = "ABABABABABABABCD";

        inputSize = 16;

        input = static_cast<std::uint8_t*>(
            malloc(inputSize)
        );

        if (input == nullptr)
        {
            return 1;
        }

        for (std::uint32_t i = 0; i < inputSize; i++)
        {
            input[i] = static_cast<std::uint8_t>(test[i]);
        }
    }

    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, inputSize))
    {
        printf("Error: LZ77 compression failed.\n");

        free(input);
        return 1;
    }

    printf("\n========== LZ77 OUTPUT ==========\n\n");
    printf("Input size          : %u bytes\n", inputSize);
    printf("Compact word count  : %u\n", block.n);
    printf("LZ77 token sequence :\n\n");

    printTokens(block);

    printf("\n========== SYMBOL FREQUENCIES ==========\n\n");

    printf("End-of-block frequency: %u\n",
           block.litlenFreq[cmp::kEob]);

    printf("Literal/length frequencies:\n");

    for (unsigned i = 0; i < cmp::kNumLitLen; i++)
    {
        if (block.litlenFreq[i] != 0)
        {
            printf("Symbol %u : %u\n",
                   i,
                   block.litlenFreq[i]);
        }
    }

    printf("\nDistance frequencies:\n");

    for (unsigned i = 0; i < cmp::kNumDist; i++)
    {
        if (block.distFreq[i] != 0)
        {
            printf("Symbol %u : %u\n",
                   i,
                   block.distFreq[i]);
        }
    }

    printf("\n========================================\n");

    free(input);

    return 0;
}