#ifndef ISPISERVICE_H
#define ISPISERVICE_H

#include <cstddef>
#include <cstdint>
#include <functional>

namespace EmbeddedIOServices
{
	using spi_transfer_callback_t = std::function<void()>;

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
		 * @brief Start exchanging byte buffers with the attached SPI device.
		 * @param txData Bytes to transmit in wire order. This storage must remain
		 * valid and unchanged until completionCallback is called.
		 * @param rxData Optional storage for received bytes. When non-null, it must
		 * remain valid until completionCallback is called. Pass nullptr to discard
		 * received data.
		 * @param length Number of bytes to exchange.
		 * @param completionCallback Called once after the exchange is complete and
		 * rxData, when supplied, contains the received bytes.
		 * @return true if the transaction was accepted; false if invalid or the
		 * implementation has no queue capacity available.
		 */
		virtual bool Transfer(
			const std::uint8_t* txData,
			std::uint8_t* rxData,
			std::size_t length,
			spi_transfer_callback_t completionCallback) = 0;
	};
}

#endif
