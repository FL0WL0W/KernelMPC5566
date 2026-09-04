#include "ON20845-007Device.h"

#include <cstddef>

namespace E78
{
	bool ON20845_007Device::ServiceWatchdog()
	{
		// The bootloader descriptor's completion callback immediately executes
		// the transaction a second time. Its pre-transfer callback complements
		// bits 9..13 before each transfer, producing an opposite-phase pair.
		if (!_service.Ready())
			return false;

		const std::uint16_t current = static_cast<std::uint16_t>(
			(static_cast<std::uint16_t>(_watchdogBuffer[0]) << 8U) |
			_watchdogBuffer[1]);
		const std::uint16_t next = static_cast<std::uint16_t>(
			((~((current >> 9U) & 0x1FU) & 0x1FU) << 9U) |
			(current & 0xC1FFU));
		_watchdogBuffer[0] = static_cast<std::uint8_t>(next >> 8U);
		_watchdogBuffer[1] = static_cast<std::uint8_t>(next);
		if (!_service.Transfer(
				_watchdogBuffer,
				sizeof(_watchdogBuffer),
				nullptr))
		{
			_watchdogBuffer[0] = static_cast<std::uint8_t>(current >> 8U);
			_watchdogBuffer[1] = static_cast<std::uint8_t>(current);
			return false;
		}
		return true;
	}
}
