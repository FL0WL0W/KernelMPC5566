#ifndef UDS_SERVICE_H
#define UDS_SERVICE_H

#include "ICommunicationService.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace E78
{
	struct UDSMemoryRegion
	{
		std::uint32_t Address;
		std::uint32_t Length;
		bool RequiresFlashWriter;
	};

	using UDSFlashWriteFunction = std::function<bool(
		std::uint32_t address,
		const std::uint8_t* data,
		std::size_t length)>;
	using UDSExitToBootloaderFunction = std::function<void()>;

	class UDSService final
	{
	private:
		static constexpr std::size_t MaximumMessageLength = 0x0FFFU;
		static constexpr std::size_t MaximumLZ4BlockLength = 4096U;
		static constexpr std::uint8_t LZ4DataFormatIdentifier = 0x10U;

		struct TransferState
		{
			std::uint32_t Address = 0U;
			std::uint32_t Size = 0U;
			std::uint32_t BytesTransferred = 0U;
			std::uint8_t DataFormatIdentifier = 0U;
			std::uint8_t NextBlockSequenceCounter = 1U;
			std::uint8_t PreviousBlockSequenceCounter = 0U;
			bool PreviousBlockValid = false;
			bool Active = false;
		};

		EmbeddedIOServices::ICommunicationService& _communication;
		const UDSMemoryRegion* const _readRegions;
		const std::size_t _readRegionCount;
		const UDSMemoryRegion* const _writeRegions;
		const std::size_t _writeRegionCount;
		const UDSFlashWriteFunction _writeFlash;
		const UDSExitToBootloaderFunction _exitToBootloader;
		EmbeddedIOServices::communication_receive_callback_id_t _callbackId;
		TransferState _download;
		TransferState _upload;
		std::uint8_t _response[MaximumMessageLength] = {};
		std::uint16_t _previousUploadResponseLength = 0U;
		std::uint8_t _lz4Buffer[MaximumLZ4BlockLength] = {};

		const UDSMemoryRegion* FindRegion(
			const UDSMemoryRegion* regions,
			std::size_t regionCount,
			std::uint32_t address,
			std::uint32_t length) const;
		std::uint8_t ReadByte(std::uint32_t address) const;
		bool WriteMemory(
			std::uint32_t address,
			const std::uint8_t* data,
			std::size_t length);
		void SendNegative(
			const EmbeddedIOServices::communication_send_callback_t& send,
			std::uint8_t service,
			std::uint8_t code) const;
		std::size_t HandleRequest(
			EmbeddedIOServices::communication_send_callback_t send,
			const void* data,
			std::size_t length);
		std::size_t HandleReadMemory(
			const EmbeddedIOServices::communication_send_callback_t& send,
			const std::uint8_t* data,
			std::size_t length);
		std::size_t HandleRequestTransfer(
			const EmbeddedIOServices::communication_send_callback_t& send,
			const std::uint8_t* data,
			std::size_t length,
			bool upload);
		std::size_t HandleTransferData(
			const EmbeddedIOServices::communication_send_callback_t& send,
			const std::uint8_t* data,
			std::size_t length);
		std::size_t HandleUploadData(
			const EmbeddedIOServices::communication_send_callback_t& send,
			const std::uint8_t* data,
			std::size_t length);
		std::size_t HandleDownloadData(
			const EmbeddedIOServices::communication_send_callback_t& send,
			const std::uint8_t* data,
			std::size_t length);
		std::size_t HandleTransferExit(
			const EmbeddedIOServices::communication_send_callback_t& send,
			std::size_t length);

	public:
		UDSService(
			EmbeddedIOServices::ICommunicationService& communication,
			const UDSMemoryRegion* readRegions,
			std::size_t readRegionCount,
			const UDSMemoryRegion* writeRegions,
			std::size_t writeRegionCount,
			UDSFlashWriteFunction writeFlash,
			UDSExitToBootloaderFunction exitToBootloader);
		~UDSService();

		UDSService(const UDSService&) = delete;
		UDSService& operator=(const UDSService&) = delete;
	};
}

#endif
