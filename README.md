# MPC5566 E78 RAM Kernel

This project builds a small Book E kernel for the MPC5566 used in the E78 ECU. It is loaded through the stock bootloader, runs entirely from SRAM, and provides UDS services for reading memory and erasing/programming internal flash.

The kernel is volatile. Resetting or power-cycling the ECU returns control to the firmware stored in flash.

While running, the kernel services both the MPC core watchdog and the ON20845-007 companion watchdog.

## Safety

- Use a stable, current-limited power supply.
- Do not reset or remove power during flash erase or programming.
- Keep a known-good 3 MiB image and a working BAM recovery method available. Be sure to read out the Shadow Password beforehand.
- Erase and program complete physical flash blocks. Flash cannot safely be treated as arbitrary byte-addressable storage.

## Build

Requirements:

- CMake 4.0 or newer
- GNU Make
- NXP's `powerpc-eabivle` GCC toolchain in `PATH`
- Git/network access the first time CMake fetches `EmbeddedIOServices`

Example using the S32 Design Studio toolchain:

```bash
export PATH="/home/daniel/NXP/S32DS_Power_v2.1/S32DS/build_tools/powerpc-eabivle-4_9/bin:$PATH"
cd /home/daniel/git/KernelMPC5566
cmake --preset MPC5566-Release
cmake --build build/MPC5566-Release -j"$(nproc)"
```

Build outputs:

| File | Purpose |
| --- | --- |
| `build/Kernel-E78.bin` | Raw binary to load into SRAM |
| `build/Kernel-E78.hex` | Intel HEX representation |
| `build/MPC5566-Release/firmware.elf` | ELF with symbols for debugging |

The binary is linked for `0x40014000` and must fit inside the 48 KiB `SRAM_CODE` region defined by `MPC5566_RAM.ld`. It is not position independent and must not be loaded at a different address.

## CAN and ISO-TP configuration

| Setting | Value |
| --- | --- |
| CAN peripheral | FlexCAN A |
| CAN bit rate | 500 kbit/s |
| Request CAN ID | `0x7E0` |
| Response CAN ID | `0x7E8` |
| Transport | ISO-TP |
| Kernel load/entry address | `0x40014000` |

All multibyte UDS fields described below are big-endian.

## Loading and executing the kernel

The kernel does not contain its own loader. A host must use the stock E78 application and bootloader to place `Kernel-E78.bin` in SRAM and branch to it.

The known working sequence is:

1. Establish communication with the stock application on CAN A.
2. Perform the stock security unlock.
3. Optionally suppress normal broadcast traffic.
4. Send the application-to-bootloader programming sequence:

   ```text
   10 02
   A5 01
   A5 03
   ```

5. Use the stock bootloader's RAM download protocol to write every byte of `Kernel-E78.bin`, beginning at `0x40014000`.
6. Execute address `0x40014000` with the stock bootloader command:

   ```text
   36 80 40 01 40 00
   ```

The currently used stock RAM-download framing is:

```text
34 00 <encoded transfer length:3>
36 00 <destination address:4> <data>
...
36 80 40 01 40 00
```

The encoded transfer length passed to `34` includes the six-byte `36 00 <address>` header for each data chunk. The stock loader accepts chunks with up to 2042 data bytes. Existing working loaders send the final partial chunk first and then the full chunks in descending-address order.

After startup, the kernel sends one unsolicited ISO-TP message:

```text
99
```

Treat `99` as the ready indication. Do not begin flash operations until it is received.

## Supported memory regions

| Region | Read | Write | Notes |
| --- | --- | --- | --- |
| `0x00000000`–`0x002FFFFF` | Yes, except the holes below | Yes | MPC5566 internal flash |
| `0x40000000`–`0x4001FFFF` | Yes | Yes | SRAM; no erase required |
| All other addresses | No | No | Rejected with NRC `31` |

The following flash addresses are excluded from reads because they are not backed by valid ECC data:

- `0x00003FE0`–`0x00003FFF`
- `0x0001FFE0`–`0x0001FFFF`

A request may not cross from one configured region into another. Therefore, reading all flash requires separate requests around those holes.

## Reading memory

### Short reads with ReadMemoryByAddress

Service `0x23` uses a standard address-and-length format:

```text
Request:  23 <ALFID> <address> <length>
Response: 63 <data>
```

The low nibble of ALFID is the address length and the high nibble is the size length. Both may be one through four bytes.

Example: read 16 bytes at `0x00080000` using four-byte address and length fields:

```text
Request:  23 44 00 08 00 00 00 00 00 10
Response: 63 <16 data bytes>
```

A single `0x23` response can contain at most 4094 data bytes.

### Streaming reads with RequestUpload

Use RequestUpload for larger ranges:

```text
35 <format> <ALFID> <address> <length>
```

Supported data formats:

| Format | Meaning |
| --- | --- |
| `00` | Uncompressed |
| `10` | Independent-block LZ4 |

Example: begin an LZ4-compressed read at `0x00080000`:

```text
Request:  35 10 44 00 08 00 00 <length:4>
Response: 75 20 0F FF
```

Request each data block with an incrementing sequence counter:

```text
Request:  36 <sequence>
Response: 76 <sequence> <decoded length:2> <LZ4 block>
```

For an uncompressed upload, the response is:

```text
76 <sequence> <raw data>
```

Sequence counters begin at `01`, increment modulo 256, and may wrap to `00`. Repeating the immediately previous counter returns the same response, allowing a lost response to be retried safely.

After receiving the requested number of decoded bytes:

```text
Request:  37
Response: 77
```

## Flash-block inventory and hashes

Routine `FF00` reports the MPC5566 physical block map and the current SHA-256 hash of every block.

Send one request:

```text
31 01 FF 00
```

The kernel streams 28 separate ISO-TP records:

```text
71 01 FF 00 1C <block ID> <address:4> <length:4> <SHA-256:32>
```

`1C` is the total block count. The block ID is zero based.

Hashing occurs in small background slices so watchdog servicing continues. The kernel may send this response while an uncached hash is being calculated:

```text
7F 31 78
```

Continue waiting after response-pending. The block records follow in ID order.

Hashes are cached. An erase invalidates the corresponding hash. The kernel schedules a new background hash only after the complete block has been rewritten. If inventory is requested before then, that block is hashed on demand.

## Physical flash-block map

| Block ID | Start address | Length |
| ---: | ---: | ---: |
| 0 | `0x00000000` | `0x00004000` |
| 1 | `0x00004000` | `0x0000C000` |
| 2 | `0x00010000` | `0x0000C000` |
| 3 | `0x0001C000` | `0x00004000` |
| 4 | `0x00020000` | `0x00010000` |
| 5 | `0x00030000` | `0x00010000` |
| 6 | `0x00040000` | `0x00020000` |
| 7 | `0x00060000` | `0x00020000` |
| 8–27 | `0x00080000`–`0x002E0000` | `0x00020000` each |

For IDs 8 through 27:

```text
address = 0x00080000 + (block ID - 8) * 0x00020000
```

The application area begins at block 8 (`0x00080000`). Blocks 0 through 7 contain the stock bootloader and its associated data/code.

## Erasing flash

Erase by physical block ID:

```text
Request:  31 01 FF 01 <block ID>
Response: 71 01 FF 01 <block ID> <status>
```

Status values:

| Status | Meaning |
| --- | --- |
| `00` | Queued |
| `01` | Running |
| `02` | Successful |
| `03` | Failed |

Status updates are separate ISO-TP messages. A client should continue accepting updates until it sees successful or failed for that block.

The erase queue can hold 28 requests. Erases and writes receive a shared operation sequence when accepted, so they execute in the same order they were submitted.

## Writing flash

The intended flash update sequence for each changed block is:

1. Obtain the block map and SHA-256 hashes with routine `FF00`.
2. Compare the desired complete block image against the reported hash.
3. Skip the block if its hash matches.
4. Queue its erase with routine `FF01`.
5. Immediately open a RequestDownload for that same block.
6. Send TransferData chunks until the complete block image has been queued.
7. Retry TransferExit until programming finishes.

It is not necessary to wait for erase-success before opening the download. The operation queue preserves erase-before-write ordering.

### RequestDownload

Begin an LZ4-compressed download:

```text
Request:  34 10 44 <address:4> <length:4>
Response: 74 20 0F FA
```

Use format `00` instead of `10` for uncompressed data.

For internal flash:

- The download address and total length must be 8-byte aligned.
- Each decoded TransferData chunk must be 8-byte aligned in length.
- A queued write must not cross a physical flash-block boundary.
- The safest and intended transfer size is one complete erased block.

### Compressed TransferData

```text
Request:  36 <sequence> <decoded length:2> <LZ4 block>
Response: 76 <sequence>
```

The maximum decoded LZ4 block is 4096 bytes. Each block is compressed independently; no dictionary is carried between TransferData messages.

### Uncompressed TransferData

```text
Request:  36 <sequence> <raw data>
Response: 76 <sequence>
```

Sequence counters begin at `01` and increment modulo 256. Repeating the immediately previous accepted sequence counter is idempotent and returns `76` again.

The flash writer owns one active and one waiting program buffer. When both are occupied, the kernel responds:

```text
7F 36 21
```

Retry the exact same sequence counter and payload. Do not increment the counter until `76 <sequence>` is received.

`76` means the chunk was accepted into the program queue. It does not mean the bytes have already reached flash.

### Completing the download

After all bytes have received `76` acknowledgements:

```text
Request:  37
```

If writes are still running:

```text
Response: 7F 37 21
```

Retry `37` until the final response arrives:

```text
Response: 77
```

Do not reset the ECU before receiving `77`.

## Writing SRAM

RequestDownload also supports SRAM at `0x40000000`–`0x4001FFFF`. SRAM does not require an erase and does not have the 8-byte flash alignment restriction.

Use the same `34`/`36`/`37` sequence. Data is copied directly when its TransferData request is handled.

## Other supported UDS services

| Request | Response | Purpose |
| --- | --- | --- |
| `27 01` | `67 01 00 00` | Stub security-access seed used by the current tooling |
| `10 02` | `50` | Exit the RAM kernel through the stock bootloader upload-entry path |

After `10 02`, assume the kernel is no longer running and repeat the complete load/execute procedure before sending more kernel UDS requests.

## Negative response codes

| NRC | Meaning in this kernel |
| --- | --- |
| `11` | Service not supported |
| `12` | Subfunction not supported |
| `13` | Incorrect request length or format |
| `21` | Busy; retry the same request |
| `22` | Required callback/condition is unavailable |
| `24` | Transfer sequence is not active or not complete |
| `31` | Address, length, format, alignment, routine, or block ID is out of range |
| `71` | TransferData would exceed the requested transfer size |
| `72` | Erase, programming, hashing, or LZ4 operation failed |
| `73` | Wrong TransferData sequence counter |
| `78` | Response pending; operation is still active |

## Recovery behavior

If communication is lost during an update, keep power applied until the current flash operation has had time to finish. Reload the RAM kernel if necessary, request the block inventory again, and resend the complete affected block. Hash comparison can then identify blocks that already match the intended image.

A normal reset or power cycle exits the kernel and starts whatever firmware remains in flash.
