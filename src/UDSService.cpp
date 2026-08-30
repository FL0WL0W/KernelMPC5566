#include "UDSService.h"

#include "LZ4.h"

namespace E78
{
	UDSService::UDSService(
		EmbeddedIOServices::ICommunicationService& communication,
		const UDSMemoryRegion* readRegions,
		std::size_t readRegionCount,
		const UDSMemoryRegion* writeRegions,
		std::size_t writeRegionCount,
		UDSFlashWriteFunction writeFlash,
		UDSExitToBootloaderFunction exitToBootloader)
		: _communication(communication),
		  _readRegions(readRegions),
		  _readRegionCount(readRegionCount),
		  _writeRegions(writeRegions),
		  _writeRegionCount(writeRegionCount),
		  _writeFlash(writeFlash),
		  _exitToBootloader(exitToBootloader),
		  _callbackId(_communication.RegisterReceiveCallBack(
			  [this](EmbeddedIOServices::communication_send_callback_t send,
				  const void* data,
				  std::size_t length) {
				  return HandleRequest(send, data, length);
			  }))
	{
	}

	UDSService::~UDSService()
	{
		_communication.UnRegisterReceiveCallBack(_callbackId);
	}

	const UDSMemoryRegion* UDSService::FindRegion(
		const UDSMemoryRegion* regions,
		std::size_t regionCount,
		std::uint32_t address,
		std::uint32_t length) const
	{
		if (regions == nullptr || length == 0U)
			return nullptr;
		const std::uint32_t end = address + length - 1U;
		if (end < address)
			return nullptr;
		for (std::size_t i = 0U; i < regionCount; ++i)
		{
			const UDSMemoryRegion& region = regions[i];
			if (region.Length == 0U)
				continue;
			const std::uint32_t regionEnd =
				region.Address + region.Length - 1U;
			if (regionEnd >= region.Address &&
				address >= region.Address && end <= regionEnd)
				return &region;
		}
		return nullptr;
	}

	std::uint8_t UDSService::ReadByte(std::uint32_t address) const
	{
		return *reinterpret_cast<const volatile std::uint8_t*>(address);
	}

	bool UDSService::WriteMemory(
		std::uint32_t address,
		const std::uint8_t* data,
		std::size_t length)
	{
		const UDSMemoryRegion* const region = FindRegion(
			_writeRegions,
			_writeRegionCount,
			address,
			static_cast<std::uint32_t>(length));
		if (region == nullptr)
			return false;
		if (region->RequiresFlashWriter)
			return _writeFlash && _writeFlash(address, data, length);

		volatile std::uint8_t* const destination =
			reinterpret_cast<volatile std::uint8_t*>(address);
		for (std::size_t i = 0U; i < length; ++i)
			destination[i] = data[i];
		return true;
	}

	void UDSService::SendNegative(
		const EmbeddedIOServices::communication_send_callback_t& send,
		std::uint8_t service,
		std::uint8_t code) const
	{
		const std::uint8_t response[] = {0x7FU, service, code};
		send(response, sizeof(response));
	}

	std::size_t UDSService::HandleReadMemory(
		const EmbeddedIOServices::communication_send_callback_t& send,
		const std::uint8_t* data,
		std::size_t length)
	{
		if (length < 3U)
		{
			SendNegative(send, 0x23U, 0x13U);
			return length;
		}
		const std::uint8_t addressLength = data[0] & 0x0FU;
		const std::uint8_t sizeLength = data[0] >> 4U;
		if (addressLength == 0U || addressLength > 4U ||
			sizeLength == 0U || sizeLength > 4U ||
			length != 1U + addressLength + sizeLength)
		{
			SendNegative(send, 0x23U, 0x13U);
			return length;
		}
		std::uint32_t address = 0U;
		for (std::uint8_t i = 0U; i < addressLength; ++i)
			address = (address << 8U) | data[1U + i];
		std::uint32_t readLength = 0U;
		for (std::uint8_t i = 0U; i < sizeLength; ++i)
			readLength = (readLength << 8U) |
				data[1U + addressLength + i];
		if (readLength + 1U > MaximumMessageLength ||
			FindRegion(_readRegions, _readRegionCount, address, readLength) == nullptr)
		{
			SendNegative(send, 0x23U, 0x31U);
			return length;
		}
		_response[0] = 0x63U;
		for (std::uint32_t i = 0U; i < readLength; ++i)
			_response[1U + i] = ReadByte(address + i);
		send(_response, 1U + readLength);
		return length;
	}

	std::size_t UDSService::HandleRequestTransfer(
		const EmbeddedIOServices::communication_send_callback_t& send,
		const std::uint8_t* data,
		std::size_t length,
		bool upload)
	{
		const std::uint8_t service = upload ? 0x35U : 0x34U;
		if (length < 4U)
		{
			SendNegative(send, service, 0x13U);
			return length;
		}
		const std::uint8_t format = data[0];
		const std::uint8_t addressLength = data[1] & 0x0FU;
		const std::uint8_t sizeLength = data[1] >> 4U;
		if (addressLength == 0U || addressLength > 4U ||
			sizeLength == 0U || sizeLength > 4U ||
			length != 2U + addressLength + sizeLength)
		{
			SendNegative(send, service, 0x13U);
			return length;
		}
		if (format != 0U && format != LZ4DataFormatIdentifier)
		{
			SendNegative(send, service, 0x31U);
			return length;
		}
		std::uint32_t address = 0U;
		for (std::uint8_t i = 0U; i < addressLength; ++i)
			address = (address << 8U) | data[2U + i];
		std::uint32_t size = 0U;
		for (std::uint8_t i = 0U; i < sizeLength; ++i)
			size = (size << 8U) | data[2U + addressLength + i];
		const UDSMemoryRegion* const region = upload
			? FindRegion(_readRegions, _readRegionCount, address, size)
			: FindRegion(_writeRegions, _writeRegionCount, address, size);
		if (region == nullptr)
		{
			SendNegative(send, service, 0x31U);
			return length;
		}

		TransferState& state = upload ? _upload : _download;
		state.Address = address;
		state.Size = size;
		state.BytesTransferred = 0U;
		state.DataFormatIdentifier = format;
		state.NextBlockSequenceCounter = 1U;
		state.PreviousBlockSequenceCounter = 0U;
		state.PreviousBlockValid = false;
		state.Active = true;
		(upload ? _download : _upload).Active = false;
		_previousUploadResponseLength = 0U;

		const std::uint8_t response[] = {
			static_cast<std::uint8_t>(upload ? 0x75U : 0x74U),
			0x20U,
			static_cast<std::uint8_t>(MaximumMessageLength >> 8U),
			static_cast<std::uint8_t>(MaximumMessageLength),
		};
		send(response, sizeof(response));
		return length;
	}

	std::size_t UDSService::HandleUploadData(
		const EmbeddedIOServices::communication_send_callback_t& send,
		const std::uint8_t* data,
		std::size_t length)
	{
		if (length != 1U)
		{
			SendNegative(send, 0x36U, 0x13U);
			return length;
		}
		const std::uint8_t counter = data[0];
		if (_upload.PreviousBlockValid &&
			counter == _upload.PreviousBlockSequenceCounter)
		{
			send(_response, _previousUploadResponseLength);
			return length;
		}
		if (counter != _upload.NextBlockSequenceCounter)
		{
			SendNegative(send, 0x36U, 0x73U);
			return length;
		}
		if (_upload.BytesTransferred >= _upload.Size)
		{
			SendNegative(send, 0x36U, 0x24U);
			return length;
		}

		const std::uint32_t remaining =
			_upload.Size - _upload.BytesTransferred;
		const std::uint32_t address =
			_upload.Address + _upload.BytesTransferred;
		_response[0] = 0x76U;
		_response[1] = counter;
		std::uint32_t blockLength = 0U;
		if (_upload.DataFormatIdentifier == LZ4DataFormatIdentifier)
		{
			blockLength = remaining < MaximumLZ4BlockLength
				? remaining : MaximumLZ4BlockLength;
			for (std::uint32_t i = 0U; i < blockLength; ++i)
				_lz4Buffer[i] = ReadByte(address + i);
			std::size_t compressedLength;
			Kernel::LZ4EncodeResult result;
			do
			{
				compressedLength = MaximumMessageLength - 4U;
				result = Kernel::EncodeLZ4Block(
					_lz4Buffer,
					blockLength,
					_response + 4U,
					compressedLength);
				if (result == Kernel::LZ4EncodeResult::OutputTooSmall)
					blockLength /= 2U;
			} while (result == Kernel::LZ4EncodeResult::OutputTooSmall &&
				blockLength != 0U);
			if (result != Kernel::LZ4EncodeResult::Success)
			{
				_upload.Active = false;
				SendNegative(send, 0x36U, 0x72U);
				return length;
			}
			_response[2] = static_cast<std::uint8_t>(blockLength >> 8U);
			_response[3] = static_cast<std::uint8_t>(blockLength);
			_previousUploadResponseLength = static_cast<std::uint16_t>(
				4U + compressedLength);
		}
		else
		{
			const std::uint32_t maximumDataLength = MaximumMessageLength - 2U;
			blockLength = remaining < maximumDataLength
				? remaining : maximumDataLength;
			for (std::uint32_t i = 0U; i < blockLength; ++i)
				_response[2U + i] = ReadByte(address + i);
			_previousUploadResponseLength = static_cast<std::uint16_t>(
				2U + blockLength);
		}
		_upload.BytesTransferred += blockLength;
		_upload.PreviousBlockSequenceCounter = counter;
		_upload.PreviousBlockValid = true;
		_upload.NextBlockSequenceCounter = static_cast<std::uint8_t>(counter + 1U);
		send(_response, _previousUploadResponseLength);
		return length;
	}

	std::size_t UDSService::HandleDownloadData(
		const EmbeddedIOServices::communication_send_callback_t& send,
		const std::uint8_t* data,
		std::size_t length)
	{
		if (length < 2U || length > MaximumMessageLength - 1U)
		{
			SendNegative(send, 0x36U, 0x13U);
			return length;
		}
		const std::uint8_t counter = data[0];
		if (_download.PreviousBlockValid &&
			counter == _download.PreviousBlockSequenceCounter)
		{
			const std::uint8_t response[] = {0x76U, counter};
			send(response, sizeof(response));
			return length;
		}
		if (counter != _download.NextBlockSequenceCounter)
		{
			SendNegative(send, 0x36U, 0x73U);
			return length;
		}
		if (_download.BytesTransferred >= _download.Size)
		{
			SendNegative(send, 0x36U, 0x24U);
			return length;
		}

		const bool compressed =
			_download.DataFormatIdentifier == LZ4DataFormatIdentifier;
		if (compressed && length < 4U)
		{
			SendNegative(send, 0x36U, 0x13U);
			return length;
		}
		std::uint32_t blockLength = static_cast<std::uint32_t>(length - 1U);
		const std::uint8_t* blockData = data + 1U;
		if (compressed)
		{
			blockLength = (static_cast<std::uint32_t>(data[1]) << 8U) | data[2];
			if (blockLength == 0U || blockLength > MaximumLZ4BlockLength)
			{
				SendNegative(send, 0x36U, 0x31U);
				return length;
			}
			if (Kernel::DecodeLZ4Block(
					data + 3U,
					length - 3U,
					_lz4Buffer,
					blockLength) != Kernel::LZ4DecodeResult::Success)
			{
				SendNegative(send, 0x36U, 0x72U);
				return length;
			}
			blockData = _lz4Buffer;
		}
		if (blockLength > _download.Size - _download.BytesTransferred)
		{
			_download.Active = false;
			SendNegative(send, 0x36U, 0x71U);
			return length;
		}
		const std::uint32_t address =
			_download.Address + _download.BytesTransferred;
		if (!WriteMemory(address, blockData, blockLength))
		{
			_download.Active = false;
			SendNegative(send, 0x36U, 0x72U);
			return length;
		}
		_download.BytesTransferred += blockLength;
		_download.PreviousBlockSequenceCounter = counter;
		_download.PreviousBlockValid = true;
		_download.NextBlockSequenceCounter = static_cast<std::uint8_t>(counter + 1U);
		const std::uint8_t response[] = {0x76U, counter};
		send(response, sizeof(response));
		return length;
	}

	std::size_t UDSService::HandleTransferData(
		const EmbeddedIOServices::communication_send_callback_t& send,
		const std::uint8_t* data,
		std::size_t length)
	{
		if (_upload.Active)
			return HandleUploadData(send, data, length);
		if (_download.Active)
			return HandleDownloadData(send, data, length);
		SendNegative(send, 0x36U, 0x24U);
		return length;
	}

	std::size_t UDSService::HandleTransferExit(
		const EmbeddedIOServices::communication_send_callback_t& send,
		std::size_t length)
	{
		if (length != 0U)
		{
			SendNegative(send, 0x37U, 0x13U);
			return length;
		}
		const bool complete =
			(_download.Active &&
			 _download.BytesTransferred == _download.Size) ||
			(_upload.Active && _upload.BytesTransferred == _upload.Size);
		if (!complete)
		{
			SendNegative(send, 0x37U, 0x24U);
			return length;
		}
		_download.Active = false;
		_upload.Active = false;
		const std::uint8_t response[] = {0x77U};
		send(response, sizeof(response));
		return length;
	}

	std::size_t UDSService::HandleRequest(
		EmbeddedIOServices::communication_send_callback_t send,
		const void* data,
		std::size_t length)
	{
		if (data == nullptr || length == 0U)
			return 0U;
		const std::uint8_t* const request =
			static_cast<const std::uint8_t*>(data);
		switch (request[0])
		{
		case 0x10U:
			if (length != 2U)
			{
				SendNegative(send, 0x10U, 0x13U);
			}
			else if (request[1] != 0x02U)
			{
				SendNegative(send, 0x10U, 0x12U);
			}
			else if (!_exitToBootloader)
			{
				SendNegative(send, 0x10U, 0x22U);
			}
			else
			{
				// Match the application's successful programming-session response.
				const std::uint8_t response[] = {0x50U};
				send(response, sizeof(response));
				_exitToBootloader();
			}
			break;
		case 0x23U:
			HandleReadMemory(send, request + 1U, length - 1U);
			break;
		case 0x34U:
			HandleRequestTransfer(send, request + 1U, length - 1U, false);
			break;
		case 0x35U:
			HandleRequestTransfer(send, request + 1U, length - 1U, true);
			break;
		case 0x36U:
			HandleTransferData(send, request + 1U, length - 1U);
			break;
		case 0x37U:
			HandleTransferExit(send, length - 1U);
			break;
		case 0x27U:
			if (length >= 2U && request[1] == 0x01U)
			{
				const std::uint8_t response[] = {0x67U, 0x01U, 0x00U, 0x00U};
				send(response, sizeof(response));
			}
			else
			{
				SendNegative(send, 0x27U, 0x12U);
			}
			break;
		default:
			SendNegative(send, request[0], 0x11U);
			break;
		}
		return length;
	}
}
