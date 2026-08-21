#include "MPC5xxxFlexCAN2Service.h"
#include "LZ4.h"

using namespace EmbeddedIOServices;
using namespace MPC5xxx;

void ServiceCoreWatchdog();

// Override this weak implementation with the target flash driver. Returning
// false prevents TransferData from acknowledging data that was not programmed.
extern "C" __attribute__((weak)) bool WriteToFlash(std::uint32_t address,
                                                    const std::uint8_t* data,
                                                    std::size_t size) {
    (void)address;
    (void)data;
    (void)size;
    return false;
}

namespace {

constexpr std::uint32_t kDSPIDModuleConfiguration = 0x813F1900U;
constexpr std::uint32_t kDSPIDCTAR1Configuration = 0x78175561U;
constexpr std::uint32_t kDSPIBModuleConfigurationHalted = 0x813F0C01U;
// Application table 0xBF5E0. The former 0x78000000 value belongs to DSPI-A,
// not DSPI-B; it clocked the ASIC far too quickly and produced BFFF/FFFF RX.
constexpr std::uint32_t kDSPIBCTAR0Configuration = 0x78015503U;
constexpr std::uint32_t kDSPIADSIConfigurationHalted = 0x113F0C01U;
constexpr std::uint32_t kDSPICDSIConfigurationHalted = 0x913F0C01U;
constexpr std::uint32_t kDSPIADSICTAR1Configuration = 0x22003341U;
constexpr std::uint32_t kDSPICDSICTAR0Configuration = 0x7A003341U;
constexpr std::uint32_t kDSPIChainConfigurationASDR = 0x94080001U;
constexpr std::uint32_t kASICSerialOutputMask = 0x001FFFFFU;
constexpr std::uint32_t kASICApplicationSerialOutput = 0x000010U;
constexpr std::uint32_t kDSPIPCS0 = 0x00010000U;
constexpr std::uint32_t kDSPIPCS1 = 0x00020000U;
constexpr std::uint32_t kDSPICTAR3 = 0x30000000U;
constexpr std::uint32_t kDSPICTAR4 = 0x40000000U;
constexpr std::uint32_t kDSPICTAR5 = 0x50000000U;
constexpr std::uint32_t kDSPIContinuousPCS = 0x80000000U;
constexpr std::uint32_t kDSPIRxFifoDrainFlag = 0x00020000U;
constexpr std::uintptr_t kBootCompanionTxBufferAddress = 0x400000F6U;
constexpr std::size_t kDSPIBRotatingCaptureCount = 10U;
constexpr std::size_t kFirstInjectorTestGPIO = 132U;
constexpr std::size_t kFirstIgnitionTestGPIO = 167U;
constexpr std::size_t kEngineOutputTestGPIOCount = 8U;
constexpr std::size_t kPotentialC2MIOOutputEnableGPIO = 205U;
constexpr std::size_t kMPMNormalModeEnableGPIO = 182U;
constexpr std::size_t kMPMPacketByteCount = 18U;

// Retain the six bytes clocked back during each companion transaction. The
// periodic CAN data remains watchdog RX[0..2], then output RX[0..2].
volatile std::uint8_t g_companionWatchdogRx[6] = {};
volatile std::uint8_t g_companionOutputRx[6] = {};

// DSPI-D PCS1 cyclic MPM packet. Keep both sides visible in RAM for bench
// inspection. Byte 0 and byte 17 are updated immediately before each transfer.
volatile std::uint8_t g_mpmPCS1Tx[kMPMPacketByteCount] = {
    0x06U, 0xFFU, 0xFFU, 0x01U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0xFFU, 0x53U, 0x10U, 0x00U, 0x09U, 0xB2U,
};
volatile std::uint8_t g_mpmPCS1Rx[kMPMPacketByteCount] = {};

// Preserve the DSPI-B identification response and the revision selected from
// it so they are easy to inspect in RAM while bringing up the ASIC path.
// Keep both identification exchanges. The C2MIO reply is pipelined, so the
// first two received words precede the response consumed by discovery.
volatile std::uint16_t g_dspiBIdentificationRx[6] = {};
volatile std::uint16_t g_dspiBInitializationRx[19] = {};
volatile std::uint16_t g_dspiBModeRx[6] = {};
volatile std::uint16_t g_dspiBEnginePositionRx[4] = {};
volatile std::uint16_t g_dspiBHeartbeatRx[4] = {};
volatile std::uint16_t g_dspiBRotatingDiagnosticRx[2] = {};
std::uint16_t g_dspiBRotatingDiagnosticTx[2] = {0x011FU, 0x0000U};
std::size_t g_dspiBRotatingWireSequenceIndex = 0U;

struct DSPIBRotatingCapture {
    std::uint16_t Tx[2];
    std::uint16_t Rx[2];
};

DSPIBRotatingCapture
    g_dspiBRotatingCaptures[kDSPIBRotatingCaptureCount] = {};
std::size_t g_dspiBRotatingCaptureCount = 0U;
volatile std::uint16_t g_dspiBStatusRx[4] = {};
volatile std::uint16_t g_dspiBBulkDiagnosticRx[9] = {};
volatile std::uint16_t g_dspiBAnalogFrontendRx[12] = {};
volatile std::uint16_t g_dspiBChannelGroupRx[3] = {};
volatile std::uint16_t g_dspiBEngineStateTransitionRx[6] = {};
volatile std::uint8_t g_dspiBRawRevision = 0U;
volatile std::uint8_t g_dspiBRevision = 0U;

// Software image of the ASIC's secondary, transmit-only 21-bit DSI input.
// Bits 20:16 feed the five-bit DSPI-A segment and bits 15:0 feed the
// sixteen-bit DSPI-C segment. The hardware repeats the ASDR contents without
// CPU or DMA service after they have been written.
volatile std::uint32_t g_asicSerialOutput21 = 0U;
volatile std::uint32_t g_asicSerialStartTimebase = 0U;

void DelayTimebaseTicks(std::uint32_t ticks);
void ServiceDSPIBModeHeartbeat();
void ServiceCompanionWatchdog();
void ServiceMPMPCS1NormalMode();
std::uint16_t EncodeDSPIBDiagnosticRequest(std::uint16_t response);

// The bootloader services the companion from a 12.5 ms eMIOS interrupt.
// Run STM at 1 MHz (256 MHz system clock / 256) and poll it from main instead.
constexpr std::uint32_t kSTMConfiguration1MHz = 0x0000FF01U;
constexpr std::uint32_t kCompanionServicePeriodTicks = 12500U;
constexpr std::uint16_t kEMIOS11InterruptVector = 62U;
constexpr std::uint8_t kEMIOS11InterruptPriority = 2U;

constexpr std::uint32_t kFlashStart = 0x00000000U;
constexpr std::uint32_t kFlashEnd = 0x003FFFFFU;
constexpr std::uint32_t kSRAMStart = 0x40000000U;
constexpr std::uint32_t kSRAMEnd = 0x4003FFFFU;
constexpr std::uint16_t kMaximumReadMemoryLength = 4094U;
constexpr std::uint16_t kMaximumTransferDataMessageLength = 0x0FFFU;
constexpr std::uint8_t kLZ4DataFormatIdentifier = 0x10U;
constexpr std::uint32_t kMaximumLZ4OutputBlockLength = 4096U;
constexpr std::uint32_t kBrokenECCFlashRegionLength = 32U;
constexpr std::uint32_t kBrokenECCFlashRegion1 = 0x00003FE0U;
constexpr std::uint32_t kBrokenECCFlashRegion2 = 0x0001FFE0U;

// One shared, allocation-free staging buffer is sufficient because diagnostic
// requests are dispatched serially by the ISO-TP service.
std::uint8_t g_lz4BlockBuffer[kMaximumLZ4OutputBlockLength];

struct DownloadState {
    std::uint32_t Address = 0U;
    std::uint32_t Size = 0U;
    std::uint32_t BytesTransferred = 0U;
    std::uint8_t DataFormatIdentifier = 0U;
    std::uint8_t NextBlockSequenceCounter = 1U;
    std::uint8_t PreviousBlockSequenceCounter = 0U;
    bool PreviousBlockValid = false;
    bool Active = false;
};

DownloadState g_downloadState;

struct UploadState {
    std::uint32_t Address = 0U;
    std::uint32_t Size = 0U;
    std::uint32_t BytesTransferred = 0U;
    std::uint8_t DataFormatIdentifier = 0U;
    std::uint8_t NextBlockSequenceCounter = 1U;
    std::uint16_t PreviousResponseLength = 0U;
    bool PreviousResponseValid = false;
    bool Active = false;
    std::uint8_t PreviousResponse[kMaximumTransferDataMessageLength];
};

UploadState g_uploadState;

bool IsProtectedFlashByte(std::uint32_t address) {
    return (address >= kBrokenECCFlashRegion1 &&
            address < kBrokenECCFlashRegion1 + kBrokenECCFlashRegionLength) ||
           (address >= kBrokenECCFlashRegion2 &&
            address < kBrokenECCFlashRegion2 + kBrokenECCFlashRegionLength);
}

std::uint8_t ReadMemoryByte(std::uint32_t address) {
    if (IsProtectedFlashByte(address)) {
        return 0xFFU;
    }
    return *reinterpret_cast<const volatile std::uint8_t*>(address);
}

bool IsReadableMemoryRange(std::uint32_t address, std::uint32_t length) {
    if (length == 0U || length > kMaximumReadMemoryLength) {
        return false;
    }

    const std::uint32_t endAddress = address + length - 1U;
    if (endAddress < address) {
        return false;
    }

    return (address >= kFlashStart && endAddress <= kFlashEnd) ||
           (address >= kSRAMStart && endAddress <= kSRAMEnd);
}

bool IsMappedMemoryRange(std::uint32_t address, std::uint32_t length) {
    if (length == 0U) {
        return false;
    }

    const std::uint32_t endAddress = address + length - 1U;
    if (endAddress < address) {
        return false;
    }

    return (address >= kFlashStart && endAddress <= kFlashEnd) ||
           (address >= kSRAMStart && endAddress <= kSRAMEnd);
}

bool IsFlashRange(std::uint32_t address, std::uint32_t length) {
    if (length == 0U) {
        return false;
    }
    const std::uint32_t endAddress = address + length - 1U;
    return endAddress >= address &&
           address >= kFlashStart && endAddress <= kFlashEnd;
}

bool IsSRAMRange(std::uint32_t address, std::uint32_t length) {
    if (length == 0U) {
        return false;
    }
    const std::uint32_t endAddress = address + length - 1U;
    return endAddress >= address &&
           address >= kSRAMStart && endAddress <= kSRAMEnd;
}

size_t HandleDiagnosticRequest27(communication_send_callback_t send, const uint8_t *data, size_t length) {
    if (length >= 1U && data[0] == 0x01U) {
        static const std::uint8_t response[] = {
            0x67U, 0x01U, 0x00U, 0x00U
        };
        send(response, sizeof(response));
        return length;
    }
    return 0;
}

size_t HandleDiagnosticRequest23(communication_send_callback_t send, const uint8_t *data, size_t length) {
    if (length < 3U) {
        const std::uint8_t response[] = {0x7FU, 0x23U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    // ISO 14229-1 ALFID: low nibble is the address width and high nibble is
    // the memory-size width, both expressed in bytes.
    const std::uint8_t addressLength = data[0] & 0x0FU;
    const std::uint8_t sizeLength = data[0] >> 4U;
    if (addressLength == 0U || addressLength > 4U ||
        sizeLength == 0U || sizeLength > 4U ||
        length != static_cast<size_t>(1U + addressLength + sizeLength)) {
        const std::uint8_t response[] = {0x7FU, 0x23U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    std::uint32_t address = 0U;
    for (std::uint8_t i = 0U; i < addressLength; ++i) {
        address = (address << 8U) | data[1U + i];
    }

    std::uint32_t readLength = 0U;
    for (std::uint8_t i = 0U; i < sizeLength; ++i) {
        readLength = (readLength << 8U) | data[1U + addressLength + i];
    }

    if (!IsReadableMemoryRange(address, readLength)) {
        const std::uint8_t response[] = {0x7FU, 0x23U, 0x31U};
        send(response, sizeof(response));
        return length;
    }

    // The callback is serviced serially by the ISO-TP service.
    std::uint8_t response[1U + readLength];
    response[0] = 0x63U;

    for (std::uint32_t i = 0U; i < readLength; ++i) {
        response[1U + i] = ReadMemoryByte(address + i);
    }

    send(response, 1U + readLength);
    return length;
}

size_t HandleDiagnosticRequest34(communication_send_callback_t send,
                                 const uint8_t* data,
                                 size_t length) {
    // RequestDownload: DFI, ALFID, memoryAddress, memorySize.
    if (length < 4U) {
        const std::uint8_t response[] = {0x7FU, 0x34U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    const std::uint8_t dataFormatIdentifier = data[0];
    const std::uint8_t addressLength = data[1] & 0x0FU;
    const std::uint8_t sizeLength = data[1] >> 4U;
    if (addressLength == 0U || addressLength > 4U ||
        sizeLength == 0U || sizeLength > 4U ||
        length != static_cast<size_t>(2U + addressLength + sizeLength)) {
        const std::uint8_t response[] = {0x7FU, 0x34U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    // Compression method 1 is this kernel's raw, independent LZ4 block format.
    // Encryption remains unsupported.
    if (dataFormatIdentifier != 0x00U &&
        dataFormatIdentifier != kLZ4DataFormatIdentifier) {
        const std::uint8_t response[] = {0x7FU, 0x34U, 0x31U};
        send(response, sizeof(response));
        return length;
    }

    std::uint32_t address = 0U;
    for (std::uint8_t i = 0U; i < addressLength; ++i) {
        address = (address << 8U) | data[2U + i];
    }

    std::uint32_t downloadSize = 0U;
    for (std::uint8_t i = 0U; i < sizeLength; ++i) {
        downloadSize = (downloadSize << 8U) |
                       data[2U + addressLength + i];
    }

    if (!IsMappedMemoryRange(address, downloadSize)) {
        const std::uint8_t response[] = {0x7FU, 0x34U, 0x31U};
        send(response, sizeof(response));
        return length;
    }

    g_downloadState.Address = address;
    g_downloadState.Size = downloadSize;
    g_downloadState.BytesTransferred = 0U;
    g_downloadState.DataFormatIdentifier = dataFormatIdentifier;
    g_downloadState.NextBlockSequenceCounter = 1U;
    g_downloadState.PreviousBlockSequenceCounter = 0U;
    g_downloadState.PreviousBlockValid = false;
    g_downloadState.Active = true;
    g_uploadState.Active = false;

    // A lengthFormatIdentifier of 0x20 says maxNumberOfBlockLength occupies
    // two bytes. The value includes the 0x36 SID and block sequence counter.
    const std::uint8_t response[] = {
        0x74U,
        0x20U,
        static_cast<std::uint8_t>(kMaximumTransferDataMessageLength >> 8U),
        static_cast<std::uint8_t>(kMaximumTransferDataMessageLength)
    };
    send(response, sizeof(response));
    return length;
}

size_t HandleDiagnosticRequest35(communication_send_callback_t send,
                                 const uint8_t* data,
                                 size_t length) {
    // RequestUpload: DFI, ALFID, memoryAddress, memorySize.
    if (length < 4U) {
        const std::uint8_t response[] = {0x7FU, 0x35U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    const std::uint8_t dataFormatIdentifier = data[0];
    const std::uint8_t addressLength = data[1] & 0x0FU;
    const std::uint8_t sizeLength = data[1] >> 4U;
    if (addressLength == 0U || addressLength > 4U ||
        sizeLength == 0U || sizeLength > 4U ||
        length != static_cast<size_t>(2U + addressLength + sizeLength)) {
        const std::uint8_t response[] = {0x7FU, 0x35U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    if (dataFormatIdentifier != 0x00U &&
        dataFormatIdentifier != kLZ4DataFormatIdentifier) {
        const std::uint8_t response[] = {0x7FU, 0x35U, 0x31U};
        send(response, sizeof(response));
        return length;
    }

    std::uint32_t address = 0U;
    for (std::uint8_t i = 0U; i < addressLength; ++i) {
        address = (address << 8U) | data[2U + i];
    }

    std::uint32_t uploadSize = 0U;
    for (std::uint8_t i = 0U; i < sizeLength; ++i) {
        uploadSize = (uploadSize << 8U) |
                     data[2U + addressLength + i];
    }

    // Unlike ReadMemoryByAddress, RequestUpload is a streamed operation, so
    // the total size is limited only by the mapped address range.
    if (!IsMappedMemoryRange(address, uploadSize)) {
        const std::uint8_t response[] = {0x7FU, 0x35U, 0x31U};
        send(response, sizeof(response));
        return length;
    }

    g_uploadState.Address = address;
    g_uploadState.Size = uploadSize;
    g_uploadState.BytesTransferred = 0U;
    g_uploadState.DataFormatIdentifier = dataFormatIdentifier;
    g_uploadState.NextBlockSequenceCounter = 1U;
    g_uploadState.PreviousResponseLength = 0U;
    g_uploadState.PreviousResponseValid = false;
    g_uploadState.Active = true;
    g_downloadState.Active = false;

    // For an upload, maxNumberOfBlockLength is the maximum complete 0x76
    // response: SID + block sequence counter + data.
    const std::uint8_t response[] = {
        0x75U,
        0x20U,
        static_cast<std::uint8_t>(kMaximumTransferDataMessageLength >> 8U),
        static_cast<std::uint8_t>(kMaximumTransferDataMessageLength)
    };
    send(response, sizeof(response));
    return length;
}

size_t HandleUploadTransferData(communication_send_callback_t send,
                                const uint8_t* data,
                                size_t length) {
    // During RequestUpload, TransferData contains only the block sequence
    // counter. The uploaded bytes are carried in the positive response.
    if (length != 1U) {
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    if (!g_uploadState.Active) {
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x24U};
        send(response, sizeof(response));
        return length;
    }

    const std::uint8_t blockSequenceCounter = data[0];
    if (g_uploadState.PreviousResponseValid &&
        blockSequenceCounter ==
            g_uploadState.PreviousResponse[1]) {
        // The client may repeat the previous request if its 0x76 response was
        // lost. Replay the exact response without advancing transfer state.
        send(g_uploadState.PreviousResponse,
             g_uploadState.PreviousResponseLength);
        return length;
    }

    if (blockSequenceCounter !=
        g_uploadState.NextBlockSequenceCounter) {
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x73U};
        send(response, sizeof(response));
        return length;
    }

    if (g_uploadState.BytesTransferred >= g_uploadState.Size) {
        // All requested bytes have been transferred; the next valid service
        // is RequestTransferExit (0x37), not another TransferData request.
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x24U};
        send(response, sizeof(response));
        return length;
    }

    const std::uint32_t remaining =
        g_uploadState.Size - g_uploadState.BytesTransferred;
    const std::uint32_t blockAddress =
        g_uploadState.Address + g_uploadState.BytesTransferred;

    g_uploadState.PreviousResponse[0] = 0x76U;
    g_uploadState.PreviousResponse[1] = blockSequenceCounter;
    std::uint32_t blockLength;
    if (g_uploadState.DataFormatIdentifier == kLZ4DataFormatIdentifier) {
        blockLength = remaining < kMaximumLZ4OutputBlockLength
                          ? remaining
                          : kMaximumLZ4OutputBlockLength;
        for (std::uint32_t i = 0U; i < blockLength; ++i) {
            g_lz4BlockBuffer[i] = ReadMemoryByte(blockAddress + i);
        }

        std::size_t compressedLength;
        Kernel::LZ4EncodeResult encodeResult;
        do {
            compressedLength = kMaximumTransferDataMessageLength - 4U;
            encodeResult = Kernel::EncodeLZ4Block(
                g_lz4BlockBuffer, blockLength,
                g_uploadState.PreviousResponse + 4U, compressedLength);
            if (encodeResult == Kernel::LZ4EncodeResult::OutputTooSmall) {
                blockLength /= 2U;
            }
        } while (encodeResult == Kernel::LZ4EncodeResult::OutputTooSmall &&
                 blockLength != 0U);

        if (encodeResult != Kernel::LZ4EncodeResult::Success) {
            g_uploadState.Active = false;
            const std::uint8_t response[] = {0x7FU, 0x36U, 0x72U};
            send(response, sizeof(response));
            return length;
        }

        g_uploadState.PreviousResponse[2] =
            static_cast<std::uint8_t>(blockLength >> 8U);
        g_uploadState.PreviousResponse[3] =
            static_cast<std::uint8_t>(blockLength);
        g_uploadState.PreviousResponseLength =
            static_cast<std::uint16_t>(4U + compressedLength);
    } else {
        constexpr std::uint32_t maximumDataLength =
            kMaximumTransferDataMessageLength - 2U;
        blockLength = remaining < maximumDataLength
                          ? remaining
                          : maximumDataLength;
        for (std::uint32_t i = 0U; i < blockLength; ++i) {
            g_uploadState.PreviousResponse[2U + i] =
                ReadMemoryByte(blockAddress + i);
        }
        g_uploadState.PreviousResponseLength =
            static_cast<std::uint16_t>(2U + blockLength);
    }

    g_uploadState.PreviousResponseValid = true;
    g_uploadState.BytesTransferred += blockLength;
    g_uploadState.NextBlockSequenceCounter =
        static_cast<std::uint8_t>(blockSequenceCounter + 1U);

    send(g_uploadState.PreviousResponse,
         g_uploadState.PreviousResponseLength);
    return length;
}

size_t HandleDownloadTransferData(communication_send_callback_t send,
                                  const uint8_t* data,
                                  size_t length) {
    // During RequestDownload, TransferData contains a block sequence counter
    // followed by the bytes to write into the requested memory range.
    if (length < 2U ||
        length > kMaximumTransferDataMessageLength - 1U) {
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    const std::uint8_t blockSequenceCounter = data[0];
    if (g_downloadState.PreviousBlockValid &&
        blockSequenceCounter ==
            g_downloadState.PreviousBlockSequenceCounter) {
        // The previous data was already committed. A repeated counter means
        // the client lost the acknowledgement, so acknowledge without writing
        // or advancing the destination a second time.
        const std::uint8_t response[] = {0x76U, blockSequenceCounter};
        send(response, sizeof(response));
        return length;
    }

    if (blockSequenceCounter !=
        g_downloadState.NextBlockSequenceCounter) {
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x73U};
        send(response, sizeof(response));
        return length;
    }

    if (g_downloadState.BytesTransferred >= g_downloadState.Size) {
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x24U};
        send(response, sizeof(response));
        return length;
    }

    const bool isLZ4 =
        g_downloadState.DataFormatIdentifier == kLZ4DataFormatIdentifier;
    if (isLZ4 && length < 4U) {
        // BSC, two-byte uncompressed length, and at least one LZ4 byte.
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    std::uint32_t blockLength = static_cast<std::uint32_t>(length - 1U);
    const std::uint8_t* blockData = data + 1U;
    if (isLZ4) {
        blockLength = (static_cast<std::uint32_t>(data[1]) << 8U) |
                      static_cast<std::uint32_t>(data[2]);
        if (blockLength == 0U ||
            blockLength > kMaximumLZ4OutputBlockLength) {
            const std::uint8_t response[] = {0x7FU, 0x36U, 0x31U};
            send(response, sizeof(response));
            return length;
        }

        const Kernel::LZ4DecodeResult decodeResult =
            Kernel::DecodeLZ4Block(data + 3U, length - 3U,
                                   g_lz4BlockBuffer, blockLength);
        if (decodeResult != Kernel::LZ4DecodeResult::Success) {
            const std::uint8_t response[] = {0x7FU, 0x36U, 0x72U};
            send(response, sizeof(response));
            return length;
        }
        blockData = g_lz4BlockBuffer;
    }

    const std::uint32_t remaining =
        g_downloadState.Size - g_downloadState.BytesTransferred;
    if (blockLength > remaining) {
        // The block would exceed the range authorized by RequestDownload.
        g_downloadState.Active = false;
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x71U};
        send(response, sizeof(response));
        return length;
    }

    const std::uint32_t blockAddress =
        g_downloadState.Address + g_downloadState.BytesTransferred;

    bool writeSucceeded = false;
    if (IsSRAMRange(blockAddress, blockLength)) {
        volatile auto* memory =
            reinterpret_cast<volatile std::uint8_t*>(blockAddress);
        for (std::uint32_t i = 0U; i < blockLength; ++i) {
            memory[i] = blockData[i];
        }
        writeSucceeded = true;
    } else if (IsFlashRange(blockAddress, blockLength)) {
        writeSucceeded = WriteToFlash(blockAddress, blockData, blockLength);
    }

    if (!writeSucceeded) {
        g_downloadState.Active = false;
        const std::uint8_t response[] = {0x7FU, 0x36U, 0x72U};
        send(response, sizeof(response));
        return length;
    }

    g_downloadState.BytesTransferred += blockLength;
    g_downloadState.PreviousBlockSequenceCounter = blockSequenceCounter;
    g_downloadState.PreviousBlockValid = true;
    g_downloadState.NextBlockSequenceCounter =
        static_cast<std::uint8_t>(blockSequenceCounter + 1U);

    const std::uint8_t response[] = {0x76U, blockSequenceCounter};
    send(response, sizeof(response));
    return length;
}

size_t HandleDiagnosticRequest36(communication_send_callback_t send,
                                 const uint8_t* data,
                                 size_t length) {
    if (g_uploadState.Active) {
        return HandleUploadTransferData(send, data, length);
    }
    if (g_downloadState.Active) {
        return HandleDownloadTransferData(send, data, length);
    }

    const std::uint8_t response[] = {0x7FU, 0x36U, 0x24U};
    send(response, sizeof(response));
    return length;
}

size_t HandleDiagnosticRequest37(communication_send_callback_t send,
                                 const uint8_t* data,
                                 size_t length) {
    // This format has no transferRequestParameterRecord.
    if (length != 0U) {
        const std::uint8_t response[] = {0x7FU, 0x37U, 0x13U};
        send(response, sizeof(response));
        return length;
    }

    const bool downloadComplete =
        g_downloadState.Active &&
        g_downloadState.BytesTransferred == g_downloadState.Size;
    const bool uploadComplete =
        g_uploadState.Active &&
        g_uploadState.BytesTransferred == g_uploadState.Size;
    if (!downloadComplete && !uploadComplete) {
        const std::uint8_t response[] = {0x7FU, 0x37U, 0x24U};
        send(response, sizeof(response));
        return length;
    }

    g_downloadState.Active = false;
    g_uploadState.Active = false;
    const std::uint8_t response[] = {0x77U};
    send(response, sizeof(response));
    return length;
}

size_t HandleDiagnosticRequest(communication_send_callback_t send,
                                    const void* data,
                                    size_t length) {
    const auto* request = static_cast<const uint8_t*>(data);

    if (length == 0U) {
        return 0U;
    }

    switch(request[0]) {
        case 0x23U:
            HandleDiagnosticRequest23(send, request + 1, length - 1U);
            return length;
        case 0x34U:
            HandleDiagnosticRequest34(send, request + 1, length - 1U);
            return length;
        case 0x35U:
            HandleDiagnosticRequest35(send, request + 1, length - 1U);
            return length;
        case 0x36U:
            HandleDiagnosticRequest36(send, request + 1, length - 1U);
            return length;
        case 0x37U:
            HandleDiagnosticRequest37(send, request + 1, length - 1U);
            return length;
        case 0x27U:
            HandleDiagnosticRequest27(send, request + 1, length - 1U);
            return length;
        default: {
            const std::uint8_t response[] = {0x7FU, request[0], 0x11U};
            send(response, sizeof(response));
            return length;
        }
    }

}

void InitializeCompanionDSPI() {
    // Match the DSPI-D setup used by the bootloader. The bootloader has already
    // configured the DSPI-D pads before handing control to this image.
    DSPI_D.MCR.R = kDSPIDModuleConfiguration;
    DSPI_D.TCR.R &= 0x0000FFFFU;
    DSPI_D.RSER.R = kDSPIRxFifoDrainFlag;
    DSPI_D.CTAR[0].R = 0x00000000U;
    DSPI_D.CTAR[1].R = kDSPIDCTAR1Configuration;
    DSPI_D.CTAR[2].R = 0x3AFC3879U;
    DSPI_D.CTAR[3].R = 0x3AFC3879U;
    DSPI_D.CTAR[4].R = 0x3AEC3C09U;
    DSPI_D.CTAR[5].R = 0x3ADC3B79U;
    DSPI_D.SR.R = 0x90020000U;

    // Exact application mux for the MPM chip select on DSPI-D PCS1.
    SIU.PCR[91].R = 0x0A04U;

}

void SetASICSerialOutput(std::uint32_t value) {
    value &= kASICSerialOutputMask;
    g_asicSerialOutput21 = value;

    // ASDR is double-buffered; writes take effect at a DSI frame boundary.
    // Only the low FMSZ bits participate in each half of this serial chain.
    DSPI_A.ASDR.R = (value >> 16U) & 0x001FU;
    DSPI_C.ASDR.R = value & 0xFFFFU;
}

void InitializeASICSerialOutput() {
    // Exact application pad image for the transmit-only ASIC connection:
    // DSPI-C supplies SCK/PCS0, while the chained frame exits on SOUTA.
    SIU.PCR[95].R = 0x060CU;   // SOUTA
    SIU.PCR[109].R = 0x0A0CU;  // SCKC
    SIU.PCR[110].R = 0x0A0CU;  // PCSC0

    // Route SOUTC, SCKC, and PCSC0 internally into the DSPI-A slave. DSPI-A
    // contributes five bits before forwarding DSPI-C's sixteen-bit segment
    // to the external SOUTA pin.
    SIU.DISR.R = 0xA8000100U;

    // Configure both modules while halted. These are the application's exact
    // DSI MCR/CTAR values. TXSS is the one deliberate difference: setting it
    // selects the writable ASDR registers instead of live eTPU SDR inputs.
    DSPI_A.MCR.R = kDSPIADSIConfigurationHalted;
    DSPI_C.MCR.R = kDSPICDSIConfigurationHalted;
    DSPI_A.TCR.R = 0U;
    DSPI_C.TCR.R = 0U;
    DSPI_A.RSER.R = 0U;
    DSPI_C.RSER.R = 0U;
    DSPI_A.CTAR[1].R = kDSPIADSICTAR1Configuration;
    DSPI_C.CTAR[0].R = kDSPICDSICTAR0Configuration;
    DSPI_A.DSICR.R = kDSPIChainConfigurationASDR;
    DSPI_C.DSICR.R = kDSPIChainConfigurationASDR;
    DSPI_A.PUSHR.R = kDSPIPCS0;
    DSPI_C.PUSHR.R = kDSPIPCS0;

    // Reproduce the application's observed 24-bit scope decode after
    // discarding its three padding bits: the actual DSI frame is 21 bits.
    SetASICSerialOutput(kASICApplicationSerialOutput);

    // Preserve the application's external-clock configuration written by
    // the same setup routine, then arm the slave before starting the master.
    SIU.ECCR.R = 0x00000801U;
    DSPI_A.MCR.R = kDSPIADSIConfigurationHalted & ~1U;
    DSPI_C.MCR.R = kDSPICDSIConfigurationHalted & ~1U;
    asm volatile("mftb %0" : "=r"(g_asicSerialStartTimebase));
}

void InitializeBootloaderCompanionWatchdogBuffer() {
    // Recreate the bootloader's group-5 TX buffer after application-style
    // startup traffic is complete. The bootloader callback alternates the
    // protected five-bit field on subsequent services.
    auto* const message = reinterpret_cast<volatile std::uint16_t*>(
        kBootCompanionTxBufferAddress);
    message[0] = 0x6AFFU;
    message[1] = 0xFFFFU;
    message[2] = 0xFFFFU;
}

std::uint16_t TransferDSPIBWord(std::uint16_t word,
                                bool keepPCSAsserted) {
    // The application uses CTAR0 for these 16-bit, MSB-first, mode-0 frames.
    // Polling RFDF keeps this startup path independent of DMA and interrupts.
    DSPI_B.SR.R = kDSPIRxFifoDrainFlag;
    DSPI_B.PUSHR.R = kDSPIPCS0 |
                     (keepPCSAsserted ? kDSPIContinuousPCS : 0U) | word;

    while ((DSPI_B.SR.R & kDSPIRxFifoDrainFlag) == 0U) {}
    return static_cast<std::uint16_t>(DSPI_B.POPR.R);
}

template <std::size_t WordCount>
void TransferDSPIBMessage(const std::uint16_t (&tx)[WordCount],
                          volatile std::uint16_t* rx = nullptr) {
    for (std::size_t i = 0; i < WordCount; ++i) {
        const std::uint16_t received =
            TransferDSPIBWord(tx[i], i + 1U != WordCount);
        if (rx != nullptr) {
            rx[i] = received;
        }
    }
}

void InitializeDSPIBAsic() {
    // Exact E78 application PCR image for SCKB, SINB, SOUTB, and PCSB0.
    SIU.PCR[102].R = 0x0604U;
    SIU.PCR[103].R = 0x0514U;
    SIU.PCR[104].R = 0x0614U;
    SIU.PCR[105].R = 0x0604U;

    // Configure while halted, clear both FIFOs, install the application's
    // CTAR0 timing, clear stale status, then start the module.
    DSPI_B.MCR.R = kDSPIBModuleConfigurationHalted;
    DSPI_B.TCR.R = 0U;
    DSPI_B.RSER.R = 0U;
    DSPI_B.CTAR[0].R = kDSPIBCTAR0Configuration;
    DSPI_B.SR.R = 0x9A0A0000U;
    DSPI_B.MCR.R = kDSPIBModuleConfigurationHalted & ~1U;

    static constexpr std::uint16_t identificationQuery[] = {
        0x0E1BU, 0x0000U,
    };

    // E78 performs this identification exchange twice before classifying the
    // three-bit revision field in bits 14:12 of the second response word.
    TransferDSPIBMessage(identificationQuery, g_dspiBIdentificationRx);
    TransferDSPIBMessage(identificationQuery,
                         g_dspiBIdentificationRx + 2U);

    // The application consumes the delayed response rather than the leading
    // filler/global words. Preserve the raw field for capture comparison. The
    // observed E78 wire sequence (including the revision-4-only 0F1D 1450
    // phase) proves this hardware must select the class-4 initialization.
    // Until the exact C2MIO pipeline word is completely decoded, do not let a
    // leading 2F49 filler word incorrectly select the class-3 table.
    g_dspiBRawRevision = static_cast<std::uint8_t>(
        (g_dspiBIdentificationRx[3] >> 12U) & 0x07U);
    g_dspiBRevision = 4U;

    // Stock ordering starts the DSI engine after DSPI-B identification but
    // immediately before the revision-selected nineteen-word initialization.
    InitializeASICSerialOutput();

    static constexpr std::uint16_t revision4Initialization[] = {
        0xCF4CU, 0x25B7U, 0x16ECU, 0x0011U, 0x1E10U,
        0x1E11U, 0x1331U, 0x7575U, 0x0055U, 0x3E00U,
        0x0013U, 0xFB82U, 0x0080U, 0x0080U, 0x0A80U,
        0x0000U, 0x0000U, 0x0000U, 0x00F0U,
    };
    static constexpr std::uint16_t revision3Initialization[] = {
        0xCF4CU, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
        0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
        0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
        0x0000U, 0x0000U, 0x0000U, 0x0000U,
    };

    if (g_dspiBRevision == 3U) {
        TransferDSPIBMessage(revision4Initialization,
                             g_dspiBInitializationRx);

        // Revision 3 performs one additional selector-13 transaction. This is
        // only the first four initialization words; it is not a second copy
        // of the complete nineteen-word selector-0 transaction.
        // static constexpr std::uint16_t revision3Followup[] = {
        //     0xCF4CU, 0x25B7U, 0x16ECU, 0x0011U,
        // };
        // TransferDSPIBMessage(revision3Followup);
    } else {
        TransferDSPIBMessage(revision4Initialization,
                             g_dspiBInitializationRx);
    }

    // Stock performs a third identification 1.75 ms after the initialization
    // block, immediately before the live selector startup sequence.
    DelayTimebaseTicks(0x0000DAC0U);
    TransferDSPIBMessage(identificationQuery,
                         g_dspiBIdentificationRx + 4U);

    // The experimental selector 1-5, 10, and 14 writes formerly sent here do
    // not have proven stock E78 callers and are intentionally omitted.
}

__attribute__((unused)) void RefreshDSPIBIdentification() {
    static constexpr std::uint16_t identificationQuery[] = {
        0x0E1BU, 0x0000U,
    };
    TransferDSPIBMessage(identificationQuery,
                         g_dspiBIdentificationRx + 2U);
}

void SendDSPIBBulkDiagnosticQuery() {
    static constexpr std::uint16_t bulkDiagnosticQuery[] = {
        0x835FU, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
        0x0000U, 0x0000U, 0x0000U, 0x0000U,
    };
    TransferDSPIBMessage(bulkDiagnosticQuery, g_dspiBBulkDiagnosticRx);
}

void SendDSPIBRotatingDiagnosticAndAdvance() {
    // The C2MIO response is delayed by one 16-bit transfer. Stock firmware
    // reads selector-11 RX[1] at 0x4000CAC4, skipping RX[0], then prepares the
    // next page after the completed transaction. It never derives this field
    // from the following selector-7/status-query response.
    // Literal first-120-ms stock sequence. This gets the ASIC through FSE
    // startup while retaining raw captures so later rolling values can be
    // validated independently.
    static constexpr std::uint16_t capturedTx[][2] = {
        {0x011FU, 0x0000U},
        {0x0F00U, 0x0023U},
        {0x011FU, 0x0023U},
        {0x0F00U, 0x0012U},
        {0x011FU, 0x0012U},
        {0x0F00U, 0x001EU},
        {0x011FU, 0x001EU},
    };
    constexpr std::size_t capturedTxCount =
        sizeof(capturedTx) / sizeof(capturedTx[0]);
    if (g_dspiBRotatingWireSequenceIndex < capturedTxCount) {
        g_dspiBRotatingDiagnosticTx[0] =
            capturedTx[g_dspiBRotatingWireSequenceIndex][0];
        g_dspiBRotatingDiagnosticTx[1] =
            capturedTx[g_dspiBRotatingWireSequenceIndex][1];
    }
    const bool sentPage1 =
        (g_dspiBRotatingDiagnosticTx[0] & 0x0F00U) == 0x0100U;
    DSPIBRotatingCapture* capture = nullptr;
    if (g_dspiBRotatingCaptureCount < kDSPIBRotatingCaptureCount) {
        capture = &g_dspiBRotatingCaptures[g_dspiBRotatingCaptureCount];
        capture->Tx[0] = g_dspiBRotatingDiagnosticTx[0];
        capture->Tx[1] = g_dspiBRotatingDiagnosticTx[1];
    }
    TransferDSPIBMessage(g_dspiBRotatingDiagnosticTx,
                         g_dspiBRotatingDiagnosticRx);

    if (capture != nullptr) {
        capture->Rx[0] = g_dspiBRotatingDiagnosticRx[0];
        capture->Rx[1] = g_dspiBRotatingDiagnosticRx[1];
        ++g_dspiBRotatingCaptureCount;
    }

    if (g_dspiBRotatingWireSequenceIndex < capturedTxCount) {
        ++g_dspiBRotatingWireSequenceIndex;
        if (g_dspiBRotatingWireSequenceIndex < capturedTxCount) {
            g_dspiBRotatingDiagnosticTx[0] =
                capturedTx[g_dspiBRotatingWireSequenceIndex][0];
            g_dspiBRotatingDiagnosticTx[1] =
                capturedTx[g_dspiBRotatingWireSequenceIndex][1];
            return;
        }
    }

    if (sentPage1) {
        g_dspiBRotatingDiagnosticTx[0] = 0x0F00U;
        g_dspiBRotatingDiagnosticTx[1] = static_cast<std::uint16_t>(
            (g_dspiBRotatingDiagnosticTx[1] & 0xFFC0U) |
            EncodeDSPIBDiagnosticRequest(
                g_dspiBRotatingDiagnosticRx[1] & 0x003FU));
    } else {
        // Page 1 retains the protected value used by the preceding page F.
        g_dspiBRotatingDiagnosticTx[0] = 0x011FU;
    }
}

void RunCapturedDSPIBPostInitializationSequence() {
    // Exact revision-4 E78 wire order captured on SOUTB after the main
    // revision-selected ASIC initialization transaction.  Keep these as
    // individual transfers because PCSB0 is released between every message.
    static constexpr std::uint16_t pairedQualification[] = {
        0x0F1AU, 0x0082U,
    };
    static constexpr std::uint16_t frontendMeasure[] = {
        0x0F1DU, 0x1450U,
    };
    static constexpr std::uint16_t frontendRun[] = {
        0x0F1DU, 0x04F0U,
    };
    static constexpr std::uint16_t asicRunMode[] = {
        0x0F14U, 0x3E20U,
    };
    static constexpr std::uint16_t statusQuery[] = {
        0x0B01U, 0x0000U, 0x0000U, 0x1FC0U,
    };
    static constexpr std::uint16_t initialStatusQuery[] = {
        0x0B01U, 0x0000U, 0x0000U, 0x0000U,
    };
    static constexpr std::uint16_t channelGroupConfiguration[] = {
        0x0F52U, 0x7575U, 0x0055U,
    };

    DelayTimebaseTicks(0x000012C0U); // 150 us after third identification
    TransferDSPIBMessage(pairedQualification,
                         g_dspiBEnginePositionRx);

    DelayTimebaseTicks(0x0003A980U); // 7.5 ms
    ServiceCoreWatchdog();
    ServiceCompanionWatchdog();
    TransferDSPIBMessage(frontendMeasure,
                         g_dspiBAnalogFrontendRx);
    DelayTimebaseTicks(0x00000F00U); // 120 us
    TransferDSPIBMessage(frontendRun,
                         g_dspiBAnalogFrontendRx + 2U);
    DelayTimebaseTicks(0x00000500U); // 40 us

    SendDSPIBBulkDiagnosticQuery();
    DelayTimebaseTicks(0x00023280U); // 4.5 ms
    ServiceCoreWatchdog();
    ServiceCompanionWatchdog();
    TransferDSPIBMessage(asicRunMode, g_dspiBModeRx);

    DelayTimebaseTicks(0x000003C0U); // 30 us
    TransferDSPIBMessage(pairedQualification,
                         g_dspiBEnginePositionRx + 2U);

    // The application iterates four logical selector-4 options.  All four
    // calibration values are zero on this E78, so every resulting transfer
    // is the same 0F1D 04F0 message.
    for (std::size_t option = 0U; option < 4U; ++option) {
        DelayTimebaseTicks(option == 0U ? 0x000003C0U : 0x000001E0U);
        TransferDSPIBMessage(frontendRun,
                             g_dspiBAnalogFrontendRx + 4U + option * 2U);
    }

    DelayTimebaseTicks(0x00017700U); // 3 ms
    ServiceCoreWatchdog();
    ServiceCompanionWatchdog();
    SendDSPIBBulkDiagnosticQuery();
    DelayTimebaseTicks(0x000012C0U); // 150 us
    SendDSPIBRotatingDiagnosticAndAdvance();
    DelayTimebaseTicks(0x000001E0U); // 15 us
    TransferDSPIBMessage(initialStatusQuery, g_dspiBStatusRx);
    DelayTimebaseTicks(0x000003C0U); // 30 us
    TransferDSPIBMessage(channelGroupConfiguration,
                         g_dspiBChannelGroupRx);

    // Four selector-12 reads at 3 ms cadence precede every rotating request.
    // Reproduce six more captured rotating transfers after the initial one.
    for (std::size_t rotating = 1U; rotating < 7U; ++rotating) {
        for (std::size_t query = 0U; query < 4U; ++query) {
            DelayTimebaseTicks(query == 0U && rotating == 1U
                                   ? 0x0000DAC0U  // 1.75 ms after 0F52
                                   : 0x00017700U); // otherwise 3 ms
            SendDSPIBBulkDiagnosticQuery();
            ServiceCoreWatchdog();
            ServiceCompanionWatchdog();
        }
        DelayTimebaseTicks(0x000012C0U); // 150 us
        SendDSPIBRotatingDiagnosticAndAdvance();
        DelayTimebaseTicks(0x000001E0U); // 15 us
        TransferDSPIBMessage(statusQuery, g_dspiBStatusRx);

        // Do not starve either watchdog during the 12 ms cyclic cadence.
        ServiceCoreWatchdog();
        ServiceCompanionWatchdog();
        ServiceMPMPCS1NormalMode();
    }
}

__attribute__((unused)) void RunDSPIBEngineStateTransitionTest() {
    // The first two packed selector-10 fields cover two eight-channel banks.
    // Ignition diagnostics occupy the first bank; the injector candidate is
    // the second. Payload 0x7575 assigns mode 1 to both banks. Program that
    // common state through the selector-10 configuration opcode first, then
    // issue the application's live-update opcode with the identical payload.
    // Retain both pipelined responses for RAM inspection.
    static constexpr std::uint16_t configureMatchingBankModes[] = {
        0x0F12U, 0x7575U, 0x0055U,
    };
    static constexpr std::uint16_t applyMatchingBankModes[] = {
        0x0F52U, 0x7575U, 0x0055U,
    };

    TransferDSPIBMessage(configureMatchingBankModes,
                         g_dspiBEngineStateTransitionRx);
    // The MPC5566 timebase used by the application runs at 32 MHz, making
    // 0x7D000 ticks approximately 16 ms.
    DelayTimebaseTicks(0x0007D000U);
    TransferDSPIBMessage(applyMatchingBankModes,
                         g_dspiBEngineStateTransitionRx + 3U);
}

void InitializeEngineOutputTestGPIOs() {
    // The stock E78 GPDO/PCR ROM image initializes GPIO205 once as a high
    // GPIO output (GPDO=1, PCR=0x0210), and application code never changes it
    // afterward. That set-once behavior and its internal-only board routing
    // make it the strongest current candidate for C2MIO's discrete OUTEN.
    // Reassert the exact stock state here so it cannot depend on a preceding
    // bootloader state or on whether the flash-backed SIU copy was accepted.
    SIU.GPDO[kPotentialC2MIOOutputEnableGPIO].B.PDO = 1U;
    SIU.PCR[kPotentialC2MIOOutputEnableGPIO].R = 0x0210U;

    for (std::size_t channel = 0U;
         channel < kEngineOutputTestGPIOCount; ++channel) {
        const std::size_t injectorGPIO = kFirstInjectorTestGPIO + channel;
        const std::size_t ignitionGPIO = kFirstIgnitionTestGPIO + channel;

        // Load the inactive state before enabling the output buffers so the
        // C2MIO inputs do not see a configuration-time pulse.
        SIU.GPDO[injectorGPIO].B.PDO = 0U;
        SIU.GPDO[ignitionGPIO].B.PDO = 0U;

        // PA=0 selects SIU GPIO instead of eTPU. These are deliberate test
        // outputs; retain no input buffer or peripheral ownership.
        SIU.PCR[injectorGPIO].B.PA = 0U;
        SIU.PCR[injectorGPIO].B.IBE = 0U;
        SIU.PCR[injectorGPIO].B.OBE = 1U;
        SIU.PCR[ignitionGPIO].B.PA = 0U;
        SIU.PCR[ignitionGPIO].B.IBE = 0U;
        SIU.PCR[ignitionGPIO].B.OBE = 1U;
    }
}

void SetEngineOutputTestGPIOs(bool state) {
    for (std::size_t channel = 0U;
         channel < kEngineOutputTestGPIOCount; ++channel) {
        SIU.GPDO[kFirstInjectorTestGPIO + channel].B.PDO = state;
        SIU.GPDO[kFirstIgnitionTestGPIO + channel].B.PDO = state;
    }
}

std::uint16_t EncodeDSPIBDiagnosticRequest(std::uint16_t response) {
    // Reproduce FUN_000c7928: carry the five returned status bits into bits
    // 5..1 and synthesize the protected bit in bit 0.
    response &= 0x003FU;
    const bool allLowFiveSet = (response & 0x001FU) == 0x001FU;
    const bool bit4 = (response & 0x0010U) != 0U;
    const bool bit5 = (response & 0x0020U) != 0U;
    const std::uint16_t protectedBit =
        static_cast<std::uint16_t>((allLowFiveSet ^ bit4) == bit5);
    return static_cast<std::uint16_t>(((response & 0x001FU) << 1U) |
                                      protectedBit);
}

void ServiceDSPIBPeriodicQueries() {
    // The captured application traffic contains several selector-12 reads
    // between each selector-11/selector-7 pair. Preserve the stock 4:1 ratio,
    // but run this fast service often enough that the slow pair still occurs
    // every 2,500 main-loop passes.
    static std::uint8_t fastPass = 0U;
    SendDSPIBBulkDiagnosticQuery();
    ServiceDSPIBModeHeartbeat();
    fastPass = static_cast<std::uint8_t>(fastPass + 1U);
    if ((fastPass & 3U) != 0U) {
        return;
    }

    // The application alternates diagnostic page 0xF and page 0x1. Entering
    // page 0xF also updates the protected six-bit request from the preceding
    // response. With this polling implementation, that delayed selector-11
    // response is clocked out as RX[0] of the immediately following selector-7
    // transaction, not RX[1] of the original two-word transfer. Encoding the
    // stale original word was why the request remained 0001.
    SendDSPIBRotatingDiagnosticAndAdvance();

    static constexpr std::uint16_t statusQuery[] = {
        0x0B01U, 0x0000U, 0x0000U, 0x1FC0U,
    };
    TransferDSPIBMessage(statusQuery, g_dspiBStatusRx);

}

void ServiceDSPIBModeHeartbeat() {
    // E78 clears control bit 10 on periodic service pass 30, restores it on
    // pass 31, and then restarts the count.
    static std::uint8_t serviceCount = 0U;
    static constexpr std::uint16_t heartbeatLow[] = {
        0x0F14U, 0x3A20U,
    };
    static constexpr std::uint16_t heartbeatHigh[] = {
        0x0F14U, 0x3E20U,
    };

    ++serviceCount;
    if (serviceCount == 30U) {
        TransferDSPIBMessage(heartbeatLow, g_dspiBHeartbeatRx);
    } else if (serviceCount >= 31U) {
        TransferDSPIBMessage(heartbeatHigh, g_dspiBHeartbeatRx + 2U);
        serviceCount = 0U;
    }
}

std::uint8_t TransferCompanionByte(std::uint8_t byte, bool keepPCSAsserted) {
    // RFDF is write-one-to-clear. Clear stale receive data indication before
    // starting the next frame, then wait for the matching received frame.
    DSPI_D.SR.R = kDSPIRxFifoDrainFlag;
    DSPI_D.PUSHR.R = kDSPICTAR4 | kDSPIPCS0 |
                     (keepPCSAsserted ? kDSPIContinuousPCS : 0U) | byte;

    while ((DSPI_D.SR.R & kDSPIRxFifoDrainFlag) == 0U) {}
    return static_cast<std::uint8_t>(DSPI_D.POPR.R);
}

std::uint8_t TransferMPMPCS1Byte(std::uint8_t byte,
                                 std::uint32_t ctar,
                                 bool keepPCSAsserted) {
    DSPI_D.SR.R = kDSPIRxFifoDrainFlag;
    DSPI_D.PUSHR.R = ctar | kDSPIPCS1 |
                     (keepPCSAsserted ? kDSPIContinuousPCS : 0U) | byte;

    while ((DSPI_D.SR.R & kDSPIRxFifoDrainFlag) == 0U) {}
    return static_cast<std::uint8_t>(DSPI_D.POPR.R);
}

std::uint8_t ComputeMPMPacketXor(
    const volatile std::uint8_t* packet) {
    std::uint8_t checksum = 0U;
    for (std::size_t i = 0U; i + 1U < kMPMPacketByteCount; ++i) {
        checksum ^= packet[i];
    }
    return checksum;
}

void InitializeMPMPCS1NormalMode() {
    // This discrete is updated by the same application routine that copies
    // the normal/failsafe fields into PCS1 TX bytes 3 and 4. Normal operation
    // drives it high; the application's global failsafe drives it low.
    SIU.GPDO[kMPMNormalModeEnableGPIO].B.PDO = 1U;
    SIU.PCR[kMPMNormalModeEnableGPIO].R = 0x0310U;

    g_mpmPCS1Tx[3] = 0x01U;
    g_mpmPCS1Tx[4] = 0x00U;
    g_mpmPCS1Tx[5] = 0x00U;
    g_mpmPCS1Tx[11] = 0x00U;
}

void ServiceMPMPCS1NormalMode() {
    // The initialized 0x06 header becomes 0x19 on the first exchange, then
    // alternates on every subsequent packet exactly as in the application.
    g_mpmPCS1Tx[0] =
        g_mpmPCS1Tx[0] == 0x19U ? 0x06U : 0x19U;
    g_mpmPCS1Tx[17] = ComputeMPMPacketXor(g_mpmPCS1Tx);

    for (std::size_t i = 0U; i < kMPMPacketByteCount; ++i) {
        const std::uint32_t ctar =
            i == 0U ? kDSPICTAR4 : (i == 1U ? kDSPICTAR5 : kDSPICTAR3);
        g_mpmPCS1Rx[i] = TransferMPMPCS1Byte(
            g_mpmPCS1Tx[i], ctar, i + 1U != kMPMPacketByteCount);
    }
}

void TransferCompanionSixByteMessage(
    const std::uint8_t (&message)[6],
    volatile std::uint8_t* receive) {
    for (std::uint32_t i = 0U; i < 6U; ++i) {
        const std::uint8_t value =
            TransferCompanionByte(message[i], i != 5U);
        if (receive != nullptr) {
            receive[i] = value;
        }
    }
}

void DelayTimebaseTicks(std::uint32_t ticks) {
    std::uint32_t start;
    std::uint32_t now;
    asm volatile("mftb %0" : "=r"(start));
    do {
        asm volatile("mftb %0" : "=r"(now));
    } while (static_cast<std::uint32_t>(now - start) < ticks);
}

void SendApplicationCompanionGroup5Base() {
    static constexpr std::uint8_t message[] = {
        0x6AU, 0x0CU, 0x00U, 0x00U, 0x00U, 0x00U,
    };
    TransferCompanionSixByteMessage(message, g_companionWatchdogRx);
}

void SendApplicationCompanionGroup5Enabled() {
    static constexpr std::uint8_t message[] = {
        0x6AU, 0x2CU, 0x00U, 0x00U, 0x00U, 0x00U,
    };
    TransferCompanionSixByteMessage(message, g_companionWatchdogRx);
}

void SendApplicationCompanionGroup4() {
    static constexpr std::uint8_t message[] = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    };
    TransferCompanionSixByteMessage(message, g_companionOutputRx);
}

void SendApplicationCompanionGroup6(std::uint8_t control) {
    const std::uint8_t message[] = {
        0x80U, control, 0x00U, 0x00U, 0x00U, 0x00U,
    };
    TransferCompanionSixByteMessage(message, g_companionOutputRx);
}

void RunApplicationCompanionStartupHandshake() {
    // Objects 0..4 are Group-6 bits 3..7. Reproduce the application's phase
    // priority while keeping the polling implementation bounded if a damaged
    // or unsupported companion never changes its returned state.
    for (std::uint32_t attempt = 0U; attempt < 8U; ++attempt) {
        const std::uint8_t returned =
            static_cast<std::uint8_t>(g_companionOutputRx[1] & 0xF8U);
        std::uint8_t phaseMask = 0U;
        if ((returned & 0xE0U) != 0U) {
            phaseMask = static_cast<std::uint8_t>(returned & 0xE0U);
        } else if ((returned & 0x08U) != 0U) {
            phaseMask = 0x08U;
        } else if ((returned & 0x10U) != 0U) {
            phaseMask = 0x10U;
        } else {
            break;
        }

        SendApplicationCompanionGroup6(
            static_cast<std::uint8_t>(0xFCU & ~phaseMask));
        SendApplicationCompanionGroup6(0xFCU);
        DelayTimebaseTicks(0x0007D000U);

        // The application samples after the deadline, clears every asserted
        // object in the selected phase, transmits that subset, then restores.
        SendApplicationCompanionGroup6(0xFCU);
        const std::uint8_t asserted =
            static_cast<std::uint8_t>(g_companionOutputRx[1] & phaseMask);
        if (asserted != 0U) {
            SendApplicationCompanionGroup6(
                static_cast<std::uint8_t>(0xFCU & ~asserted));
        }
        SendApplicationCompanionGroup6(0xFCU);
    }

    DelayTimebaseTicks(4000U);
    SendApplicationCompanionGroup6(0xFCU);
}

void ServiceCompanionWatchdog() {
    // This exactly mirrors the transaction called from the bootloader's
    // eMIOS-8 ISR. Its pre-transfer callback complements bits 9..13 in the
    // first word of the bootloader-owned TX buffer at 0x400000F6, then sends
    // the first three bytes as 8-bit CTAR4 frames. Reusing that RAM buffer
    // preserves the rolling phase across the bootloader-to-kernel handoff.
    auto* const messageWord = reinterpret_cast<volatile std::uint16_t*>(
        kBootCompanionTxBufferAddress);
    const std::uint16_t current = *messageWord;
    *messageWord = static_cast<std::uint16_t>(
        ((~((current >> 9U) & 0x1FU) & 0x1FU) << 9U) |
        (current & 0xC1FFU));

    auto* const message = reinterpret_cast<volatile std::uint8_t*>(
        kBootCompanionTxBufferAddress);
    for (std::uint32_t i = 0; i < 6U; ++i) {
        g_companionWatchdogRx[i] =
            TransferCompanionByte(message[i], i != 5U);
    }
}

void SendCompanionOutputConfiguration() {
    // Application Group 6 message with the otherwise separate bit-2 output
    // path enable asserted. Keep the command/header and unused third byte at
    // their application values; the five descriptor-controlled bits 3..7 are
    // all asserted by the 0xFC control byte.
    SendApplicationCompanionGroup6(0xFCU);
}

} // namespace

volatile uint8_t emiosHits = 0;

inline void ServiceCoreWatchdog()
{
    const std::uint32_t watchdogService = 0x40000000U;

    asm volatile(
        "isync\n"
        "mtspr 336, %0\n"
        "isync\n"
        :
        : "r"(watchdogService)
        : "memory"
    );
}

extern "C" void EMIOS_11_Handler()
{
    // CSR.FLAG is write-one-to-clear. Clear the interrupt source before
    // completing the handler and writing INTC.EOIR in the assembly wrapper.
    EMIOS.CH[11].CSR.R = 1U;
    ServiceCoreWatchdog();
    ServiceCompanionWatchdog();
    emiosHits++;
}

MPC5xxxFlexCAN2Service* canService = nullptr;
static volatile FLEXCAN2_tag* canTags[] = { &CAN_A };
static const CANBaudRate canBauds[] = { CANBaudRate::Kbps500 };

extern "C" int main(void) 
{
    // INTC.PSR[kEMIOS11InterruptVector].B.PRI = kEMIOS11InterruptPriority;
    asm("wrteei 0");

    InitializeCompanionDSPI();
    ServiceCoreWatchdog();
    InitializeMPMPCS1NormalMode();

    InitializeDSPIBAsic();

    // Stock application wire order: Group-6 output gate, base Group-5,
    // descriptor-0x157 Group-5, then the response-dependent Group-6 phases.
    SendCompanionOutputConfiguration();
    SendApplicationCompanionGroup5Base();
    SendApplicationCompanionGroup5Enabled();
    RunApplicationCompanionStartupHandshake();

    SendApplicationCompanionGroup5Enabled();
    SendApplicationCompanionGroup4();
    SendApplicationCompanionGroup5Enabled();

    RunCapturedDSPIBPostInitializationSequence();
    InitializeEngineOutputTestGPIOs();

    // Return to the bootloader-compatible rolling watchdog protocol after the
    // one-time application startup sequence.
    InitializeBootloaderCompanionWatchdogBuffer();
    ServiceCompanionWatchdog();
    ServiceMPMPCS1NormalMode();

    canService = new MPC5xxxFlexCAN2Service(canTags, canBauds, 1);
    ICommunicationService* isotpService = canService->GetISOTPService({0x7E0, 0}, {0x7E8, 0});
    isotpService->RegisterReceiveCallBack(HandleDiagnosticRequest);

    std::uint32_t serviceCounter = 0U;
    bool rotatingCaptureReported = false;
    bool engineOutputTestState = false;
    while(true) 
    {
        // Run selector-12 every 625 passes. The service's divide-by-four sends
        // the rotating selector-11 and immediately following selector-7 pair
        // every 2,500 passes, matching the captured interleaving without
        // returning to the original kernel's extremely sparse schedule.
        if (serviceCounter % 625U == 0U) {
            ServiceDSPIBPeriodicQueries();
        }

        if(serviceCounter % 1000000U == 0U)
        {
            engineOutputTestState = !engineOutputTestState;
            SetEngineOutputTestGPIOs(engineOutputTestState);
        }

        if (!rotatingCaptureReported &&
            g_dspiBRotatingCaptureCount == kDSPIBRotatingCaptureCount) {
            // One ISO-TP packet, and no other unsolicited reports:
            //   D2 0A, then ten records of TX0 TX1 RX0 RX1, big-endian.
            std::uint8_t report[2U + kDSPIBRotatingCaptureCount * 8U] = {};
            report[0] = 0xD2U;
            report[1] = static_cast<std::uint8_t>(
                kDSPIBRotatingCaptureCount);
            for (std::size_t record = 0U;
                 record < kDSPIBRotatingCaptureCount; ++record) {
                const DSPIBRotatingCapture& capture =
                    g_dspiBRotatingCaptures[record];
                const std::size_t offset = 2U + record * 8U;
                report[offset + 0U] =
                    static_cast<std::uint8_t>(capture.Tx[0] >> 8U);
                report[offset + 1U] =
                    static_cast<std::uint8_t>(capture.Tx[0]);
                report[offset + 2U] =
                    static_cast<std::uint8_t>(capture.Tx[1] >> 8U);
                report[offset + 3U] =
                    static_cast<std::uint8_t>(capture.Tx[1]);
                report[offset + 4U] =
                    static_cast<std::uint8_t>(capture.Rx[0] >> 8U);
                report[offset + 5U] =
                    static_cast<std::uint8_t>(capture.Rx[0]);
                report[offset + 6U] =
                    static_cast<std::uint8_t>(capture.Rx[1] >> 8U);
                report[offset + 7U] =
                    static_cast<std::uint8_t>(capture.Rx[1]);
            }
            isotpService->Send(report, sizeof(report));
            rotatingCaptureReported = true;
        }
        if(serviceCounter % 10000U == 0U)
        {
            ServiceCoreWatchdog();
            ServiceCompanionWatchdog();
            ServiceMPMPCS1NormalMode();
        }
        canService->PollFlexCAN(CAN_A);
        ++serviceCounter;
    }
    return 0;
}

/*
 Probable function     eTPU channels    GPIO pads      416-BGA balls
  ━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   Fuel injectors 1–8    eTPU-A 18–25     GPIO132–139    H3, H2, H1, G4, G2, G1, F1, G3
  ────────────────────  ───────────────  ─────────────  ────────────────────────────────────────
   Ignition coils 1–8    eTPU-B 20–27     GPIO167–174    A18, B17, C17, D18, A19, B18, C18, A20
*/

/*
The E78 application configures 203 of the 231 SIU-controlled pads. This comes from the 231-entry PCR image at 0xC45DE, installed by FUN_000C4894 and periodically checked by FUN_000C3C20.

  “Output + readback” means both OBE and IBE are enabled: the pin is driven as an output while its physical state can also be read.

  ### External bus interface

   SIU pad(s)    Selected function
  ━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━
   0             EBI CS0
  ────────────  ───────────────────
   9–25          EBI ADDR13–29
  ────────────  ───────────────────
   28–59         EBI DATA0–31
  ────────────  ───────────────────
   62            EBI RD_WR
  ────────────  ───────────────────
   63            EBI BDIP
  ────────────  ───────────────────
   64–67         EBI WE/BE0–3
  ────────────  ───────────────────
   68            EBI OE
  ────────────  ───────────────────
   69            EBI TS

  Notably, CS1–3, ADDR8–12, ADDR30–31, TA, TEA, BR, BG, and BB are configured as GPIO instead.

  ### CAN and serial

   SIU pad    Selected function    Direction
  ━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━
        83    FlexCAN A CNTXA      TX
  ─────────  ───────────────────  ───────────
        84    FlexCAN A CNRXA      RX
  ─────────  ───────────────────  ───────────
        85    FlexCAN B CNTXB      TX
  ─────────  ───────────────────  ───────────
        86    FlexCAN B CNRXB      RX
  ─────────  ───────────────────  ───────────
        92    eSCI B RXDB          Input

  Therefore both CAN A and CAN B are physically muxed. CAN C and CAN D are not: their possible pads are assigned to DSPI or GPIO.

  ### DSPI

   SIU pad    Selected function
  ━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━
        87    DSPI D PCSD3
  ─────────  ───────────────────
        91    DSPI D PCSD1
  ─────────  ───────────────────
        98    DSPI D SCKD
  ─────────  ───────────────────
        99    DSPI D SIND
  ─────────  ───────────────────
       100    DSPI D SOUTD
  ─────────  ───────────────────
       106    DSPI D PCSD0
  ─────────  ───────────────────
        95    DSPI A SOUTA
  ─────────  ───────────────────
       102    DSPI B SCKB
  ─────────  ───────────────────
       103    DSPI B SINB
  ─────────  ───────────────────
       104    DSPI B SOUTB
  ─────────  ───────────────────
       105    DSPI B PCSB0
  ─────────  ───────────────────
       109    DSPI C SCKC
  ─────────  ───────────────────
       110    DSPI C PCSC0

  DSPI D is the most completely routed interface, matching what we have already seen with the companion ASIC. DSPI B is a conventional full-duplex link. DSPI C and A form a transmit-only DSI serial chain: DSPI C supplies SCKC/PCSC0 and the final 21-bit stream exits through DSPI A SOUTA.

  ### eTPU

   SIU pad(s)    Selected eTPU function            Buffer configuration
  ━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━
   130           eTPU A16, dynamically enabled     Output
  ────────────  ────────────────────────────────  ──────────────────────
   131–139       eTPU A17–25                       Input + output
  ────────────  ────────────────────────────────  ──────────────────────
   146           eTPU B TCRCLKB                    Input
  ────────────  ────────────────────────────────  ──────────────────────
   147–157       eTPU B0–10                        Input
  ────────────  ────────────────────────────────  ──────────────────────
   163–165       eTPU B16–18                       Input
  ────────────  ────────────────────────────────  ──────────────────────
   166–174       eTPU B19–27                       Input + output
  ────────────  ────────────────────────────────  ──────────────────────
   175           eTPU B28                          Input
  ────────────  ────────────────────────────────  ──────────────────────
   160           eTPU B29, dynamically selected    Input + output
  ────────────  ────────────────────────────────  ──────────────────────
   177–178       eTPU B30–31                       Input

  This confirms the ignition pins:

  - SIU pads 167–174 are eTPU B20–27.
  - They are not being operated as ordinary GPIO despite commonly being called “GPIO167–174.”
  - Pad 166/eTPU B19 is another fully enabled eTPU output immediately adjacent to them.
  - eTPU A17–25 on pads 131–139 is another bank of nine fully enabled timing outputs.

  The two runtime mux changes are:

  - Pad 130: GPIO input → eTPU A16 output using descriptor 0x820898.
  - Pad 160: GPIO → eTPU B29 using 0xA008D1, then restored using 0xA008C1.

  ### eMIOS

   SIU pad    Selected function    Configuration
  ━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━
       185    eMIOS6               Input + output
  ─────────  ───────────────────  ────────────────
       190    eMIOS11              Input
  ─────────  ───────────────────  ────────────────
       196    eMIOS17              Input + output
  ─────────  ───────────────────  ────────────────
       199    eMIOS20              Input + output
  ─────────  ───────────────────  ────────────────
       200    eMIOS21              Input
  ─────────  ───────────────────  ────────────────
       201    eMIOS22              Input + output
  ─────────  ───────────────────  ────────────────
       202    eMIOS23              Input + output

  eMIOS8’s external pad would be SIU187, but SIU187 is configured as GPIO. The bootloader can still use eMIOS8 internally as a timer/interrupt without exposing the channel on a pin.

  ### eQADC external-multiplexer controls

   SIU pad    Function
  ━━━━━━━━━  ━━━━━━━━━━
       215    MA0
  ─────────  ──────────
       216    MA1
  ─────────  ──────────
       217    MA2

  These are the three address outputs for an external analog multiplexer.

  Pad 218 is set to an invalid/disabled mux selection rather than FCK or AN15, so I do not count it as used.

  ### Direct GPIO

  Inputs only:

  4–7, 72, 89, 93, 96, 107, 115–118, 121–123, 130, 186, 203–204

  GPIO4–7 are the hardware board-revision straps.

  Outputs only:

  1, 73, 101, 114, 119, 205

  Outputs with input readback:

  2–3, 8, 27, 60–61, 70–71, 74, 88, 90, 94, 97, 108, 111–113, 120, 124–129, 140–144, 158–162, 176, 179–184, 187–189, 191–195, 197–198, 206–207

  Initial states:

  - GPIO205 starts high.
  - Every other GPIO with its output buffer enabled starts low.
  - Depending on board revision, either GPIO89 or GPIO186 is changed from input to an output driven low.
  - GPIO191 is the relay/control path we traced previously, but the SIU alone does not prove its external schematic name.

  ### Clock outputs

   SIU pad    Function
  ━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━
       214    ENGCLK, enabled output
  ─────────  ────────────────────────
       229    CLKOUT, enabled output

  ### Pads not configured as application I/O

  26, 75–82, 145, 208–213, 218–228, 230

  Pads 219–228 are Nexus/debug drive-strength registers. Their PCR values do not establish that Nexus is actively being driven.

  This list does not include fixed-function pins such as XTAL/EXTAL, reset, JTAG, power, or the dedicated analog inputs. Usage of the dedicated ADC channels has to be recovered from the
  eQADC command queues, not from ordinary SIU PCR muxing. The mux definitions were checked against the /home/daniel/Documents/Manuals/CAN/15160_MPC5566RM.pdf.

*/
