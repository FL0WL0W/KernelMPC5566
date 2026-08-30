#include "MPMSPIService.h"

namespace E78
{
	MPC5xxx::SPIFrameTiming MPMSPIService::TimingForFrame(
		std::size_t frameIndex) const
	{
		if (frameIndex == 0U)
			return {1750U, 640000U, 219U};
		if (frameIndex == 1U)
			return {1750U, 192000U, 28000U};
		return {1750U, 56000U, 28000U};
	}
}
