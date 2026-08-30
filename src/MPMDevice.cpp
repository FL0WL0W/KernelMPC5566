#include "MPMDevice.h"

#include "MPC5xxx.h"

namespace
{
	constexpr std::size_t kNormalModeEnableGPIO = 182U;
}

namespace E78
{
	std::uint8_t MPMDevice::ComputeXor() const
	{
		std::uint8_t checksum = 0U;
		for (std::size_t i = 0U; i + 1U < PacketLength; ++i)
			checksum ^= _transmit[i];
		return checksum;
	}

	void MPMDevice::InitializeNormalMode()
	{
		SIU.GPDO[kNormalModeEnableGPIO].B.PDO = 1U;
		SIU.PCR[kNormalModeEnableGPIO].R = 0x0310U;
		_transmit[3] = 0x01U;
		_transmit[4] = 0x00U;
		_transmit[5] = 0x00U;
		_transmit[11] = 0x00U;
	}

	void MPMDevice::ServiceNormalMode()
	{
		_transmit[0] = _transmit[0] == 0x19U ? 0x06U : 0x19U;
		_transmit[17] = ComputeXor();

		std::uint8_t packet[PacketLength] = {};
		for (std::size_t i = 0U; i < PacketLength; ++i)
			packet[i] = _transmit[i];
		_service.Transfer(
			packet,
			sizeof(packet),
			[this](std::uint8_t* response, std::size_t length) {
				const std::size_t bytesToCopy =
					length < PacketLength ? length : PacketLength;
				for (std::size_t i = 0U; i < bytesToCopy; ++i)
					_response[i] = response[i];
			});
	}
}
