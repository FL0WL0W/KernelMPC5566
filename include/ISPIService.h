#ifndef ISPISERVICE_H
#define ISPISERVICE_H

#include <cstddef>
#include <cstdint>
#include <functional>

namespace EmbeddedIOServices
{
	using spi_transfer_callback_t = std::function<void(
		std::uint8_t* data,
		std::size_t length)>;

	/**
	 * @brief An asynchronous, full-duplex connection to one SPI device.
	 *
	 * Peripheral selection, chip-select behavior, clock configuration, and
	 * hardware frame size belong to the implementation. Protocol code only
	 * supplies bytes in wire order.
	 */
	class ISPIService
	{
	public:
		virtual ~ISPIService() = default;

		/**
		 * @brief Report whether the service can accept another transaction.
		 * @return true when Transfer can enqueue another transaction.
		 */
		virtual bool Ready() = 0;

		/**
		 * @brief Start exchanging a byte buffer with the attached SPI device.
		 * @param data Bytes to transmit in wire order. The implementation copies
		 * these bytes before returning, so the caller may immediately reuse or
		 * release this storage.
		 * @param length Number of bytes to exchange.
		 * @param completionCallback Called once with the complete received bytes.
		 * The response storage is owned by the service and is valid only while the
		 * callback is running. Copy any response that must be retained.
		 * @return true if the transaction was accepted; false if invalid or the
		 * implementation has no queue capacity available.
		 */
		virtual bool Transfer(
			std::uint8_t* data,
			std::size_t length,
			spi_transfer_callback_t completionCallback) = 0;
	};
}

#endif
