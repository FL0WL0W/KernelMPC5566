#include "LZ4BlockDecoder.h"

#include <cassert>
#include <cstdint>
#include <cstring>

int main() {
    {
        const std::uint8_t encoded[] = {0x50U, 'h', 'e', 'l', 'l', 'o'};
        std::uint8_t decoded[5] = {};
        assert(Kernel::DecodeLZ4Block(encoded, sizeof(encoded), decoded,
                                      sizeof(decoded)) ==
               Kernel::LZ4DecodeResult::Success);
        assert(std::memcmp(decoded, "hello", sizeof(decoded)) == 0);
    }
    {
        // Three literals, an overlapping four-byte match, then final literals.
        const std::uint8_t encoded[] = {
            0x30U, 'a', 'b', 'c', 0x03U, 0x00U,
            0x50U, 'b', 'c', 'X', 'Y', 'Z'
        };
        std::uint8_t decoded[12] = {};
        assert(Kernel::DecodeLZ4Block(encoded, sizeof(encoded), decoded,
                                      sizeof(decoded)) ==
               Kernel::LZ4DecodeResult::Success);
        assert(std::memcmp(decoded, "abcabcabcXYZ", sizeof(decoded)) == 0);
    }
    {
        const std::uint8_t zeroOffset[] = {
            0x10U, 'x', 0x00U, 0x00U
        };
        std::uint8_t decoded[5] = {};
        assert(Kernel::DecodeLZ4Block(zeroOffset, sizeof(zeroOffset), decoded,
                                      sizeof(decoded)) ==
               Kernel::LZ4DecodeResult::MalformedInput);
    }
    {
        const std::uint8_t encoded[] = {0x10U, 'x'};
        std::uint8_t decoded[2] = {};
        assert(Kernel::DecodeLZ4Block(encoded, sizeof(encoded), decoded,
                                      sizeof(decoded)) ==
               Kernel::LZ4DecodeResult::OutputSizeMismatch);
    }
    {
        std::uint8_t input[4096];
        std::memset(input, 0xA5, sizeof(input));
        std::uint8_t encoded[251] = {};
        std::size_t encodedSize = sizeof(encoded);
        assert(Kernel::EncodeLZ4Block(input, sizeof(input), encoded,
                                      encodedSize) ==
               Kernel::LZ4EncodeResult::Success);
        assert(encodedSize < sizeof(encoded));

        std::uint8_t decoded[4096] = {};
        assert(Kernel::DecodeLZ4Block(encoded, encodedSize, decoded,
                                      sizeof(decoded)) ==
               Kernel::LZ4DecodeResult::Success);
        assert(std::memcmp(input, decoded, sizeof(input)) == 0);
    }
    {
        std::uint8_t input[234];
        for (std::size_t i = 0U; i < sizeof(input); ++i) {
            input[i] = static_cast<std::uint8_t>(i);
        }
        std::uint8_t encoded[251] = {};
        std::size_t encodedSize = sizeof(encoded);
        assert(Kernel::EncodeLZ4Block(input, sizeof(input), encoded,
                                      encodedSize) ==
               Kernel::LZ4EncodeResult::Success);

        std::uint8_t decoded[234] = {};
        assert(Kernel::DecodeLZ4Block(encoded, encodedSize, decoded,
                                      sizeof(decoded)) ==
               Kernel::LZ4DecodeResult::Success);
        assert(std::memcmp(input, decoded, sizeof(input)) == 0);
    }
}
