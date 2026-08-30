#ifndef DELPHI28046304DEVICE_H
#define DELPHI28046304DEVICE_H

#include "ISPIService.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace E78
{
	using Delphi28046304ResponseCallback = std::function<void(
		const std::uint16_t* words,
		std::size_t wordCount)>;

	/**
	 * @brief Protocol operations for the Delphi 28046304 C2MIO ASIC.
	 *
	 * Response words are valid only for the duration of the callback. The
	 * watchdog service owns protocol state but no clock; its caller determines
	 * the service cadence.
	 */
	class Delphi28046304Device final
	{
	private:
		EmbeddedIOServices::ISPIService& _service;
		std::uint16_t _rotatingDiagnosticCommand = 0x011FU;
		std::uint16_t _rotatingDiagnosticValue = 0x0000U;
		std::uint8_t _watchdogStartupStage = 0U;
		std::uint8_t _diagnosticPass = 0U;
		std::uint8_t _heartbeatPass = 0U;
		bool _rotatingDiagnosticPending = false;

		static std::uint16_t EncodeProtectedDiagnosticBits(
			std::uint16_t response);
		bool QueueRotatingDiagnostic();
		void UpdateRotatingDiagnostic(
			const std::uint16_t* response,
			std::size_t wordCount);

	public:
		explicit Delphi28046304Device(
			EmbeddedIOServices::ISPIService& service)
			: _service(service) {}

		bool Transfer(
			const std::uint16_t* transmit,
			std::size_t wordCount,
			Delphi28046304ResponseCallback responseCallback = nullptr);

		bool RequestIdentification(
			Delphi28046304ResponseCallback responseCallback = nullptr);
		bool SendRevision4Configuration(
			Delphi28046304ResponseCallback responseCallback = nullptr);
		bool RequestDiagnostic(
			Delphi28046304ResponseCallback responseCallback = nullptr);
		bool RequestStatus(
			Delphi28046304ResponseCallback responseCallback = nullptr);
		bool ConfigureChannelGroups(
			Delphi28046304ResponseCallback responseCallback = nullptr);
		bool SendCommand(
			std::uint16_t command,
			std::uint16_t value,
			Delphi28046304ResponseCallback responseCallback = nullptr);

		void ServiceWatchdog();
	};
}

#endif
