#ifndef SHA256_H
#define SHA256_H

#include <cstddef>
#include <cstdint>

namespace Kernel
{
	class SHA256 final
	{
	private:
		std::uint32_t _state[8];
		std::uint64_t _byteCount;
		std::uint8_t _buffer[64];
		std::size_t _bufferLength;

		void Transform(const std::uint8_t block[64]);

	public:
		SHA256();
		void Reset();
		void Update(const volatile std::uint8_t* data, std::size_t length);
		void Final(std::uint8_t digest[32]);
	};
}

#endif
