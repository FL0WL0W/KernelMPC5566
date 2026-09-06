#ifndef ON20845_007DEVICE_H
#define ON20845_007DEVICE_H

#include "ISPIService.h"

#include <cstdint>

namespace E78
{
	class ON20845_007Device final
	{
	private:
		EmbeddedIOServices::ISPIService& _service;

	protected:
		std::uint8_t _watchdogBuffer[6];

	public:
		explicit ON20845_007Device(
			EmbeddedIOServices::ISPIService& service)
			: _service(service),
			  _watchdogBuffer{0x6AU, 0x2CU, 0x00U, 0x00U, 0x00U, 0x00U} {}

		bool ServiceWatchdog();
	};
}

#endif
