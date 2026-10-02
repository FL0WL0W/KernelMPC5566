#ifndef ON20845_007DEVICE_H
#define ON20845_007DEVICE_H

#include "ISPIService.h"

#include <cstddef>
#include <cstdint>

namespace E78
{
	class ON20845_007Device final
	{
	private:
		EmbeddedIOServices::ISPIService& _service;
		std::uint8_t _commandBuffer[2] = {};
		volatile bool _commandPending = false;
		volatile bool _watchdogPending = false;
		bool SendCommand(std::uint8_t first, std::uint8_t second);

	protected:
		std::uint8_t _watchdogBuffer[6];

	public:
		static constexpr std::size_t MaximumTransferLength = 6U;

		explicit ON20845_007Device(EmbeddedIOServices::ISPIService& service)
			: _service(service),
			  _watchdogBuffer{0x6AU, 0x2CU, 0x00U, 0x00U, 0x00U, 0x00U} {}

		void ServiceWatchdog();
		void SendOutputConfiguration();
		void SendGroup5Base();
		void SendGroup5Enabled();
		void SendGroup4();
		void SendGroup6(std::uint8_t control);
	};
}

#endif
