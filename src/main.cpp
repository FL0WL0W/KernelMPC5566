#include "E78SPISystem.h"
#include "MPC5xxxDigitalService.h"
#include "MPC5xxxFlexCAN2Service.h"
#include "UDSService.h"

#include <cstddef>
#include <cstdint>

using namespace EmbeddedIOServices;
using namespace MPC5xxx;

extern "C" __attribute__((weak)) bool WriteToFlash(
	std::uint32_t address,
	const std::uint8_t* data,
	std::size_t length)
{
	(void)address;
	(void)data;
	(void)length;
	return false;
}

namespace
{
	constexpr std::uint32_t LoopPeriodTimebaseTicks = 0x0005DC00U;
	constexpr digitalpin_t FirstInjectorPin = 132U;
	constexpr digitalpin_t FirstIgnitionPin = 167U;
	constexpr std::size_t EngineOutputCount = 8U;
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

	E78::E78SPISystem spiSystem;
	spiSystem.ON20845.SendOutputConfiguration();
	spiSystem.DelphiDigitalOutputs.InitPin(4U, Out);
	spiSystem.DelphiDigitalOutputs.WritePin(4U, true);
	spiSystem.Delphi28046304.RequestIdentification(nullptr);
	spiSystem.Delphi28046304.RequestIdentification(nullptr);
	spiSystem.Delphi28046304.SendRevision4Configuration(nullptr);
	spiSystem.Delphi28046304.SendCommand(0x0F1AU, 0x0082U, nullptr);
	spiSystem.Delphi28046304.SendCommand(0x0F1DU, 0x1450U, nullptr);
	spiSystem.Delphi28046304.SendCommand(0x0F1DU, 0x04F0U, nullptr);
	spiSystem.Delphi28046304.RequestDiagnostic(nullptr);
	spiSystem.Delphi28046304.SendCommand(0x0F14U, 0x3E20U, nullptr);
	MPC5xxxDigitalService digitalService;
	for (std::size_t channel = 0U; channel < EngineOutputCount; ++channel)
	{
		const digitalpin_t injector = static_cast<digitalpin_t>(
			FirstInjectorPin + channel);
		const digitalpin_t ignition = static_cast<digitalpin_t>(
			FirstIgnitionPin + channel);
		digitalService.WritePin(injector, false);
		digitalService.WritePin(ignition, false);
		digitalService.InitPin(injector, Out);
		digitalService.InitPin(ignition, Out);
	}
	volatile FLEXCAN2_tag* canModules[] = {&CAN_A};
	const CANBaudRate canBaudRates[] = {CANBaudRate::Kbps500};
	MPC5xxxFlexCAN2Service canService(canModules, canBaudRates, 1U);
	ICommunicationService* const isotp = canService.GetISOTPService(
		{0x7E0U, 0U},
		{0x7E8U, 0U});
	const E78::UDSMemoryRegion udsReadRegions[] = {
		{0x00000000U, 0x00003FE0U, true},
		{0x00004000U, 0x0001BFE0U, true},
		{0x00020000U, 0x003E0000U, true},
		{0x40000000U, 0x00040000U, false},
	};
	const E78::UDSMemoryRegion udsWriteRegions[] = {
		{0x00000000U, 0x00400000U, true},
		{0x40000000U, 0x00040000U, false},
	};
	E78::UDSService uds(
		*isotp,
		udsReadRegions,
		sizeof(udsReadRegions) / sizeof(udsReadRegions[0]),
		udsWriteRegions,
		sizeof(udsWriteRegions) / sizeof(udsWriteRegions[0]),
		WriteToFlash);

	const std::uint8_t alive = 0x99U;
	bool engineOutputState = false;
	std::uint32_t loopStart = ReadTimebase();
	while (true)
	{
		canService.PollFlexCAN(CAN_A);
		spiSystem.Service();

		const std::uint32_t now = ReadTimebase();
		if (static_cast<std::uint32_t>(now - loopStart) <
			LoopPeriodTimebaseTicks)
			continue;

		loopStart = now;
		ServiceCoreWatchdog();
		spiSystem.ServiceWatchdogs();
		engineOutputState = !engineOutputState;
		for (std::size_t channel = 0U; channel < EngineOutputCount; ++channel)
		{
			digitalService.WritePin(
				static_cast<digitalpin_t>(FirstInjectorPin + channel),
				engineOutputState);
			digitalService.WritePin(
				static_cast<digitalpin_t>(FirstIgnitionPin + channel),
				engineOutputState);
		}
		isotp->Send(&alive, 1U);
	}
}
