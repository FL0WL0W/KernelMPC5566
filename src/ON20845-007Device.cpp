#include "ON20845-007Device.h"

#include <cstddef>

namespace E78
{
	bool ON20845_007Device::SendCommand(
		std::uint8_t first,
		std::uint8_t second)
	{
		std::uint8_t command[] = {first, second};
		return _service.Transfer(command, sizeof(command), nullptr);
	}

	void ON20845_007Device::ServiceWatchdog()
	{
		// The periodic watchdog is best-effort and non-blocking.
		if (!_service.Ready())
			return;

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
		}
	}

	void ON20845_007Device::SendGroup5Base()
	{
		SendCommand(0x6AU, 0x0CU);
	}

	void ON20845_007Device::SendGroup5Enabled()
	{
		SendCommand(0x6AU, 0x2CU);
	}

	void ON20845_007Device::SendGroup4()
	{
		SendCommand(0x00U, 0x00U);
	}

	void ON20845_007Device::SendGroup6(std::uint8_t control)
	{
		SendCommand(0x80U, control);
	}

	void ON20845_007Device::SendOutputConfiguration()
	{
		SendGroup6(0xFCU);
	}
}
