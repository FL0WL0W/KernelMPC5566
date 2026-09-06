#include "MPC55xxSystemClockService.h"
#include "MPC5xxxFlexCAN2Service.h"
#include "MPC5xxxSPIService.h"
#include "MPC5566FlashService.h"
#include "ON20845-007Device.h"
#include "UDSService.h"

#include <cstddef>
#include <cstdint>

using namespace EmbeddedIOServices;
using namespace MPC5xxx;

extern "C" [[noreturn]] void ExitToBootloaderUploadRoutine();

namespace
{
	constexpr std::uint32_t LoopPeriodTimebaseTicks = 0x00061A80U;
	constexpr MPC5xxxSPIServiceConfiguration ON20845Configuration = {
		0U,
		4800000U,
		16U,
		SPIClockPolarity::IdleLow,
		SPIClockPhase::CaptureOnLeadingEdge,
		500U,
		1500U,
		3000U,
		false,
		false,
	};

	std::uint32_t ReadTimebase()
	{
		std::uint32_t value;
		asm volatile("mftb %0" : "=r"(value));
		return value;
	}

	void ServiceCoreWatchdog()
	{
		const std::uint32_t watchdogService = 0x40000000U;
		asm volatile(
			"isync\n"
			"mtspr 336, %0\n"
			"isync\n"
			:
			: "r"(watchdogService)
			: "memory");
	}
}

extern "C" int main()
{
	asm("wrteei 0");

	MPC55xxSystemClockService::Initialize(8000000U, 128000000U);
	MPC5xxxSPIService on20845SPI(&DSPI_D, ON20845Configuration);
	// The bootloader callback stores the word it just transmitted here. Seed
	// the kernel from that live value so the next service uses the opposite
	// rolling phase rather than restarting at an arbitrary phase.
	E78::ON20845_007Device on20845(
		on20845SPI);

    MPC5xxxFlexCAN2Service::Initialize(CAN_A, CANBaudRate::Kbps500);
	ICommunicationService* const isotp = MPC5xxxFlexCAN2Service::Instance().GetISOTPService(
		{0x7E0U, 0U},
		{0x7E8U, 0U});
	const E78::UDSMemoryRegion udsReadRegions[] = {
		{0x00000000U, 0x00003FE0U, true},
		{0x00004000U, 0x0001BFE0U, true},
		{0x00020000U, 0x002E0000U, true},
		{0x40000000U, 0x00020000U, false},
	};
	const E78::UDSMemoryRegion udsWriteRegions[] = {
		{0x00000000U, 0x00300000U, true},
		{0x40000000U, 0x00020000U, false},
	};
	E78::MPC5566FlashService flashService;
	E78::UDSService uds(
		*isotp,
		udsReadRegions,
		sizeof(udsReadRegions) / sizeof(udsReadRegions[0]),
		udsWriteRegions,
		sizeof(udsWriteRegions) / sizeof(udsWriteRegions[0]),
		[&flashService](std::uint32_t address,
			const std::uint8_t* data,
			std::size_t length,
			E78::UDSFlashWriteCompletion completion) {
			return flashService.QueueWrite(address, data, length, completion);
		},
		ExitToBootloaderUploadRoutine,
		[&flashService](std::uint8_t subFunction,
			std::uint16_t routineIdentifier,
			const std::uint8_t* optionRecord,
			std::size_t optionRecordLength,
			const communication_send_callback_t& send) {
			return flashService.HandleRoutineControl(
				subFunction,
				routineIdentifier,
				optionRecord,
				optionRecordLength,
				send);
		});
	(void)uds;

	const uint8_t readyResponse[] = {0x99};
	if (isotp->Ready())
		isotp->Send(readyResponse, 1U);

	std::uint32_t loopStart = ReadTimebase();
	while (true)
	{
		MPC5xxxFlexCAN2Service::PollFlexCAN(CAN_A);
		MPC5xxxSPIService::Service(DSPI_D);

		const std::uint32_t now = ReadTimebase();
		if (isotp->Ready())
			flashService.Service(now);
		if (static_cast<std::uint32_t>(now - loopStart) <
			LoopPeriodTimebaseTicks)
			continue;

		loopStart = now;
		ServiceCoreWatchdog();
		on20845.ServiceWatchdog();
	}
}
