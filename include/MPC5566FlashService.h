#ifndef MPC5566_FLASH_SERVICE_H
#define MPC5566_FLASH_SERVICE_H

#include "ICommunicationService.h"
#include "SHA256.h"
#include "UDSService.h"

#include <cstddef>
#include <cstdint>

namespace E78
{
	class MPC5566FlashService final
	{
	private:
		static constexpr std::uint16_t InventoryRoutineIdentifier = 0xFF00U;
		static constexpr std::uint16_t EraseRoutineIdentifier = 0xFF01U;
		static constexpr std::size_t BlockCount = 28U;
		static constexpr std::size_t BlockRecordLength = 41U;
		static constexpr std::size_t InventoryResponseLength =
			5U + BlockRecordLength;
		static constexpr std::size_t HashBytesPerService = 1024U;
		static constexpr std::uint32_t StatusPeriodTicks = 16000000U;

		enum class EraseStatus : std::uint8_t
		{
			Queued = 0U,
			Running = 1U,
			Successful = 2U,
			Failed = 3U,
		};

		struct EraseRequest
		{
			std::uint8_t Block = 0U;
			std::uint32_t Sequence = 0U;
			EmbeddedIOServices::communication_send_callback_t Send;
		};

		struct ProgramRequest
		{
			std::uint32_t Address = 0U;
			std::size_t Length = 0U;
			std::uint8_t* Data = nullptr;
			std::uint32_t Sequence = 0U;
			UDSFlashWriteCompletion Completion;
		};

		enum class Operation : std::uint8_t
		{
			Idle,
			EraseStart,
			EraseWait,
			ProgramStart,
			ProgramWait,
		};

		enum class CacheStatus : std::uint8_t
		{
			NeedsHash,
			AwaitingRewrite,
			Valid,
		};

		Operation _operation = Operation::Idle;
		EmbeddedIOServices::communication_send_callback_t _send;
		Kernel::SHA256 _sha256;
		std::uint8_t _response[InventoryResponseLength] = {};
		std::uint8_t _hashCache[BlockCount][32U] = {};
		CacheStatus _cacheStatus[BlockCount] = {};
		std::uint32_t _rewriteThrough[BlockCount] = {};
		std::uint8_t _blockIndex = 0U;
		std::uint8_t _hashBlockIndex = 0U;
		std::uint8_t _inventoryNextBlock = 0U;
		std::uint32_t _hashOffset = 0U;
		std::uint32_t _lastPendingTime = 0U;
		bool _hashActive = false;
		bool _inventoryActive = false;
		std::uint32_t _savedBIUCR = 0U;
		std::uint32_t _savedLMLR = 0U;
		std::uint32_t _savedSLMLR = 0U;
		std::uint32_t _savedHLR = 0U;
		EraseRequest _eraseQueue[BlockCount];
		std::size_t _eraseQueueHead = 0U;
		std::size_t _eraseQueueCount = 0U;
		ProgramRequest _activeProgram;
		ProgramRequest _queuedProgram;
		std::size_t _programOffset = 0U;
		std::size_t _programNextOffset = 0U;
		bool _programControllerActive = false;
		std::uint32_t _nextOperationSequence = 0U;

		void SendNegative(
			const EmbeddedIOServices::communication_send_callback_t& send,
			std::uint8_t code) const;
		void SendPending();
		void SendEraseStatus(
			const EmbeddedIOServices::communication_send_callback_t& send,
			std::uint8_t block,
			EraseStatus status) const;
		void QueueErase(
			std::uint8_t block,
			const EmbeddedIOServices::communication_send_callback_t& send);
		void BeginNextErase(std::uint32_t now);
		void CompleteQueuedErase(EraseStatus status);
		void StartInventory();
		void ServiceInventory();
		void StartHash(std::uint8_t block);
		void ServiceHash();
		void ServiceBackgroundHash();
		void CancelHash(std::uint8_t block);
		void MarkProgramResult(bool successful);
		void StartErase();
		void ServiceErase();
		void FinishErase(bool successful);
		void BeginProgram();
		void StartProgramPage();
		void ServiceProgram();
		void FinishProgram(bool successful);
		void RestoreFlashController();

	public:
		bool HandleRoutineControl(
			std::uint8_t subFunction,
			std::uint16_t routineIdentifier,
			const std::uint8_t* optionRecord,
			std::size_t optionRecordLength,
			const EmbeddedIOServices::communication_send_callback_t& send);
		bool QueueWrite(
			std::uint32_t address,
			const std::uint8_t* data,
			std::size_t length,
			UDSFlashWriteCompletion completion);
		void Service(std::uint32_t now);
		bool Ready() const
		{
			return _operation == Operation::Idle && _eraseQueueCount == 0U &&
				_activeProgram.Data == nullptr && !_inventoryActive;
		}
	};
}

#endif
