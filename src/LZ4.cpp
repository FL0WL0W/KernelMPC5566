#include "LZ4.h"

#include <limits>

namespace Kernel {
namespace {

constexpr std::size_t kMaximumEncoderInputSize = 4096U;
constexpr std::size_t kHashTableSize = 2048U;
constexpr std::uint16_t kInvalidPosition = 0xFFFFU;

std::uint16_t g_hashTable[kHashTableSize];

std::uint32_t Read32(const std::uint8_t* input) {
    return static_cast<std::uint32_t>(input[0]) |
           (static_cast<std::uint32_t>(input[1]) << 8U) |
           (static_cast<std::uint32_t>(input[2]) << 16U) |
           (static_cast<std::uint32_t>(input[3]) << 24U);
}

std::size_t HashSequence(const std::uint8_t* input) {
    return ((Read32(input) * 2654435761U) >> 21U) &
           (kHashTableSize - 1U);
}

bool WriteLength(std::size_t length,
                 std::uint8_t*& output,
                 const std::uint8_t* outputEnd) {
    while (length >= 255U) {
        if (output == outputEnd) {
            return false;
        }
        *output++ = 255U;
        length -= 255U;
    }
    if (output == outputEnd) {
        return false;
    }
    *output++ = static_cast<std::uint8_t>(length);
    return true;
}

bool ReadExtendedLength(const std::uint8_t*& input,
                        const std::uint8_t* inputEnd,
                        std::size_t& length) {
    if (length != 15U) {
        return true;
    }

    std::uint8_t extension;
    do {
        if (input == inputEnd) {
            return false;
        }
        extension = *input++;
        if (length > std::numeric_limits<std::size_t>::max() - extension) {
            return false;
        }
        length += extension;
    } while (extension == 255U);

    return true;
}

} // namespace

LZ4EncodeResult EncodeLZ4Block(const std::uint8_t* input,
                               std::size_t inputSize,
                               std::uint8_t* output,
                               std::size_t& outputSize) {
    if (input == nullptr || output == nullptr || inputSize == 0U ||
        inputSize > kMaximumEncoderInputSize) {
        return LZ4EncodeResult::InputTooLarge;
    }

    for (std::size_t i = 0U; i < kHashTableSize; ++i) {
        g_hashTable[i] = kInvalidPosition;
    }

    const std::uint8_t* const inputEnd = input + inputSize;
    const std::uint8_t* anchor = input;
    const std::uint8_t* cursor = input;
    std::uint8_t* outputCursor = output;
    const std::uint8_t* const outputEnd = output + outputSize;

    // Keeping five trailing literals produces blocks accepted by strict LZ4
    // decoders. Requiring twelve bytes here also respects LZ4's match limit.
    while (static_cast<std::size_t>(inputEnd - cursor) >= 12U) {
        const std::size_t hash = HashSequence(cursor);
        const std::uint16_t referencePosition = g_hashTable[hash];
        g_hashTable[hash] = static_cast<std::uint16_t>(cursor - input);

        if (referencePosition == kInvalidPosition) {
            ++cursor;
            continue;
        }

        const std::uint8_t* reference = input + referencePosition;
        if (Read32(reference) != Read32(cursor)) {
            ++cursor;
            continue;
        }

        const std::size_t literalLength =
            static_cast<std::size_t>(cursor - anchor);
        std::uint8_t* const token = outputCursor;
        if (outputCursor == outputEnd) {
            return LZ4EncodeResult::OutputTooSmall;
        }
        ++outputCursor;

        *token = static_cast<std::uint8_t>(
            (literalLength < 15U ? literalLength : 15U) << 4U);
        if (literalLength >= 15U &&
            !WriteLength(literalLength - 15U, outputCursor, outputEnd)) {
            return LZ4EncodeResult::OutputTooSmall;
        }
        if (literalLength > static_cast<std::size_t>(outputEnd - outputCursor)) {
            return LZ4EncodeResult::OutputTooSmall;
        }
        for (std::size_t i = 0U; i < literalLength; ++i) {
            *outputCursor++ = anchor[i];
        }

        const std::size_t matchOffset =
            static_cast<std::size_t>(cursor - reference);
        if (outputEnd - outputCursor < 2) {
            return LZ4EncodeResult::OutputTooSmall;
        }
        *outputCursor++ = static_cast<std::uint8_t>(matchOffset);
        *outputCursor++ = static_cast<std::uint8_t>(matchOffset >> 8U);

        const std::uint8_t* const matchStart = cursor;
        cursor += 4U;
        reference += 4U;
        const std::uint8_t* const matchEnd = inputEnd - 5U;
        while (cursor < matchEnd && *cursor == *reference) {
            ++cursor;
            ++reference;
        }

        const std::size_t encodedMatchLength =
            static_cast<std::size_t>(cursor - matchStart) - 4U;
        *token |= static_cast<std::uint8_t>(
            encodedMatchLength < 15U ? encodedMatchLength : 15U);
        if (encodedMatchLength >= 15U &&
            !WriteLength(encodedMatchLength - 15U,
                         outputCursor, outputEnd)) {
            return LZ4EncodeResult::OutputTooSmall;
        }
        anchor = cursor;
    }

    const std::size_t literalLength =
        static_cast<std::size_t>(inputEnd - anchor);
    if (outputCursor == outputEnd) {
        return LZ4EncodeResult::OutputTooSmall;
    }
    *outputCursor++ = static_cast<std::uint8_t>(
        (literalLength < 15U ? literalLength : 15U) << 4U);
    if (literalLength >= 15U &&
        !WriteLength(literalLength - 15U, outputCursor, outputEnd)) {
        return LZ4EncodeResult::OutputTooSmall;
    }
    if (literalLength > static_cast<std::size_t>(outputEnd - outputCursor)) {
        return LZ4EncodeResult::OutputTooSmall;
    }
    for (std::size_t i = 0U; i < literalLength; ++i) {
        *outputCursor++ = anchor[i];
    }

    outputSize = static_cast<std::size_t>(outputCursor - output);
    return LZ4EncodeResult::Success;
}

LZ4DecodeResult DecodeLZ4Block(const std::uint8_t* input,
                               std::size_t inputSize,
                               std::uint8_t* output,
                               std::size_t outputSize) {
    if (input == nullptr || output == nullptr || inputSize == 0U) {
        return LZ4DecodeResult::MalformedInput;
    }

    const std::uint8_t* inputCursor = input;
    const std::uint8_t* const inputEnd = input + inputSize;
    std::size_t outputOffset = 0U;

    while (inputCursor < inputEnd) {
        const std::uint8_t token = *inputCursor++;

        std::size_t literalLength = token >> 4U;
        if (!ReadExtendedLength(inputCursor, inputEnd, literalLength) ||
            literalLength > static_cast<std::size_t>(inputEnd - inputCursor) ||
            literalLength > outputSize - outputOffset) {
            return LZ4DecodeResult::MalformedInput;
        }

        for (std::size_t i = 0U; i < literalLength; ++i) {
            output[outputOffset++] = *inputCursor++;
        }

        // A raw LZ4 block ends with a literal-only sequence.
        if (inputCursor == inputEnd) {
            return outputOffset == outputSize
                       ? LZ4DecodeResult::Success
                       : LZ4DecodeResult::OutputSizeMismatch;
        }

        if (inputEnd - inputCursor < 2) {
            return LZ4DecodeResult::MalformedInput;
        }
        const std::size_t matchOffset =
            static_cast<std::size_t>(inputCursor[0]) |
            (static_cast<std::size_t>(inputCursor[1]) << 8U);
        inputCursor += 2;
        if (matchOffset == 0U || matchOffset > outputOffset) {
            return LZ4DecodeResult::MalformedInput;
        }

        std::size_t matchLength = token & 0x0FU;
        if (!ReadExtendedLength(inputCursor, inputEnd, matchLength) ||
            matchLength > std::numeric_limits<std::size_t>::max() - 4U) {
            return LZ4DecodeResult::MalformedInput;
        }
        matchLength += 4U;
        if (matchLength > outputSize - outputOffset) {
            return LZ4DecodeResult::MalformedInput;
        }

        // Bytewise copying intentionally supports overlapping matches.
        for (std::size_t i = 0U; i < matchLength; ++i) {
            output[outputOffset] = output[outputOffset - matchOffset];
            ++outputOffset;
        }
    }

    return LZ4DecodeResult::OutputSizeMismatch;
}

} // namespace Kernel
