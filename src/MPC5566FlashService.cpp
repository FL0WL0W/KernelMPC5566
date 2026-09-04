#include "MPC5566FlashService.h"

#include "MPC5xxx.h"

#include <new>

namespace
{
	enum class FlashBlockArea : std::uint8_t
	{
		Low,
		Mid,
		High,
	};

	struct FlashBlock
	{
		std::uint32_t Address;
		std::uint32_t Length;
		FlashBlockArea Area;
		std::uint8_t AreaIndex;
	};

	constexpr FlashBlock FlashBlocks[] = {
		{0x00000000U, 0x00004000U, FlashBlockArea::Low, 0U},
		{0x00004000U, 0x0000C000U, FlashBlockArea::Low, 1U},
		{0x00010000U, 0x0000C000U, FlashBlockArea::Low, 2U},
		{0x0001C000U, 0x00004000U, FlashBlockArea::Low, 3U},
		{0x00020000U, 0x00010000U, FlashBlockArea::Low, 4U},
		{0x00030000U, 0x00010000U, FlashBlockArea::Low, 5U},
		{0x00040000U, 0x00020000U, FlashBlockArea::Mid, 0U},
		{0x00060000U, 0x00020000U, FlashBlockArea::Mid, 1U},
		{0x00080000U, 0x00020000U, FlashBlockArea::High, 0U},
		{0x000A0000U, 0x00020000U, FlashBlockArea::High, 1U},
		{0x000C0000U, 0x00020000U, FlashBlockArea::High, 2U},
		{0x000E0000U, 0x00020000U, FlashBlockArea::High, 3U},
		{0x00100000U, 0x00020000U, FlashBlockArea::High, 4U},
		{0x00120000U, 0x00020000U, FlashBlockArea::High, 5U},
		{0x00140000U, 0x00020000U, FlashBlockArea::High, 6U},
		{0x00160000U, 0x00020000U, FlashBlockArea::High, 7U},
		{0x00180000U, 0x00020000U, FlashBlockArea::High, 8U},
		{0x001A0000U, 0x00020000U, FlashBlockArea::High, 9U},
		{0x001C0000U, 0x00020000U, FlashBlockArea::High, 10U},
		{0x001E0000U, 0x00020000U, FlashBlockArea::High, 11U},
		{0x00200000U, 0x00020000U, FlashBlockArea::High, 12U},
		{0x00220000U, 0x00020000U, FlashBlockArea::High, 13U},
		{0x00240000U, 0x00020000U, FlashBlockArea::High, 14U},
		{0x00260000U, 0x00020000U, FlashBlockArea::High, 15U},
		{0x00280000U, 0x00020000U, FlashBlockArea::High, 16U},
		{0x002A0000U, 0x00020000U, FlashBlockArea::High, 17U},
		{0x002C0000U, 0x00020000U, FlashBlockArea::High, 18U},
		{0x002E0000U, 0x00020000U, FlashBlockArea::High, 19U},
	};

	static_assert(sizeof(FlashBlocks) / sizeof(FlashBlocks[0]) == 28U,
		"The MPC5566 has 28 main-array flash blocks");

	// These two factory/reserved ranges may have no valid ECC syndrome. Any
	// CPU read can raise a machine-check exception, so represent them as erased
	// bytes in the client-visible block hash without touching the flash bus.
	constexpr std::uint32_t InvalidECCRegion1 = 0x00003FE0U;
	constexpr std::uint32_t InvalidECCRegion2 = 0x0001FFE0U;
	constexpr std::uint32_t InvalidECCRegionLength = 0x20U;

	bool HasInvalidECC(std::uint32_t address)
	{
		return (address >= InvalidECCRegion1 &&
			address < InvalidECCRegion1 + InvalidECCRegionLength) ||
			(address >= InvalidECCRegion2 &&
			 address < InvalidECCRegion2 + InvalidECCRegionLength);
	}

	void WriteBigEndian32(std::uint8_t* destination, std::uint32_t value)
	{
		destination[0] = static_cast<std::uint8_t>(value >> 24U);
		destination[1] = static_cast<std::uint8_t>(value >> 16U);
		destination[2] = static_cast<std::uint8_t>(value >> 8U);
		destination[3] = static_cast<std::uint8_t>(value);
	}

	void Synchronize()
	{
		asm volatile("msync" ::: "memory");
	}

	const FlashBlock* FindFlashBlock(std::uint32_t address, std::size_t length)
	{
		if (length == 0U || length > 0xFFFFFFFFU)
			return nullptr;
		const std::uint32_t last =
			address + static_cast<std::uint32_t>(length) - 1U;
		if (last < address)
			return nullptr;
		for (const FlashBlock& block : FlashBlocks)
		{
			if (address >= block.Address &&
				last < block.Address + block.Length)
				return &block;
		}
		return nullptr;
	}

	std::size_t FindFlashBlockIndex(std::uint32_t address, std::size_t length)
	{
		const FlashBlock* const block = FindFlashBlock(address, length);
		return block == nullptr
			? sizeof(FlashBlocks) / sizeof(FlashBlocks[0])
			: static_cast<std::size_t>(block - FlashBlocks);
	}

	std::uint32_t ReadBigEndian32(const std::uint8_t* source)
	{
		return (static_cast<std::uint32_t>(source[0]) << 24U) |
			(static_cast<std::uint32_t>(source[1]) << 16U) |
			(static_cast<std::uint32_t>(source[2]) << 8U) |
			source[3];
	}
}

namespace E78
{
	void MPC5566FlashService::SendNegative(
		const EmbeddedIOServices::communication_send_callback_t& send,
		std::uint8_t code) const
	{
		const std::uint8_t response[] = {0x7FU, 0x31U, code};
		send(response, sizeof(response));
	}

	void MPC5566FlashService::SendPending()
	{
		SendNegative(_send, 0x78U);
	}

	void MPC5566FlashService::SendEraseStatus(
		const EmbeddedIOServices::communication_send_callback_t& send,
		std::uint8_t block,
		EraseStatus status) const
	{
		const std::uint8_t response[] = {
			0x71U,
			0x01U,
			static_cast<std::uint8_t>(EraseRoutineIdentifier >> 8U),
			static_cast<std::uint8_t>(EraseRoutineIdentifier),
			block,
			static_cast<std::uint8_t>(status),
		};
		send(response, sizeof(response));
	}

	void MPC5566FlashService::QueueErase(
		std::uint8_t block,
		const EmbeddedIOServices::communication_send_callback_t& send)
	{
		for (std::size_t offset = 0U; offset < _eraseQueueCount; ++offset)
		{
			const std::size_t index = (_eraseQueueHead + offset) % BlockCount;
			if (_eraseQueue[index].Block != block)
				continue;
			const bool active = offset == 0U &&
				(_operation == Operation::EraseStart ||
				 _operation == Operation::EraseWait);
			SendEraseStatus(
				send,
				block,
				active ? EraseStatus::Running : EraseStatus::Queued);
			return;
		}

		if (_eraseQueueCount == BlockCount)
		{
			SendEraseStatus(send, block, EraseStatus::Failed);
			return;
		}
		const std::size_t tail =
			(_eraseQueueHead + _eraseQueueCount) % BlockCount;
		_eraseQueue[tail].Block = block;
		_eraseQueue[tail].Sequence = _nextOperationSequence++;
		_eraseQueue[tail].Send = send;
		++_eraseQueueCount;
		SendEraseStatus(send, block, EraseStatus::Queued);
	}

	void MPC5566FlashService::BeginNextErase(std::uint32_t now)
	{
		_blockIndex = _eraseQueue[_eraseQueueHead].Block;
		CancelHash(_blockIndex);
		_lastPendingTime = now;
		_operation = Operation::EraseStart;
		SendEraseStatus(
			_eraseQueue[_eraseQueueHead].Send,
			_blockIndex,
			EraseStatus::Running);
	}

	void MPC5566FlashService::CompleteQueuedErase(EraseStatus status)
	{
		EraseRequest& request = _eraseQueue[_eraseQueueHead];
		SendEraseStatus(request.Send, request.Block, status);
		request.Send = EmbeddedIOServices::communication_send_callback_t();
		_eraseQueueHead = (_eraseQueueHead + 1U) % BlockCount;
		--_eraseQueueCount;
		_operation = Operation::Idle;
	}

	void MPC5566FlashService::StartInventory()
	{
		_response[0] = 0x71U;
		_response[1] = 0x01U;
		_response[2] = static_cast<std::uint8_t>(InventoryRoutineIdentifier >> 8U);
		_response[3] = static_cast<std::uint8_t>(InventoryRoutineIdentifier);
		_response[4] = static_cast<std::uint8_t>(BlockCount);
		_inventoryNextBlock = 0U;
		_lastPendingTime = 0U;
		_inventoryActive = true;
		for (std::size_t block = 0U; block < BlockCount; ++block)
		{
			if (_cacheStatus[block] == CacheStatus::AwaitingRewrite)
				_cacheStatus[block] = CacheStatus::NeedsHash;
		}
		SendPending();
	}

	void MPC5566FlashService::ServiceInventory()
	{
		if (_cacheStatus[_inventoryNextBlock] != CacheStatus::Valid)
		{
			if (!_hashActive)
				StartHash(_inventoryNextBlock);
			ServiceHash();
			return;
		}

		const FlashBlock& block = FlashBlocks[_inventoryNextBlock];
		const std::size_t recordOffset = 5U;
		_response[recordOffset] = _inventoryNextBlock;
		WriteBigEndian32(_response + recordOffset + 1U, block.Address);
		WriteBigEndian32(_response + recordOffset + 5U, block.Length);
		for (std::size_t i = 0U; i < 32U; ++i)
			_response[recordOffset + 9U + i] =
				_hashCache[_inventoryNextBlock][i];

		_send(_response, sizeof(_response));
		++_inventoryNextBlock;
		_lastPendingTime = 0U;
		if (_inventoryNextBlock == BlockCount)
		{
			_send = EmbeddedIOServices::communication_send_callback_t();
			_inventoryActive = false;
		}
	}

	void MPC5566FlashService::StartHash(std::uint8_t block)
	{
		_hashBlockIndex = block;
		_hashOffset = 0U;
		_hashActive = true;
		_sha256.Reset();
	}

	void MPC5566FlashService::ServiceHash()
	{
		const FlashBlock& block = FlashBlocks[_hashBlockIndex];
		const std::uint32_t remaining = block.Length - _hashOffset;
		const std::uint32_t count = remaining < HashBytesPerService
			? remaining : static_cast<std::uint32_t>(HashBytesPerService);
		std::uint32_t cursor = block.Address + _hashOffset;
		const std::uint32_t end = cursor + count;
		while (cursor < end)
		{
			if (HasInvalidECC(cursor))
			{
				const std::uint8_t erased = 0xFFU;
				_sha256.Update(&erased, 1U);
				++cursor;
				continue;
			}

			std::uint32_t directEnd = end;
			if (cursor < InvalidECCRegion1 && InvalidECCRegion1 < directEnd)
				directEnd = InvalidECCRegion1;
			if (cursor < InvalidECCRegion2 && InvalidECCRegion2 < directEnd)
				directEnd = InvalidECCRegion2;
			_sha256.Update(
				reinterpret_cast<const volatile std::uint8_t*>(cursor),
				directEnd - cursor);
			cursor = directEnd;
		}
		_hashOffset += count;
		if (_hashOffset != block.Length)
			return;

		_sha256.Final(_hashCache[_hashBlockIndex]);
		_cacheStatus[_hashBlockIndex] = CacheStatus::Valid;
		_hashActive = false;
	}

	void MPC5566FlashService::ServiceBackgroundHash()
	{
		if (!_hashActive)
		{
			for (std::size_t block = 0U; block < BlockCount; ++block)
			{
				if (_cacheStatus[block] != CacheStatus::NeedsHash)
					continue;
				StartHash(static_cast<std::uint8_t>(block));
				break;
			}
		}
		if (_hashActive)
			ServiceHash();
	}

	void MPC5566FlashService::CancelHash(std::uint8_t block)
	{
		if (_hashActive && _hashBlockIndex == block)
			_hashActive = false;
	}

	void MPC5566FlashService::StartErase()
	{
		const FlashBlock& block = FlashBlocks[_blockIndex];
		if (FLASH.MCR.B.PGM != 0U || FLASH.MCR.B.ERS != 0U ||
			FLASH.MCR.B.EHV != 0U || FLASH.MCR.B.DONE == 0U)
		{
			CompleteQueuedErase(EraseStatus::Failed);
			return;
		}

		_savedBIUCR = FLASH.BIUCR.R;
		_savedLMLR = FLASH.LMLR.R;
		_savedSLMLR = FLASH.SLMLR.R;
		_savedHLR = FLASH.HLR.R;
		FLASH.BIUCR.B.IPFEN = 0U;
		FLASH.BIUCR.B.DPFEN = 0U;
		Synchronize();

		if (block.Area == FlashBlockArea::High)
		{
			FLASH.HLR.R = 0xB2B22222U;
			FLASH.HLR.B.HBLOCK &= ~(1UL << block.AreaIndex);
		}
		else
		{
			FLASH.LMLR.R = 0xA1A11111U;
			FLASH.SLMLR.R = 0xC3C33333U;
			if (block.Area == FlashBlockArea::Low)
			{
				FLASH.LMLR.B.LLOCK &= ~(1UL << block.AreaIndex);
				FLASH.SLMLR.B.SLLOCK &= ~(1UL << block.AreaIndex);
			}
			else
			{
				FLASH.LMLR.B.MLOCK &= ~(1UL << block.AreaIndex);
				FLASH.SLMLR.B.SMLOCK &= ~(1UL << block.AreaIndex);
			}
		}

		FLASH.MCR.B.ERS = 1U;
		if (block.Area == FlashBlockArea::Low)
			FLASH.LMSR.B.LSEL = 1UL << block.AreaIndex;
		else if (block.Area == FlashBlockArea::Mid)
			FLASH.LMSR.B.MSEL = 1UL << block.AreaIndex;
		else
			FLASH.HSR.B.HBSEL = 1UL << block.AreaIndex;
		*reinterpret_cast<volatile std::uint32_t*>(block.Address) = 0xFFFFFFFFU;
		Synchronize();
		FLASH.MCR.B.EHV = 1U;
		Synchronize();
		_operation = Operation::EraseWait;
	}

	void MPC5566FlashService::ServiceErase()
	{
		if (FLASH.MCR.B.DONE == 0U)
			return;
		FinishErase(FLASH.MCR.B.PEG != 0U);
	}

	void MPC5566FlashService::FinishErase(bool successful)
	{
		FLASH.MCR.B.EHV = 0U;
		Synchronize();
		FLASH.LMSR.R = 0U;
		FLASH.HSR.R = 0U;
		FLASH.MCR.B.ERS = 0U;
		Synchronize();
		RestoreFlashController();

		CancelHash(_blockIndex);
		if (successful)
		{
			_cacheStatus[_blockIndex] = CacheStatus::AwaitingRewrite;
			_rewriteThrough[_blockIndex] = FlashBlocks[_blockIndex].Address;
		}
		else
		{
			_cacheStatus[_blockIndex] = CacheStatus::AwaitingRewrite;
			_rewriteThrough[_blockIndex] = FlashBlocks[_blockIndex].Address;
		}

		CompleteQueuedErase(
			successful ? EraseStatus::Successful : EraseStatus::Failed);
	}

	void MPC5566FlashService::RestoreFlashController()
	{
		FLASH.LMLR.B.LLOCK = _savedLMLR & 0xFFFFU;
		FLASH.LMLR.B.MLOCK = (_savedLMLR >> 16U) & 0xFU;
		FLASH.SLMLR.B.SLLOCK = _savedSLMLR & 0xFFFFU;
		FLASH.SLMLR.B.SMLOCK = (_savedSLMLR >> 16U) & 0xFU;
		FLASH.HLR.B.HBLOCK = _savedHLR & 0x0FFFFFFFU;

		// Toggling BFEN invalidates the flash line buffers. Prefetch stays
		// disabled until after invalidation, as required by the flash BIU.
		FLASH.BIUCR.B.BFEN = 0U;
		Synchronize();
		FLASH.BIUCR.B.BFEN = (_savedBIUCR & 1U) != 0U;
		FLASH.BIUCR.R = _savedBIUCR;
		Synchronize();
	}

	bool MPC5566FlashService::QueueWrite(
		std::uint32_t address,
		const std::uint8_t* data,
		std::size_t length,
		UDSFlashWriteCompletion completion)
	{
		const std::size_t blockIndex = FindFlashBlockIndex(address, length);
		if (data == nullptr || !completion || length == 0U || length > 4096U ||
			(address & 7U) != 0U || (length & 7U) != 0U ||
			blockIndex == BlockCount)
			return false;

		std::uint8_t* const copy = new (std::nothrow) std::uint8_t[length];
		if (copy == nullptr)
			return false;
		for (std::size_t i = 0U; i < length; ++i)
			copy[i] = data[i];

		ProgramRequest request;
		request.Address = address;
		request.Length = length;
		request.Data = copy;
		request.Sequence = _nextOperationSequence++;
		request.Completion = completion;
		if (_activeProgram.Data == nullptr)
		{
			_activeProgram = request;
			CancelHash(static_cast<std::uint8_t>(blockIndex));
			return true;
		}
		if (_queuedProgram.Data == nullptr)
		{
			_queuedProgram = request;
			CancelHash(static_cast<std::uint8_t>(blockIndex));
			return true;
		}
		delete[] copy;
		return false;
	}

	void MPC5566FlashService::BeginProgram()
	{
		const FlashBlock* const block = FindFlashBlock(
			_activeProgram.Address,
			_activeProgram.Length);
		if (block == nullptr || FLASH.MCR.B.PGM != 0U ||
			FLASH.MCR.B.ERS != 0U || FLASH.MCR.B.EHV != 0U ||
			FLASH.MCR.B.DONE == 0U)
		{
			FinishProgram(false);
			return;
		}

		_savedBIUCR = FLASH.BIUCR.R;
		_savedLMLR = FLASH.LMLR.R;
		_savedSLMLR = FLASH.SLMLR.R;
		_savedHLR = FLASH.HLR.R;
		FLASH.BIUCR.B.IPFEN = 0U;
		FLASH.BIUCR.B.DPFEN = 0U;
		Synchronize();

		if (block->Area == FlashBlockArea::High)
		{
			FLASH.HLR.R = 0xB2B22222U;
			FLASH.HLR.B.HBLOCK &= ~(1UL << block->AreaIndex);
		}
		else
		{
			FLASH.LMLR.R = 0xA1A11111U;
			FLASH.SLMLR.R = 0xC3C33333U;
			if (block->Area == FlashBlockArea::Low)
			{
				FLASH.LMLR.B.LLOCK &= ~(1UL << block->AreaIndex);
				FLASH.SLMLR.B.SLLOCK &= ~(1UL << block->AreaIndex);
			}
			else
			{
				FLASH.LMLR.B.MLOCK &= ~(1UL << block->AreaIndex);
				FLASH.SLMLR.B.SMLOCK &= ~(1UL << block->AreaIndex);
			}
		}

		_programOffset = 0U;
		_programControllerActive = true;
		_operation = Operation::ProgramStart;
	}

	void MPC5566FlashService::StartProgramPage()
	{
		const std::uint32_t address =
			_activeProgram.Address + static_cast<std::uint32_t>(_programOffset);
		const std::uint32_t pageEndAddress = (address & ~0x1FU) + 0x20U;
		const std::size_t remaining = _activeProgram.Length - _programOffset;
		const std::size_t pageLength = remaining < pageEndAddress - address
			? remaining : pageEndAddress - address;
		std::uint8_t programMask = 0U;

		for (std::size_t offset = 0U; offset < pageLength; offset += 8U)
		{
			const volatile std::uint8_t* const current =
				reinterpret_cast<const volatile std::uint8_t*>(address + offset);
			const std::uint8_t* const desired =
				_activeProgram.Data + _programOffset + offset;
			bool equal = true;
			bool erased = true;
			for (std::size_t byte = 0U; byte < 8U; ++byte)
			{
				equal = equal && current[byte] == desired[byte];
				erased = erased && current[byte] == 0xFFU;
			}
			if (equal)
				continue;
			if (!erased)
			{
				FinishProgram(false);
				return;
			}
			programMask |= 1U << (offset / 8U);
		}

		_programNextOffset = _programOffset + pageLength;
		if (programMask == 0U)
		{
			_programOffset = _programNextOffset;
			if (_programOffset == _activeProgram.Length)
				FinishProgram(true);
			return;
		}
		FLASH.MCR.B.PGM = 1U;
		Synchronize();
		for (std::size_t offset = 0U; offset < pageLength; offset += 8U)
		{
			if ((programMask & (1U << (offset / 8U))) == 0U)
				continue;
			const std::uint8_t* const desired =
				_activeProgram.Data + _programOffset + offset;
			volatile std::uint32_t* const destination =
				reinterpret_cast<volatile std::uint32_t*>(address + offset);
			destination[0] = ReadBigEndian32(desired);
			destination[1] = ReadBigEndian32(desired + 4U);
		}
		Synchronize();
		FLASH.MCR.B.EHV = 1U;
		Synchronize();
		_operation = Operation::ProgramWait;
	}

	void MPC5566FlashService::ServiceProgram()
	{
		if (FLASH.MCR.B.DONE == 0U)
			return;
		const bool successful = FLASH.MCR.B.PEG != 0U;
		// End this page's high-voltage operation, but keep PGM asserted. The
		// controller specification returns directly to the next interlock write
		// for additional pages and clears PGM only after the complete sequence.
		FLASH.MCR.B.EHV = 0U;
		Synchronize();
		if (!successful)
		{
			FinishProgram(false);
			return;
		}
		_programOffset = _programNextOffset;
		if (_programOffset == _activeProgram.Length)
		{
			FinishProgram(true);
			return;
		}
		_operation = Operation::ProgramStart;
	}

	void MPC5566FlashService::FinishProgram(bool successful)
	{
		if (_programControllerActive)
		{
			FLASH.MCR.B.EHV = 0U;
			FLASH.MCR.B.PGM = 0U;
			Synchronize();
			RestoreFlashController();
			_programControllerActive = false;
		}
		MarkProgramResult(successful);
		_activeProgram.Completion(successful);
		delete[] _activeProgram.Data;
		_activeProgram = _queuedProgram;
		_queuedProgram = ProgramRequest();
		_operation = Operation::Idle;
	}

	void MPC5566FlashService::MarkProgramResult(bool successful)
	{
		const std::size_t blockIndex = FindFlashBlockIndex(
			_activeProgram.Address,
			_activeProgram.Length);
		if (blockIndex == BlockCount)
			return;
		CancelHash(static_cast<std::uint8_t>(blockIndex));
		if (!successful)
		{
			_cacheStatus[blockIndex] = CacheStatus::AwaitingRewrite;
			_rewriteThrough[blockIndex] = FlashBlocks[blockIndex].Address;
			return;
		}

		if (_cacheStatus[blockIndex] != CacheStatus::AwaitingRewrite)
		{
			_cacheStatus[blockIndex] = CacheStatus::AwaitingRewrite;
			_rewriteThrough[blockIndex] = FlashBlocks[blockIndex].Address;
		}
		if (_cacheStatus[blockIndex] == CacheStatus::AwaitingRewrite)
		{
			const std::uint32_t writeStart = _activeProgram.Address;
			const std::uint32_t writeEnd = writeStart +
				static_cast<std::uint32_t>(_activeProgram.Length);
			if (writeStart <= _rewriteThrough[blockIndex] &&
				writeEnd > _rewriteThrough[blockIndex])
				_rewriteThrough[blockIndex] = writeEnd;
			const FlashBlock& block = FlashBlocks[blockIndex];
			if (_rewriteThrough[blockIndex] >= block.Address + block.Length)
				_cacheStatus[blockIndex] = CacheStatus::NeedsHash;
		}
	}

	bool MPC5566FlashService::HandleRoutineControl(
		std::uint8_t subFunction,
		std::uint16_t routineIdentifier,
		const std::uint8_t* optionRecord,
		std::size_t optionRecordLength,
		const EmbeddedIOServices::communication_send_callback_t& send)
	{
		if (routineIdentifier != InventoryRoutineIdentifier &&
			routineIdentifier != EraseRoutineIdentifier)
			return false;

		if (subFunction != 0x01U)
		{
			SendNegative(send, 0x12U);
			return true;
		}

		if (routineIdentifier == InventoryRoutineIdentifier)
		{
			if (optionRecordLength != 0U)
			{
				SendNegative(send, 0x13U);
				return true;
			}
			if (_inventoryActive || _operation != Operation::Idle ||
				_eraseQueueCount != 0U || _activeProgram.Data != nullptr)
			{
				SendNegative(send, 0x21U);
				return true;
			}
			_send = send;
			StartInventory();
			return true;
		}

		if (optionRecord == nullptr || optionRecordLength != 1U ||
			optionRecord[0] >= BlockCount)
		{
			SendNegative(send, optionRecordLength == 1U ? 0x31U : 0x13U);
			return true;
		}
		QueueErase(optionRecord[0], send);
		return true;
	}

	void MPC5566FlashService::Service(std::uint32_t now)
	{
		if (_operation == Operation::Idle && !_inventoryActive)
		{
			const bool eraseIsNext = _eraseQueueCount != 0U &&
				(_activeProgram.Data == nullptr ||
				 static_cast<std::int32_t>(
					 _eraseQueue[_eraseQueueHead].Sequence -
					 _activeProgram.Sequence) < 0);
			if (eraseIsNext)
				BeginNextErase(now);
			else if (_activeProgram.Data != nullptr)
				BeginProgram();
		}

		if (_operation != Operation::Idle)
		{
			if (_lastPendingTime == 0U)
			{
				_lastPendingTime = now;
			}
			else if (static_cast<std::uint32_t>(now - _lastPendingTime) >=
				StatusPeriodTicks)
			{
				_lastPendingTime = now;
				if (_operation == Operation::EraseStart ||
					_operation == Operation::EraseWait)
					SendEraseStatus(
						_eraseQueue[_eraseQueueHead].Send,
						_eraseQueue[_eraseQueueHead].Block,
						EraseStatus::Running);
			}

			if (_operation == Operation::EraseStart)
				StartErase();
			else if (_operation == Operation::EraseWait)
				ServiceErase();
			else if (_operation == Operation::ProgramStart)
				StartProgramPage();
			else if (_operation == Operation::ProgramWait)
				ServiceProgram();
			return;
		}

		if (_inventoryActive)
		{
			if (_lastPendingTime == 0U)
			{
				_lastPendingTime = now;
			}
			else if (static_cast<std::uint32_t>(now - _lastPendingTime) >=
				StatusPeriodTicks)
			{
				_lastPendingTime = now;
				SendPending();
				return;
			}
			ServiceInventory();
			return;
		}

		ServiceBackgroundHash();
	}
}
