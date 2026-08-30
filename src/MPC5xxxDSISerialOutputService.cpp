#include "MPC5xxxDSISerialOutputService.h"

namespace
{
	constexpr std::uint32_t kHalt = 0x00000001U;
	constexpr std::uint32_t kPCS0 = 0x00010000U;
	constexpr EmbeddedIOServices::digitalpin_t kOutputCount = 21U;
}

namespace MPC5xxx
{
	MPC5xxxDSISerialOutputService::MPC5xxxDSISerialOutputService(
		volatile DSPI_tag* leadingModule,
		volatile DSPI_tag* clockingModule,
		std::uint32_t leadingModuleConfiguration,
		std::uint32_t clockingModuleConfiguration,
		std::uint32_t leadingClockTransferAttributes,
		std::uint32_t clockingClockTransferAttributes,
		std::uint32_t serialConfiguration,
		std::uint32_t outputMask,
		std::uint32_t initialValue)
		: _leadingModule(leadingModule),
		  _clockingModule(clockingModule),
		  _outputMask(outputMask)
	{
		// E78 routing: DSPI-C supplies SCK/PCS0, and the chained 21-bit
		// stream exits through SOUTA after DSPI-A's five leading bits.
		SIU.DISR.R = 0xA8000100U;

		_leadingModule->MCR.R = leadingModuleConfiguration | kHalt;
		_clockingModule->MCR.R = clockingModuleConfiguration | kHalt;
		_leadingModule->TCR.R = 0U;
		_clockingModule->TCR.R = 0U;
		_leadingModule->RSER.R = 0U;
		_clockingModule->RSER.R = 0U;
		_leadingModule->CTAR[1].R = leadingClockTransferAttributes;
		_clockingModule->CTAR[0].R = clockingClockTransferAttributes;
		_leadingModule->DSICR.R = serialConfiguration;
		_clockingModule->DSICR.R = serialConfiguration;
		_leadingModule->PUSHR.R = kPCS0;
		_clockingModule->PUSHR.R = kPCS0;
		Set(initialValue);
		SIU.ECCR.R = 0x00000801U;
		_leadingModule->MCR.R = leadingModuleConfiguration & ~kHalt;
		_clockingModule->MCR.R = clockingModuleConfiguration & ~kHalt;
	}

	void MPC5xxxDSISerialOutputService::Set(std::uint32_t value)
	{
		_value = value & _outputMask;
		_leadingModule->ASDR.R = (_value >> 16U) & 0x001FU;
		_clockingModule->ASDR.R = _value & 0xFFFFU;
	}

	void MPC5xxxDSISerialOutputService::InitPin(
		EmbeddedIOServices::digitalpin_t pin,
		EmbeddedIOServices::PinDirection direction)
	{
		(void)pin;
		(void)direction;
	}

	bool MPC5xxxDSISerialOutputService::ReadPin(
		EmbeddedIOServices::digitalpin_t pin)
	{
		return pin < kOutputCount && (_value & (1UL << pin)) != 0U;
	}

	void MPC5xxxDSISerialOutputService::WritePin(
		EmbeddedIOServices::digitalpin_t pin,
		bool value)
	{
		if (pin >= kOutputCount)
			return;
		const std::uint32_t bit = 1UL << pin;
		Set(value ? _value | bit : _value & ~bit);
	}

	void MPC5xxxDSISerialOutputService::AttachInterrupt(
		EmbeddedIOServices::digitalpin_t pin,
		EmbeddedIOServices::callback_t callBack)
	{
		(void)pin;
		(void)callBack;
	}

	void MPC5xxxDSISerialOutputService::DetachInterrupt(
		EmbeddedIOServices::digitalpin_t pin)
	{
		(void)pin;
	}
}
