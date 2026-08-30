#ifndef MPM_SPI_SERVICE_H
#define MPM_SPI_SERVICE_H

#include "MPC5xxxSPIService.h"

namespace E78
{
	/**
	 * @brief MPM-specific DSPI timing.
	 *
	 * The stock E78 uses different inter-word delays for the first byte, second
	 * byte, and remaining bytes. Keeping that exception here leaves the common
	 * MPC5xxx SPI configuration identical to an ordinary SPI device setup.
	 */
	class MPMSPIService final : public MPC5xxx::MPC5xxxSPIService
	{
	protected:
		MPC5xxx::SPIFrameTiming TimingForFrame(
			std::size_t frameIndex) const override;

	public:
		using MPC5xxx::MPC5xxxSPIService::MPC5xxxSPIService;
	};
}

#endif
