#include "E78SPISystem.h"

#include "MPC5xxx.h"

namespace
{
	constexpr MPC5xxx::MPC5xxxSPIServiceConfiguration ON20845Configuration = {
		0U,
		62500U,
		8U,
		MPC5xxx::SPIClockPolarity::IdleLow,
		MPC5xxx::SPIClockPhase::CaptureOnTrailingEdge,
		1750U,
		640000U,
		219U,
		false,
		false,
	};

	constexpr MPC5xxx::MPC5xxxSPIServiceConfiguration MPMConfiguration = {
		1U,
		62500U,
		8U,
		MPC5xxx::SPIClockPolarity::IdleLow,
		MPC5xxx::SPIClockPhase::CaptureOnTrailingEdge,
		1750U,
		640000U,
		219U,
		false,
		false,
	};

	constexpr MPC5xxx::MPC5xxxSPIServiceConfiguration DelphiConfiguration = {
		0U,
		2666667U,
		16U,
		MPC5xxx::SPIClockPolarity::IdleLow,
		MPC5xxx::SPIClockPhase::CaptureOnLeadingEdge,
		1000U,
		1000U,
		0U,
		false,
		false,
	};
}

namespace E78
{
	E78SPISystem::E78SPISystem()
		: _on20845SPI(&DSPI_D, ON20845Configuration),
		  _mpmSPI(&DSPI_D, MPMConfiguration),
		  _delphi28046304SPI(&DSPI_B, DelphiConfiguration),
		  _delphiDigitalOutputService(
			  &DSPI_A,
			  &DSPI_C,
			  0x113F0C01U,
			  0x913F0C01U,
			  0x22003341U,
			  0x7A003341U,
			  0x94080001U,
			  0x001FFFFFU,
			  0U),
		  ON20845(_on20845SPI),
		  MPM(_mpmSPI),
		  Delphi28046304(_delphi28046304SPI),
		  DelphiDigitalOutputs(_delphiDigitalOutputService)
	{
		MPM.InitializeNormalMode();
	}

	void E78SPISystem::Service()
	{
		MPC5xxx::MPC5xxxSPIService::Service(DSPI_B);
		MPC5xxx::MPC5xxxSPIService::Service(DSPI_D);
	}

	void E78SPISystem::ServiceWatchdogs()
	{
		ON20845.ServiceWatchdog();
		MPM.ServiceNormalMode();
		Delphi28046304.ServiceWatchdog();
	}
}
