#ifndef E78_SPI_SYSTEM_H
#define E78_SPI_SYSTEM_H

#include "Delphi28046304Device.h"
#include "MPC5xxxDSISerialOutputService.h"
#include "MPC5xxxSPIService.h"
#include "MPMDevice.h"
#include "MPMSPIService.h"
#include "ON20845-007Device.h"

namespace E78
{
	class E78SPISystem final
	{
	private:
		MPC5xxx::MPC5xxxSPIService _on20845SPI;
		MPMSPIService _mpmSPI;
		MPC5xxx::MPC5xxxSPIService _delphi28046304SPI;
		MPC5xxx::MPC5xxxDSISerialOutputService _delphiDigitalOutputService;

	public:
		ON20845_007Device ON20845;
		MPMDevice MPM;
		Delphi28046304Device Delphi28046304;
		EmbeddedIOServices::IDigitalService& DelphiDigitalOutputs;

		E78SPISystem();

		void Service();
		void ServiceWatchdogs();
	};
}

#endif
