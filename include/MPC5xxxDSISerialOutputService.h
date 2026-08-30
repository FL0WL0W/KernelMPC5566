#ifndef MPC5XXX_DSI_SERIAL_OUTPUT_SERVICE_H
#define MPC5XXX_DSI_SERIAL_OUTPUT_SERVICE_H

#include "IDigitalService.h"
#include "MPC5xxx.h"

#include <cstdint>

namespace MPC5xxx
{
	/**
	 * @brief MPC5xxx two-module DSI chain used by the Delphi ASIC serial input.
	 *
	 * This is a continuously repeated output rather than a full-duplex SPI
	 * transaction, so it intentionally does not implement ISPIService.
	 */
	class MPC5xxxDSISerialOutputService final
		: public EmbeddedIOServices::IDigitalService
	{
	private:
		volatile DSPI_tag* const _leadingModule;
		volatile DSPI_tag* const _clockingModule;
		const std::uint32_t _outputMask;
		volatile std::uint32_t _value = 0U;

	public:
		MPC5xxxDSISerialOutputService(
			volatile DSPI_tag* leadingModule,
			volatile DSPI_tag* clockingModule,
			std::uint32_t leadingModuleConfiguration,
			std::uint32_t clockingModuleConfiguration,
			std::uint32_t leadingClockTransferAttributes,
			std::uint32_t clockingClockTransferAttributes,
			std::uint32_t serialConfiguration,
			std::uint32_t outputMask,
			std::uint32_t initialValue);

		void Set(std::uint32_t value);
		std::uint32_t Value() const { return _value; }

		void InitPin(
			EmbeddedIOServices::digitalpin_t pin,
			EmbeddedIOServices::PinDirection direction) override;
		bool ReadPin(EmbeddedIOServices::digitalpin_t pin) override;
		void WritePin(
			EmbeddedIOServices::digitalpin_t pin,
			bool value) override;
		void AttachInterrupt(
			EmbeddedIOServices::digitalpin_t pin,
			EmbeddedIOServices::callback_t callBack) override;
		void DetachInterrupt(
			EmbeddedIOServices::digitalpin_t pin) override;
	};
}

#endif
