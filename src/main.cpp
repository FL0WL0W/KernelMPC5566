#include "MPC5xxxFlexCAN2Service.h"
#include "LZ4.h"

using namespace EmbeddedIOServices;
using namespace MPC5xxx;

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
constexpr std::uint32_t kDSPIPCS0 = 0x00010000U;
constexpr std::uint32_t kDSPICTAR1 = 0x10000000U;
constexpr std::uint32_t kDSPIContinuousPCS = 0x80000000U;
constexpr std::uint32_t kDSPIRxFifoDrainFlag = 0x00020000U;

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
constexpr std::uint32_t kBrokenECCFlashRegionLength = 8U;
constexpr std::uint32_t kBrokenECCFlashRegion1 = 0x0001FFF8U;
constexpr std::uint32_t kBrokenECCFlashRegion2 = 0x0002FFF8U;

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
    DSPI_D.CTAR[3].R = 0x3ADC3B79U;
    DSPI_D.CTAR[4].R = 0x3AEC3C09U;
    DSPI_D.SR.R = 0x90020000U;
}

std::uint16_t TransferCompanionWord(std::uint16_t word, bool keepPCSAsserted) {
    // RFDF is write-one-to-clear. Clear stale receive data indication before
    // starting the next frame, then wait for the matching received frame.
    DSPI_D.SR.R = kDSPIRxFifoDrainFlag;
    DSPI_D.PUSHR.R = kDSPICTAR1 | kDSPIPCS0 |
                     (keepPCSAsserted ? kDSPIContinuousPCS : 0U) | word;

    while ((DSPI_D.SR.R & kDSPIRxFifoDrainFlag) == 0U) {}
    return static_cast<std::uint16_t>(DSPI_D.POPR.R);
}

void ServiceCompanionWatchdog() {
    // Same two three-word commands emitted by the bootloader. PCS0 remains
    // asserted within each command and is released between the commands.
    static constexpr std::uint16_t message[] = {
        0x6AA4U, 0xA1F0U, 0x0000U,
        0x6944U, 0xA1F0U, 0x0000U,
    };

    volatile std::uint16_t response;
    for (std::uint32_t i = 0; i < 6U; ++i) {
        response = TransferCompanionWord(message[i], (i % 3U) != 2U);
    }
    (void)response;
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
    // InitializeCompanionDSPI();
    // INTC.PSR[kEMIOS11InterruptVector].B.PRI = kEMIOS11InterruptPriority;
    // asm("wrteei 1");
    canService = new MPC5xxxFlexCAN2Service(canTags, canBauds, 1);
    canService->Send({0x7E8, 0}, {{0x01, 0x99}}, 2);
    ICommunicationService* isotpService = canService->GetISOTPService({0x7E0, 0}, {0x7E8, 0});
    isotpService->RegisterReceiveCallBack(HandleDiagnosticRequest);
    while(true) 
    {
        canService->PollFlexCAN(CAN_A);
    }
    return 0;
}