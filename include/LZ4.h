#pragma once

#include <cstddef>
#include <cstdint>

namespace Kernel {

enum class LZ4DecodeResult {
    Success,
    MalformedInput,
    OutputSizeMismatch,
};

enum class LZ4EncodeResult {
    Success,
    InputTooLarge,
    OutputTooSmall,
};

// Decodes one independent raw LZ4 block. The caller supplies the exact
// expected output size; no frame header, checksum, dictionary, or allocation
// is used.
LZ4DecodeResult DecodeLZ4Block(const std::uint8_t* input,
                               std::size_t inputSize,
                               std::uint8_t* output,
                               std::size_t outputSize);

// Encodes one independent raw LZ4 block. outputSize is the output capacity on
// entry and the encoded byte count on success. Input is limited to 4096 bytes.
LZ4EncodeResult EncodeLZ4Block(const std::uint8_t* input,
                               std::size_t inputSize,
                               std::uint8_t* output,
                               std::size_t& outputSize);

} // namespace Kernel
