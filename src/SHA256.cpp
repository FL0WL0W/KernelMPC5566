#include "SHA256.h"

namespace
{
	constexpr std::uint32_t K[64] = {
		0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
		0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
		0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
		0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
		0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
		0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
		0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
		0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
		0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
		0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
		0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
		0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
		0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
		0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
		0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
		0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
	};

	std::uint32_t RotateRight(std::uint32_t value, std::uint32_t count)
	{
		return (value >> count) | (value << (32U - count));
	}
}

namespace Kernel
{
	SHA256::SHA256()
	{
		Reset();
	}

	void SHA256::Reset()
	{
		_state[0] = 0x6A09E667U;
		_state[1] = 0xBB67AE85U;
		_state[2] = 0x3C6EF372U;
		_state[3] = 0xA54FF53AU;
		_state[4] = 0x510E527FU;
		_state[5] = 0x9B05688CU;
		_state[6] = 0x1F83D9ABU;
		_state[7] = 0x5BE0CD19U;
		_byteCount = 0U;
		_bufferLength = 0U;
	}

	void SHA256::Transform(const std::uint8_t block[64])
	{
		std::uint32_t words[64];
		for (std::size_t i = 0U; i < 16U; ++i)
		{
			const std::size_t offset = i * 4U;
			words[i] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
				(static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
				(static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
				block[offset + 3U];
		}
		for (std::size_t i = 16U; i < 64U; ++i)
		{
			const std::uint32_t s0 = RotateRight(words[i - 15U], 7U) ^
				RotateRight(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
			const std::uint32_t s1 = RotateRight(words[i - 2U], 17U) ^
				RotateRight(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
			words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
		}

		std::uint32_t a = _state[0];
		std::uint32_t b = _state[1];
		std::uint32_t c = _state[2];
		std::uint32_t d = _state[3];
		std::uint32_t e = _state[4];
		std::uint32_t f = _state[5];
		std::uint32_t g = _state[6];
		std::uint32_t h = _state[7];
		for (std::size_t i = 0U; i < 64U; ++i)
		{
			const std::uint32_t sum1 = RotateRight(e, 6U) ^
				RotateRight(e, 11U) ^ RotateRight(e, 25U);
			const std::uint32_t choose = (e & f) ^ (~e & g);
			const std::uint32_t temporary1 = h + sum1 + choose + K[i] + words[i];
			const std::uint32_t sum0 = RotateRight(a, 2U) ^
				RotateRight(a, 13U) ^ RotateRight(a, 22U);
			const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
			const std::uint32_t temporary2 = sum0 + majority;
			h = g;
			g = f;
			f = e;
			e = d + temporary1;
			d = c;
			c = b;
			b = a;
			a = temporary1 + temporary2;
		}
		_state[0] += a;
		_state[1] += b;
		_state[2] += c;
		_state[3] += d;
		_state[4] += e;
		_state[5] += f;
		_state[6] += g;
		_state[7] += h;
	}

	void SHA256::Update(const volatile std::uint8_t* data, std::size_t length)
	{
		if (data == nullptr)
			return;
		_byteCount += length;
		while (length-- != 0U)
		{
			_buffer[_bufferLength++] = *data++;
			if (_bufferLength == sizeof(_buffer))
			{
				Transform(_buffer);
				_bufferLength = 0U;
			}
		}
	}

	void SHA256::Final(std::uint8_t digest[32])
	{
		const std::uint64_t bitCount = _byteCount * 8U;
		_buffer[_bufferLength++] = 0x80U;
		if (_bufferLength > 56U)
		{
			while (_bufferLength < 64U)
				_buffer[_bufferLength++] = 0U;
			Transform(_buffer);
			_bufferLength = 0U;
		}
		while (_bufferLength < 56U)
			_buffer[_bufferLength++] = 0U;
		for (std::size_t i = 0U; i < 8U; ++i)
			_buffer[56U + i] = static_cast<std::uint8_t>(bitCount >> (56U - 8U * i));
		Transform(_buffer);
		for (std::size_t i = 0U; i < 8U; ++i)
		{
			digest[i * 4U] = static_cast<std::uint8_t>(_state[i] >> 24U);
			digest[i * 4U + 1U] = static_cast<std::uint8_t>(_state[i] >> 16U);
			digest[i * 4U + 2U] = static_cast<std::uint8_t>(_state[i] >> 8U);
			digest[i * 4U + 3U] = static_cast<std::uint8_t>(_state[i]);
		}
	}
}
