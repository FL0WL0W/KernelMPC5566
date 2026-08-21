# E78 DSPI-D PCS1 MPM protocol

This document treats the device selected by DSPI-D PCS1/GPIO91 as the MPM.
The device identity is a working hardware assumption; the packet construction
and code paths below are directly established by `E78.bin`.

## Physical transfer

- DSPI-D PCS1, SIU pad/GPIO91.
- PCS mask `0x00020000` from the configuration record at `0xB994C`.
- Eighteen 8-bit full-duplex transfers per packet.
- Byte 0 uses CTAR4 (`0x3AEC3C09`).
- Byte 1 uses CTAR5 (`0x3ADC3B79`).
- Bytes 2 through 17 use CTAR3 (`0x3AFC3879`).
- The packet is exchanged at scheduler phase 4, once per ten invocations of
  `ServiceMPMCyclicScheduler`.

## Transmit packet

The working packet is at `0x400301BA`; immediately before a transfer all 18
bytes are copied to the DSPI-D DMA buffer at `0x40009E04`.

| Byte | Initial value | Runtime source | Code-supported interpretation |
|---:|---:|---|---|
| 0 | `06` | Alternated `06`/`19` before every transfer | Sequence/frame type |
| 1 | `FF` | Never modified | Fixed protocol value |
| 2 | `FF` | Never modified | Fixed protocol value |
| 3 | `00`, then normally `01` | MPM control-state byte A | Normal/run-state flag |
| 4 | `00`, normally `00`; failsafe sets `01` | MPM control-state byte B | Complementary failsafe/state flag |
| 5 | `00` | Set to `01` when the latched-request source is asserted | Latched request/notification |
| 6 | `00` | Never modified | Reserved/fixed zero |
| 7 | `00` | Never modified | Reserved/fixed zero |
| 8 | `00` | Never modified | Reserved/fixed zero |
| 9 | `00` | Never modified | Reserved/fixed zero |
| 10 | `00` | Never modified | Reserved/fixed zero |
| 11 | `00` | Clock/phase calibration state machine | MPM clock-trim command |
| 12 | `FF` | Never modified | Fixed protocol value |
| 13 | `53` | Never modified | Fixed identification/configuration value |
| 14 | `10` | Never modified | Fixed identification/configuration value |
| 15 | `00` | Never modified | Fixed protocol value |
| 16 | `09` | Never modified | Fixed identification/configuration value |
| 17 | calculated | XOR of bytes 0 through 16 | Packet checksum |

The initialized template is:

```text
06 FF FF 00 00 00 00 00 00 00 00 00 FF 53 10 00 09 B3
```

Before normal operation's first transfer, the control-state updater normally
changes byte 3 to `01`, and the exchange routine flips byte 0 from `06` to
`19`.  With byte 5 and clock trim still zero, that on-wire packet is:

```text
19 FF FF 01 00 00 00 00 00 00 00 00 FF 53 10 00 09 AD
```

The alternate sequence packet has byte 0 `06` and checksum `B2`.

When the application's global failsafe/startup-invalid flag is set, its source
control state is forced from byte pair `01 00` to `00 01` before being copied
into bytes 3 and 4.

## Byte 11 clock-trim loop

The calibration code treats receive bytes 7:8 and 9:10 as two big-endian
16-bit counters. It calculates:

```text
counter_a = (rx[7] << 8) | rx[8]
counter_b = (rx[9] << 8) | rx[10]
if counter_a < counter_b:
    counter_a += 0x0C36
delta = counter_a - counter_b
```

The target delta is `500`. The state machine adjusts transmit byte 11 upward
or downward one count at a time and observes receive byte 11 as trim feedback.
It also uses special command values including `00`, `80`, and `FF`, and accepts
externally requested trim values in the range `02` through `FD`.

## Receive packet

The completed DMA response is stored at `0x40009E16`. At the next scheduler
phase-4 call it is copied into `0x400301A8`; therefore processing has one
transaction of latency.

| Byte | Known consumer |
|---:|---|
| 0 | No resolved direct consumer beyond the packet checksum |
| 1 | No resolved direct consumer |
| 2 | Exported as a diagnostic/status byte |
| 3-5 | No resolved direct consumers |
| 6 | Cached by the calibration/status service |
| 7-8 | Big-endian calibration counter A |
| 9-10 | Big-endian calibration counter B |
| 11 | Clock-trim feedback |
| 12 | Exported as an MPM status byte |
| 13-14 | Exported as two status bytes |
| 15 | Exported as a status byte |
| 16 | State flags; bits 3:2 are tested for value `2` (`(rx[16] & 0x0C) == 0x08`) |
| 17 | XOR checksum for receive bytes 0 through 16 |

A receive-checksum mismatch sets bit 6 in the application's MPM status word.

## Principal firmware functions

| Address | Name | Role |
|---:|---|---|
| `0xD5B24` | `InitializeMPMRuntime` | Initializes packet state and the MPM scheduler subsystem |
| `0xD7FE8` | `InitializeDSPIDPCS1Packet` | Builds the initial transmit template |
| `0xD73C8` | `InitializeMPMControlState` | Initializes the sources copied into bytes 3-5 |
| `0xD6F00` | `UpdateMPMTransmitControlFields` | Copies the current state into bytes 3-5 |
| `0xD80A4` | `ExchangeDSPIDPCS1CyclicPacket` | Toggles byte 0, checksums, copies DMA buffers, and queues the packet |
| `0xD7FC0` | `ComputeDSPIDPCS1PacketXorChecksum` | XORs bytes 0 through 16 |
| `0xD8B28` | `ServiceMPMClockTrimCalibration` | Processes counters and updates byte 11 |
| `0xB260C` | `QueueDSPIDPCS1Packet` | Submits the 18-byte DSPI-D PCS1 transaction |

## Kernel bench reproduction

`src/main.cpp` now reproduces the normal-mode cyclic exchange directly,
without calling application flash:

- explicitly muxes GPIO91 as DSPI-D PCS1 (`PCR=0x0A04`);
- drives GPIO182 high using its stock bidirectional GPIO configuration
  (`PCR=0x0310`);
- alternates the `19 ... AD` and `06 ... B2` normal-mode packets;
- uses CTAR4, CTAR5, then CTAR3 for bytes 0, 1, and 2-17 respectively;
- retains all received bytes in `g_mpmPCS1Rx`;
- periodically reports `D1`, a checksum-valid byte, and the 18 raw RX bytes
  over ISO-TP.

## Relationship to MPC5566 shadow flash

The MPC5566 shadow-flash window is `0x00FFFC00` through `0x00FFFFFF`
(1024 bytes).  It contains nonvolatile device security/configuration data;
notably, the 64-bit password starts at `0x00FFFDD8` and the censorship control
word is at `0x00FFFDE0`.

No resolved E78 application instruction directly references the shadow base,
password, or censorship-control addresses.  More importantly, no shadow-flash
read or copy reaches the PCS1 transmit packet.  Its fixed fields are written as
immediate constants by `InitializeDSPIDPCS1Packet`, while its dynamic fields
come from runtime RAM state and the MPM clock-trim service.

The byte pattern `00 FF FC 00` occurs at ROM locations `0xD2F80` and
`0xD2F98`, but both occurrences are embedded in an opaque, currently
unreferenced data table.  There is no code cross-reference proving that either
occurrence is used as a CPU address, so they must not be treated as evidence of
a shadow-flash read.

The BAM can consume password/security configuration from shadow flash
internally during its boot/security protocol.  That hardware/BAM behavior is
separate from the application code and from the DSPI-D PCS1 packet builder.
Inspecting the actual factory shadow contents would require a shadow-flash dump
to be imported as its own Ghidra memory block.
