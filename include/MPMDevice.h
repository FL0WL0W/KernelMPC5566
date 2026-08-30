#ifndef MPMDEVICE_H
#define MPMDEVICE_H

#include "ISPIService.h"

#include <cstddef>
#include <cstdint>

namespace E78
{
	class MPMDevice final
	{
	public:
		static constexpr std::size_t PacketLength = 18U;

	private:
		EmbeddedIOServices::ISPIService& _service;
		volatile std::uint8_t _transmit[PacketLength] = {
			0x06U, 0xFFU, 0xFFU, 0x01U, 0x00U, 0x00U,
			0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
			0xFFU, 0x53U, 0x10U, 0x00U, 0x09U, 0xB2U,
		};
		volatile std::uint8_t _response[PacketLength] = {};

		std::uint8_t ComputeXor() const;

	public:
		explicit MPMDevice(EmbeddedIOServices::ISPIService& service)
			: _service(service) {}

		void InitializeNormalMode();
		void ServiceNormalMode();

		const volatile std::uint8_t* TransmitPacket() const { return _transmit; }
		const volatile std::uint8_t* ResponsePacket() const { return _response; }
	};
}

#endif
