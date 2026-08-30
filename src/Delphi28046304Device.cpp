#include "Delphi28046304Device.h"

namespace E78
{
	std::uint16_t Delphi28046304Device::EncodeProtectedDiagnosticBits(
		std::uint16_t response)
	{
		response &= 0x003FU;
		const bool allLowFiveBitsSet = (response & 0x001FU) == 0x001FU;
		const bool bit4EqualsBit5 =
			((response >> 4U) & 1U) == ((response >> 5U) & 1U);
		const std::uint16_t protectedBit = static_cast<std::uint16_t>(
			allLowFiveBitsSet ^ bit4EqualsBit5);
		return static_cast<std::uint16_t>(
			((response & 0x001FU) << 1U) | protectedBit);
	}

	void Delphi28046304Device::UpdateRotatingDiagnostic(
		const std::uint16_t* response,
		std::size_t wordCount)
	{
		_rotatingDiagnosticPending = false;
		if (response == nullptr || wordCount < 2U)
			return;
		if ((_rotatingDiagnosticCommand & 0x0F00U) == 0x0100U)
		{
			_rotatingDiagnosticValue = EncodeProtectedDiagnosticBits(response[1]);
			_rotatingDiagnosticCommand = 0x0F00U;
		}
		else
		{
			_rotatingDiagnosticCommand = 0x011FU;
		}
	}

	bool Delphi28046304Device::QueueRotatingDiagnostic()
	{
		if (_rotatingDiagnosticPending)
			return false;
		_rotatingDiagnosticPending = true;
		if (!SendCommand(
				_rotatingDiagnosticCommand,
				_rotatingDiagnosticValue,
				[this](const std::uint16_t* response, std::size_t wordCount) {
					UpdateRotatingDiagnostic(response, wordCount);
				}))
		{
			_rotatingDiagnosticPending = false;
			return false;
		}
		return true;
	}

	bool Delphi28046304Device::Transfer(
		const std::uint16_t* transmit,
		std::size_t wordCount,
		Delphi28046304ResponseCallback responseCallback)
	{
		constexpr std::size_t MaximumWords = 19U;
		if (transmit == nullptr || wordCount == 0U || wordCount > MaximumWords)
			return false;

		std::uint8_t bytes[MaximumWords * 2U] = {};
		for (std::size_t i = 0U; i < wordCount; ++i)
		{
			bytes[i * 2U] = static_cast<std::uint8_t>(transmit[i] >> 8U);
			bytes[i * 2U + 1U] = static_cast<std::uint8_t>(transmit[i]);
		}

		return _service.Transfer(
			bytes,
			wordCount * 2U,
			[wordCount, responseCallback](
				std::uint8_t* received,
				std::size_t receivedLength) mutable {
				constexpr std::size_t MaximumResponseWords = 19U;
				std::uint16_t response[MaximumResponseWords] = {};
				const std::size_t availableWords = receivedLength / 2U;
				const std::size_t responseWords =
					availableWords < wordCount ? availableWords : wordCount;
				for (std::size_t i = 0U; i < responseWords; ++i)
					response[i] = static_cast<std::uint16_t>(
						(static_cast<std::uint16_t>(received[i * 2U]) << 8U) |
						received[i * 2U + 1U]);
				if (responseCallback)
					responseCallback(response, responseWords);
			});
	}

	bool Delphi28046304Device::RequestIdentification(
		Delphi28046304ResponseCallback responseCallback)
	{
		const std::uint16_t request[] = {0x0E1BU, 0x0000U};
		return Transfer(request, 2U, responseCallback);
	}

	bool Delphi28046304Device::SendRevision4Configuration(
		Delphi28046304ResponseCallback responseCallback)
	{
		const std::uint16_t configuration[] = {
			0xCF4CU, 0x25B7U, 0x16ECU, 0x0011U, 0x1E10U,
			0x1E11U, 0x1331U, 0x7575U, 0x0055U, 0x3E00U,
			0x0013U, 0xFB82U, 0x0080U, 0x0080U, 0x0A80U,
			0x0000U, 0x0000U, 0x0000U, 0x00F0U,
		};
		return Transfer(configuration, 19U, responseCallback);
	}

	bool Delphi28046304Device::RequestDiagnostic(
		Delphi28046304ResponseCallback responseCallback)
	{
		const std::uint16_t request[] = {
			0x835FU, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
			0x0000U, 0x0000U, 0x0000U, 0x0000U,
		};
		return Transfer(request, 9U, responseCallback);
	}

	bool Delphi28046304Device::RequestStatus(
		Delphi28046304ResponseCallback responseCallback)
	{
		const std::uint16_t request[] = {
			0x0B01U, 0x0000U, 0x0000U, 0x1FC0U,
		};
		return Transfer(request, 4U, responseCallback);
	}

	bool Delphi28046304Device::ConfigureChannelGroups(
		Delphi28046304ResponseCallback responseCallback)
	{
		const std::uint16_t request[] = {0x0F52U, 0x7575U, 0x0055U};
		return Transfer(request, 3U, responseCallback);
	}

	bool Delphi28046304Device::SendCommand(
		std::uint16_t command,
		std::uint16_t value,
		Delphi28046304ResponseCallback responseCallback)
	{
		const std::uint16_t request[] = {command, value};
		return Transfer(request, 2U, responseCallback);
	}

	void Delphi28046304Device::ServiceWatchdog()
	{
		if (_watchdogStartupStage == 0U)
		{
			SendCommand(0x0F1AU, 0x0082U, nullptr);
			for (std::size_t repeat = 0U; repeat < 4U; ++repeat)
				SendCommand(0x0F1DU, 0x04F0U, nullptr);
			RequestDiagnostic(nullptr);
			if (QueueRotatingDiagnostic())
				RequestStatus(nullptr);
			_watchdogStartupStage = 1U;
			return;
		}

		if (_watchdogStartupStage == 1U)
		{
			ConfigureChannelGroups(nullptr);
			_watchdogStartupStage = 2U;
			return;
		}

		RequestDiagnostic(nullptr);
		++_diagnosticPass;
		if ((_diagnosticPass & 3U) == 0U && QueueRotatingDiagnostic())
			RequestStatus(nullptr);

		++_heartbeatPass;
		if (_heartbeatPass == 30U)
		{
			SendCommand(0x0F14U, 0x3A20U, nullptr);
		}
		else if (_heartbeatPass >= 31U)
		{
			SendCommand(0x0F14U, 0x3E20U, nullptr);
			_heartbeatPass = 0U;
		}
	}
}
