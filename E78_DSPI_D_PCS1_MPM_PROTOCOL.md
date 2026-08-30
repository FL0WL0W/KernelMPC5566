# E78 DSPI-D PCS1 MPM protocol

This document is a code-derived map of every resolved read and write on the
E78 application's DSPI-D PCS1 link.  The external device is called the MPM
here because that matches the board-level identification used during the
reverse-engineering work.  The electrical link, packet construction, parser,
checksums, and consumers below are established directly by `E78.bin`.
Semantic names such as *normal mode*, *remote analog*, and *event* are
inferences from how the firmware uses the fields; confidence is called out
where the code does not reveal the external signal name.

## Most important result

PCS1 does not carry one simple 18-byte status packet.  Two logical protocols
are interleaved through the same 18-byte DMA transport:

1. A cyclic `06`/`19` service frame maintains the MPM state, normal/failsafe
   request, application version, clock trim, counters, and state acknowledgments.
2. A 16-byte command/status overlay lets the MPM send bit commands, retained
   record updates, and eight optional remote-analog payload bytes.  The ECM
   returns status bitmaps and optional multiplexed analog feedback.  Bytes
   16-17 remain the physical transport footer/state area.  E78's calibration
   disables the optional remote-analog feature, although the shared code is
   present.

The second path is why most of the MPM response initially appeared unused.
It reads the DMA-completion snapshot through indirect accessor functions at
`0xB2560`/`0xB271C`, well outside the cyclic-frame code at `0xD5xxx`.

No resolved MPM receive bit directly writes the injector/ignition enable,
eTPU enable, or C2MIO FSE pin.  The MPM does affect normal/failsafe state,
retained event/status records, remote analog channels, clock calibration, and
OBD Service $09 calibration-identification/CVN records.

## Physical transport

No PCS1 transaction was resolved in the bootloader region below `0x80000`.
The bootloader's DSPI-D companion traffic uses PCS0; the PCS1/MPM protocol
described here belongs to the application firmware.

| Property | Value |
|---|---|
| MCU module | DSPI-D |
| Chip select | PCS1 |
| SIU pad/GPIO | GPIO91, `PCR=0x0A04` |
| PCS descriptor | `0xB994C`, PCS mask `0x00020000` |
| Direction | 18-byte full-duplex |
| Frame size | 8 bits |
| Bit order | MSB first |
| SPI mode | CPOL=0, CPHA=1 (mode 1) |
| PCS behavior | CONT set for the packet's frame descriptors; one packet transaction |
| DMA TX | `0x40009E04..0x40009E15` |
| DMA RX | `0x40009E16..0x40009E27` |
| completed RX snapshot | `0x4000B148..0x4000B159` |
| cyclic working RX | `0x400301A8..0x400301B9` |
| cyclic working TX | `0x400301BA..0x400301CB` |

The transaction descriptor at `0xB8244` fixes the physical count at 18 bytes.
The CTAR selection table at `0xB8190` assigns:

| Physical byte | CTAR | Raw value |
|---:|---:|---:|
| 0 | CTAR4 | `0x3AEC3C09` |
| 1 | CTAR5 | `0x3ADC3B79` |
| 2-17 | CTAR3 | `0x3AFC3879` |

All three CTARs select PBR=2, BR=512, DBR=0.  Therefore:

```text
SCK = fSYS / (2 * 512)
    = 250 kHz when fSYS = 256 MHz
```

At 256 MHz, the programmed timing fields decode as follows:

| CTAR | PCS-to-SCK | after-SCK | delay-after-transfer |
|---:|---:|---:|---:|
| 3 | 112 clocks = 0.4375 us | 3584 clocks = 14 us | 1792 clocks = 7 us |
| 4 | 112 clocks = 0.4375 us | 40960 clocks = 160 us | 14 clocks = 0.0547 us |
| 5 | 112 clocks = 0.4375 us | 12288 clocks = 48 us | 1792 clocks = 7 us |

The exceptionally long byte-0 and byte-1 timing is intentional protocol
framing, not an incidental SPI setting.

## Transport ownership and ordering

`QueueDSPIDPCS1Packet` at `0xB260C` is the only resolved low-level submitter.
Its callers are:

- `ExchangeDSPIDPCS1CyclicPacket` at `0xD80A4`;
- `RetryDSPIDPCS1PacketUnlessValidHeader` at `0xB26C4`.

The second call is the command/status overlay path.  It queues the DMA buffer
only when TX byte 0 is neither `06` nor `19`; this prevents it from re-queuing
a cyclic service frame that has just overwritten the shared DMA buffer.

On DMA completion, `ValidateDSPIDPCS1DmaReceiveChecksum` at `0x82878` checks
the outer XOR, then `CompleteDSPIDPCS1DmaTransfer` at `0xB25C8` changes the
transport state from active (`2`) to completed (`1`) and, if no replacement
request is pending, copies all 18 RX bytes to the snapshot at `0x4000B148`.

The cyclic exchange has one-transaction latency:

1. consume the previous DMA RX into cyclic working RX;
2. build/copy the next cyclic TX into DMA TX;
3. process the old RX;
4. queue the new exchange.

The command parser consumes the separate completion snapshot, so the two
logical clients can coexist without both parsing the same live DMA buffer.

## Layer 1: cyclic `06`/`19` service frame

### TX format

`InitializeDSPIDPCS1Packet` at `0xD7FE8` builds this 18-byte template:

```text
06 FF FF 00 00 00 00 00 00 00 00 00 FF 53 10 00 09 B3
```

The first normal-mode exchange toggles byte 0 to `19`, sets byte 3 to `01`,
and rebuilds byte 17:

```text
19 FF FF 01 00 00 00 00 00 00 00 00 FF 53 10 00 09 AD
```

The alternating `06` normal-mode form is:

```text
06 FF FF 01 00 00 00 00 00 00 00 00 FF 53 10 00 09 B2
```

| Byte | Normal value | Writer/source | Code-supported meaning |
|---:|---:|---|---|
| 0 | `06` / `19` | toggled before every cyclic exchange | two-state alive/frame sequence |
| 1 | `FF` | initialization only | fixed protocol value; unknown |
| 2 | `FF` | initialization only | fixed protocol value; unknown |
| 3 | `01` | normal/failsafe state updater | normal/run-state request |
| 4 | `00` | normal/failsafe state updater | failsafe-state request |
| 5 | `00` | sticky request updater | persistent request/event; no internal setter found |
| 6-10 | `00` | initialization only | reserved/fixed zero in E78 |
| 11 | `00` normally | MPM clock-trim state machine | clock-trim command/value |
| 12 | `FF` | initialization only | fixed protocol value; unknown |
| 13 | `53` | packed from ROM `0xB5470[0:1]` | application version nibbles 5.3 |
| 14 | `10` | packed from ROM `0xB5470[2:3]` | application version nibbles 1.0 |
| 15 | `00` | initialization only | fixed protocol value; unknown |
| 16 | `09` | initialization only after an early `01` preseed | two 2-bit state requests; see below |
| 17 | dynamic | XOR of bytes 0-16 | outer packet checksum |

The four ROM bytes at `0xB5470` are `05 03 01 00`, immediately followed by
the Delphi copyright string.  A second firmware routine copies the same four
bytes into a runtime version record.  Therefore the `53 10` interpretation as
firmware version 5.3.1.0 is high confidence.

### Normal/failsafe transition

`ServiceMPMControlStateAndOutputs` at `0xD74F4` and
`UpdateMPMTransmitControlFields` at `0xD6F00` establish the exact behavior:

| Application state | GPIO182 | TX[3] | TX[4] |
|---|---:|---:|---:|
| normal (`0x400069E0 == 0`) | source state, initialized high | `01` | `00` |
| global failsafe (`0x400069E0 != 0`) | low | `00` | `01` |

GPIO182 is therefore a discrete hardware companion to the packet's
normal/failsafe request.  Calling it an MPM normal-mode/failsafe line is high
confidence; the actual schematic net name is not present in the binary.

TX[5] is sticky.  If source byte `0x40030354` is nonzero, the updater writes
TX[5]=1 and clears the source.  No firmware instruction sets that source byte
to one, and no firmware instruction clears TX[5] after it is set.  It may be
reserved for calibration/tool injection or for code absent from this image.

### TX[16] and RX[16] state fields

The firmware exposes two independent 2-bit state fields:

| Byte 16 bits | TX value | RX use |
|---|---:|---|
| 1:0 | `1` | completed PCS1 command packet is accepted only if RX value is `2` in runtime mode 0 |
| 3:2 | `2` after early startup | clock-trim state machine waits for RX value `2` |
| 7:4 | `0` | no resolved consumer |

Early application startup preseeds raw DMA TX[16].bits1:0 to `1` and
TX[16].bits3:2 to `0`.  The cyclic working template later uses `09`, which is
field values `1` and `2`.  This is evidence that byte 16 is a pair of state
requests, not an arbitrary fixed identification byte.

`GetDSPIDPCS1CompletionAndAckState` at `0xB256C` enforces the low field.  If a
DMA exchange is complete but raw RX[16].bits1:0 is not `2`, it discards the
completed state before the high-level MPM command parser can run.

### RX format as seen by cyclic processing

| Byte | Cyclic consumer |
|---:|---|
| 0 | outer XOR only |
| 1 | outer XOR only |
| 2 | sampled into the phase-1 board/diagnostic snapshot |
| 3-5 | no cyclic consumer; used by command overlay |
| 6 | copied to clock-measurement diagnostic status |
| 7-8 | big-endian clock counter A |
| 9-10 | big-endian clock counter B |
| 11 | MPM clock-trim feedback |
| 12 | exported status snapshot |
| 13-15 | exported status snapshots |
| 16 | state fields described above |
| 17 | outer XOR checksum |

Bytes 2 and 12-15 are exported/snapshotted, but no direct application
decision based on their individual bits was found.  That does not prove that
the MPM regards them as reserved; it means this E78 image does not interpret
their bits internally.

### Outer XOR

The cyclic packet checksum is:

```text
xor = byte[0] XOR byte[1] ... XOR byte[16]
byte[17] = xor
```

It is checked twice on receive:

1. the DMA-completion callback stores a boolean at `0x40009132`;
2. the next cyclic exchange sets bit 6 of application status byte
   `0x40030340` on mismatch.

No reader of the DMA callback's boolean exists.  The cyclic mismatch bit is a
reported diagnostic; it does not suppress parsing or force failsafe in this
image.

## MPM clock-trim subprotocol

`ServiceMPMClockTrimCalibration` at `0xD8B28` interprets:

```text
counter_a = (RX[7] << 8) | RX[8]
counter_b = (RX[9] << 8) | RX[10]
if counter_a < counter_b:
    counter_a += 0x0C36
delta = counter_a - counter_b
target = 500
```

TX[11] carries the requested trim/control value; RX[11] is feedback.  The
firmware uses these special values:

| TX[11] | Observed role |
|---:|---|
| `00` | normal/no-trim command |
| `80` | enter/reset trim handshake and wait for RX[16].bits3:2=`2`, RX[11]=`80` |
| `FF` | calibration completion/special terminal command |
| `02..FD` | bounded requested trim value |
| RX[11] or RX[11]+/-1 | closed-loop trim adjustment |

The automatic calibration walks one count at a time toward delta 500.  It
logs samples, averages ten-sample windows, applies a narrow acceptance window,
then a final validation window.  GPIO/eTPU timing hardware is configured by
`InitializeMPMClockMeasurementHardware` at `0xD8738`; the service also controls
the GPIO160/eTPU timing path.  This is a clock/phase calibration handshake,
not an output-enable command.

## Layer 2: MPM command/status overlay

The command overlay operates on bytes 0-15 of the same 18-byte physical DMA
packet.  Its header is distinguished from cyclic `06`/`19` frames.  Bytes
16-17 remain available to the physical state/footer mechanism.

### Sequence protocol

There are three sequence states: 0, 1, and 2, encoded in header bits 5:4 as
`00`, `10`, and `20`.

For each response build, the ECM:

1. remembers the old state as the expected MPM RX sequence;
2. advances `0 -> 1 -> 2 -> 0`;
3. transmits the new state in TX[0].bits5:4.

The MPM command is accepted only when:

```text
RX[0].bit7 == 1
RX[0].bits5:4 == expected_previous_sequence
inner_crc_is_valid
PCS1 RX[16].bits1:0 == 2       # in runtime mode 0
```

This is an echo/ack rolling sequence, not cryptographic authentication.

A resynchronization frame built by `QueueMPMResynchronizationPacket` has:

```text
TX[0] = next_sequence_bits | 0x05
TX[1:15] initially zero, then the normal inner CRC is installed
```

The two timeout handlers wait at least `0x5000` timebase ticks before queuing
that frame; they also service a watchdog function while waiting.

### Inner CRC-16

The inner checksum is CRC-16/IBM (reflected polynomial `0xA001`, initial value
zero, no final XOR).  The lookup table starts at `0x12A798`; its first entries
are the canonical `0000 C0C1 C181 0140 ...` table.

Incoming command CRC:

```text
crc = CRC16_IBM(RX[3], RX[4], RX[5], RX[0])
RX[1] = crc high byte
RX[2] = crc low byte
```

Outgoing status CRC:

```text
crc = CRC16_IBM(TX[3], TX[4], TX[5], TX[6], TX[7], TX[8], TX[0])
TX[1] = crc high byte
TX[2] = crc low byte
```

Notably, RX[6:15] and TX[9:15] are not covered by the inner CRC.  The cyclic
outer XOR can cover all 17 data/state bytes, but its receive result is logged
rather than used as a parser gate.

### Incoming MPM command layout

| Byte | Field | Meaning/use |
|---:|---|---|
| 0 | header | valid bit 7; three-state sequence in bits 5:4 |
| 1 | CRC high | CRC-16/IBM high byte |
| 2 | CRC low | CRC-16/IBM low byte |
| 3 | flag group A | persistent/event controls and a 3-bit mode |
| 4 | flag group B | persistent/event controls; most require two consecutive assertions |
| 5 | flag group C | status, a 4-bit value, and reset requests |
| 6 | selector | `00..3F` retained-data command ID |
| 7 | argument | selector payload byte |
| 8-15 | analog payload | eight multiplexed remote-analog bytes |

### RX[3] flag group A

The bypass condition mentioned below is
`0x40011C5E == 1 && 0x4001B45E == 0`.

| Bit | Exact firmware effect | Interpretation/confidence |
|---:|---|---|
| 7 | when asserted in consecutive eligible packets, set retained byte `0x40001B38=1` | confirmed persistent event A; external meaning unknown |
| 6 | when asserted in consecutive eligible packets, set `0x40001B0F=1` | confirmed persistent event B; external meaning unknown |
| 5 | when asserted in consecutive eligible packets, set `0x40001A46=1` | confirmed persistent event C; external meaning unknown |
| 4 | when asserted in consecutive eligible packets, set `0x40001B10=1` | confirmed persistent event D; external meaning unknown |
| 3 | no resolved consumer | unused by this image |
| 2:0 | copy value `0..7` to `0x4001A0DC` | 3-bit mode/status value; exact external name unknown |

Bits 7:4 are ignored under the bypass condition.  Their repeated-frame
qualification is debounce/confirmation behavior, not edge triggering.

### RX[4] flag group B

| Bit | Exact firmware effect | Interpretation/confidence |
|---:|---|---|
| 7 | on confirmed assertion sets one of two application status latches (`0x4001A0D0` or `0x4001A0CC`) subject to runtime conditions | confirmed event/status latch |
| 6 | on two consecutive assertions, records event ID 10/type 5 and sets retained byte `0x40001AE4=1` | confirmed persistent event |
| 5 | copied to `0x4001BAFD` | direct status flag |
| 4 | on two consecutive assertions, records event ID 8/type 5 and sets `0x40001ADF=1` | confirmed persistent event |
| 3 | on two consecutive assertions, sets `0x40001AB8=1` | confirmed persistent event |
| 2 | on two consecutive assertions, sets `0x40001AB3=1` | confirmed persistent event |
| 1 | on two consecutive assertions, records event ID 9/type 5 and sets `0x40001AE2=1` | confirmed persistent event |
| 0 | on two consecutive assertions, records event ID 2/type 5 and sets `0x40001AC0=1` | confirmed persistent event |

The event IDs are internal record indexes; assigning connector or subsystem
names to them would require calibration-symbol or schematic evidence.

### RX[5] flag group C

| Bits | Exact firmware effect | Interpretation/confidence |
|---:|---|---|
| 7 | copied to `0x4001BAEB` | direct MPM status |
| 6 | on two consecutive assertions, records event ID 1/type 5 | confirmed persistent event |
| 5:2 | copied as a 4-bit value to retained byte `0x40001ACE` | MPM-supplied mode/value |
| 1 | clears three transient states and invokes three reset/clear functions | reset/clear request A |
| 0 | clears `0x4001BAE9` | reset/clear request B |

### RX[6:7] selector and argument

The selector spans exactly `00..3F`.  Most commands update checksummed
retained RAM rather than driving hardware immediately.  The two large ordered
records have a firm downstream identity:

- `BuildOBDMode09CalibrationIdResponse` at `0x23AA04` returns the 16-byte
  records in a `49 04` response (OBD Service $09 calibration ID);
- `BuildOBDMode09CVNResponse` at `0x23ABA8` returns the four-byte records in a
  `49 06` response (OBD Service $09 calibration verification numbers).

| Selector | Operation using RX[7] |
|---|---|
| `00..0F` | assemble positions 0..15 of an OBD $09 calibration-ID record at `0x40001854`; commit after a complete ordered set using record mode/check 1 |
| `10..1F` | assemble the same calibration-ID positions 0..15 using record mode/check 0 |
| `20..23` | write retained bytes `0x40001840..0x40001843` |
| `24..27` | write retained bytes `0x40001846..0x40001849` |
| `28..2B` | write retained bytes `0x4000183A..0x4000183D` |
| `2C..2D` | write retained bytes `0x40001844..0x40001845` |
| `2E..2F` | write retained bytes `0x4000184A..0x4000184B` |
| `30..31` | write retained bytes `0x4000183E..0x4000183F` |
| `32..33` | ordered four-byte OBD $09 CVN record mode 1; selector 32 also clears/starts positions 0 and 1 before writing positions 2/3 |
| `34..35` | ordered four-byte OBD $09 CVN record mode 0; selector 34 also clears/starts positions 0 and 1 before writing positions 2/3 |
| `36` | update retained maximum byte `0x400018FE` if argument is larger |
| `37` | update retained maximum byte `0x400018FF` if argument is larger |
| `38` | update retained maximum byte `0x40001900` if argument is larger |
| `39` | write retained byte `0x40001909` |
| `3A` | update runtime byte `0x4001B950`, detect increase against prior value |
| `3B` | write high byte of runtime 16-bit value `0x4001B94E` |
| `3C` | write low byte of runtime 16-bit value `0x4001B94E` |
| `3D` | commit the assembled runtime bundle into retained checksummed fields when change is pending |
| `3E` | update retained maximum byte `0x400018B5` |
| `3F` | update retained minimum byte `0x400018B4` |

This selector space is an OBD identification/CVN, retained-data, and
statistics synchronization protocol.  It does not directly call SIU, eTPU,
DSPI-B, or injector/ignition output routines.

### RX[8:15] remote analog payload

`ApplyMPMRemoteAnalogPayloadByte` at `0x2113E0` dispatches the eight bytes
through the jump table at `0x211410`:

| RX byte | Dispatch index | Behavior |
|---:|---:|---|
| 8 | 0 | remote analog channel 0; 8-bit or raw/scaled depending on configuration |
| 9 | 1 | remote analog channel 1 |
| 10 | 2 | remote analog channel 2 |
| 11 | 3 | channel 0 extension/alternate; may combine with RX[8] into 16 bits |
| 12 | 4 | channel 1 extension/alternate; may combine with RX[9] into 16 bits |
| 13 | 5 | channel 2 extension/alternate; may combine with RX[10] into 16 bits |
| 14 | 6 | channel 3/extension byte; behavior depends on mode 2 |
| 15 | 7 | completes channel 3 as 8/16-bit and applies mode-dependent scale, including a 0.16 divisor mode |

The whole bank is ignored if enable byte `0x0005CEEC` is zero.  In this E78
binary that byte is exactly `00`, so **RX[8:15] have no runtime effect** and
the feedback reader returns zero.  The table above documents a capability in
the shared code, not an enabled E78 feature.  If enabled in another
calibration, per-channel mode/configuration words immediately following the
guard determine whether a sample is 8-bit, paired 16-bit, raw, or scaled.

## Outgoing command/status overlay

`BuildMPMStatusResponsePacket` at `0x289760` constructs bytes 0-15, and
`FinalizeAndQueueMPMStatusResponse` at `0x289ADC` installs the inner CRC and
copies them to DMA TX.

| Byte | Field | Source/use |
|---:|---|---|
| 0 | header | next 3-state sequence in bits 5:4 |
| 1-2 | CRC16 | big-endian inner CRC |
| 3 | status group A | eight ECM status bits |
| 4 | status group B | eight ECM status bits |
| 5 | status group C | direct ECM status byte |
| 6-8 | zero | no writer after BSS initialization |
| 9 | rotating/multiplex ID | startup sweep and later cyclic range |
| 10-11 | feedback A | big-endian remote-analog feedback |
| 12-13 | feedback B | big-endian remote-analog feedback |
| 14-15 | feedback C | big-endian remote-analog feedback |
| 16 | transport state request | normally remains `09` |
| 17 | physical XOR/footer | cyclic path rebuilds it; no direct overlay rebuild was resolved |

### TX[3] status group A

| Bit | Exact source |
|---:|---|
| 7 | retained flag `0x40001AC8 != 0` |
| 6 | retained flag `0x40001B04 != 0` |
| 5 | any internal event state 10..18 active, or either of two related retained fault fields active |
| 4 | runtime flag `0x40016219 != 0` |
| 3 | protocol runtime flag `0x4001BB34 == 1` |
| 2 | protocol runtime flag `0x4001BB35 == 1` |
| 1 | MPM synchronization/status latch `0x4001BAE8 != 0` |
| 0 | MPM synchronization/status condition involving `0x4001BAE9`, another runtime check, and override `0x40011C5E` |

### TX[4] status group B

| Bit | Exact source |
|---:|---|
| 7 | retained flag `0x40001B36` when global override is not active |
| 6 | retained flag `0x40001B0C` when global override is not active |
| 5 | retained flag `0x40001A64` when global override is not active |
| 4 | retained flag `0x40001B0D` when global override is not active |
| 3 | runtime flag `0x4001B45E` |
| 2:0 | retained 3-bit value `0x40001AD4 & 7` |

TX[5] is copied directly from retained byte `0x40001AD0`.

These are exact software sources.  Their physical names remain unknown; the
firmware mostly treats them as retained event/DTC state rather than immediate
output commands.

### TX[9] multiplex sequence

The multiplex ID starts from zero after BSS initialization:

1. it emits `00..31` during the initial sweep;
2. it then normally cycles `36..3F`;
3. under the alternate synchronization condition it emits `32..35` once
   before entering the `36..3F` cycle.

It is independent of the three-state header sequence.

### TX[10:15] analog feedback

`ReadMPMRemoteAnalogFeedback` at `0x2118B0` uses the jump table at `0x2118E8`.
The 3-state sequence rotates which paired channels are returned:

| updated sequence state | TX[10:11] | TX[12:13] |
|---:|---|---|
| 0 | feedback channel 0 | feedback channel 3 |
| 1 | feedback channel 1 | feedback channel 4 |
| 2 | feedback channel 2 | feedback channel 5 |

TX[14:15] normally returns channel 6.  A synchronization condition can replace
it with another application value and marks the related protocol state.  With
E78's feature guard `0x0005CEEC=0`, the normal feedback accessor returns zero,
so these channel values are not active unless the alternate value is selected.

## Complete resolved function map

### Transport and packet access

| Address | Name | Role |
|---:|---|---|
| `0xB260C` | `QueueDSPIDPCS1Packet` | submit fixed 18-byte PCS1 DMA transaction |
| `0xB25C8` | `CompleteDSPIDPCS1DmaTransfer` | complete state and snapshot RX |
| `0xB26B4` | `CompleteDSPIDPCS1DmaTransferThunk` | completion thunk |
| `0x82878` | `ValidateDSPIDPCS1DmaReceiveChecksum` | outer RX XOR check |
| `0x828C0` | `WriteDSPIDPCS1TransmitByte` | cyclic writer into DMA TX |
| `0x828D4` | `ReadDSPIDPCS1ReceiveByte` | cyclic reader from DMA RX |
| `0x828E0` | `ComputeDSPIDPCS1DmaTransmitXorChecksum` | unresolved/unused direct DMA XOR helper |
| `0xB2548` | `WriteDSPIDPCS1DmaTransmitByte` | overlay writer into DMA TX |
| `0xB2560` | `ReadDSPIDPCS1SnapshotByte` | read completion snapshot |
| `0xB271C` | `ReadMPMCommandSnapshotByte` | command-layer read thunk |
| `0xB256C` | `GetDSPIDPCS1CompletionAndAckState` | completion plus RX[16].bits1:0 gate |
| `0xB2740` | `GetMPMSystemMonitorTransactionState` | MPM parser-facing status thunk |
| `0xB26C4` | `RetryDSPIDPCS1PacketUnlessValidHeader` | queues non-`06`/`19` overlay frame |

### Cyclic service and clock trim

| Address | Name | Role |
|---:|---|---|
| `0xD5B24` | `InitializeMPMRuntime` | initializes MPM subsystem and product mode |
| `0xD7FE8` | `InitializeDSPIDPCS1Packet` | builds cyclic template |
| `0xD7F68` | `ClearMPMReceivePacket` | clears working RX |
| `0xD73C8` | `InitializeMPMControlState` | initializes normal/failsafe sources |
| `0xD74F4` | `ServiceMPMControlStateAndOutputs` | failsafe switch and companion outputs |
| `0xD6F00` | `UpdateMPMTransmitControlFields` | updates TX[3:5] and GPIO182 |
| `0xD80A4` | `ExchangeDSPIDPCS1CyclicPacket` | consumes old RX and queues next cyclic TX |
| `0xD7FC0` | `ComputeDSPIDPCS1PacketXorChecksum` | XOR bytes 0-16 |
| `0xD7C78` | `ServiceMPMSchedulerPhase` | 10-phase dispatcher |
| `0xD7E6C` | `ServiceMPMCyclicScheduler` | top-level MPM scheduler |
| `0xD8738` | `InitializeMPMClockMeasurementHardware` | trim measurement setup |
| `0xD86A4` | `ServiceMPMClockMeasurementCapture` | capture service |
| `0xD8550` | `ProcessMPMClockMeasurementCapture` | min/max/sample processing |
| `0xD8B28` | `ServiceMPMClockTrimCalibration` | trim state machine |
| `0xD8A58` | `AdjustMPMClockTrimTowardTarget` | one-step target adjustment |
| `0xD87D4` | `AverageMPMClockTrimFineWindow` | ten-sample fine window |
| `0xD8860` | `FinalizeMPMClockTrimCalibration` | final validation/commit |
| `0xD88F4` | `ProcessMPMClockTrimRequest` | external calibration request dispatcher |
| `0xD89A0` | `LogMPMClockTrimSample` | trace buffer logger |

### Command/status overlay

| Address | Name | Role |
|---:|---|---|
| `0x288FB8` | `CopyAndValidateMPMCommandHeaderCrc` | copies RX[0:15], validates inner CRC |
| `0x289698` | `ProcessMPMCommandPacketIfReady` | transport, CRC, valid-bit, and sequence gate |
| `0x289060` | `ApplyMPMCommandPacket` | applies flags, selector, and analog payload |
| `0x289748` | `InitializeMPMCommandEdgeHistory` | clears repeated-bit history |
| `0x289B88` | `InitializeMPMCommandSequence` | initializes sequence state to 2 |
| `0x289760` | `BuildMPMStatusResponsePacket` | builds response header/status/feedback |
| `0x289ADC` | `FinalizeAndQueueMPMStatusResponse` | inner CRC, DMA copy, queue |
| `0x289B68` | `ServiceMPMCommandResponseProtocol` | response service wrapper |
| `0x289B98` | `QueueMPMResynchronizationPacket` | opcode-5 resync packet |
| `0x289C00` | `HandleMPMResponseTimeoutA` | timeout/resync path A |
| `0x289C80` | `HandleMPMResponseTimeoutB` | timeout/resync path B |
| `0x28A070` | `ServiceMPMCommandReceivePath` | command parser plus related diagnostic service |
| `0x28A094` | `InitializeMPMCommandProtocolState` | initializes command latches/history |
| `0x2113E0` | `ApplyMPMRemoteAnalogPayloadByte` | RX[8:15] jump-table decoder |
| `0x2118B0` | `ReadMPMRemoteAnalogFeedback` | response feedback jump table |
| `0x12A76C` | `UpdateCRC16IBM` | CRC-16/IBM wrapper |
| `0xCD14C` | `UpdateCRC16IBMWithTable` | table-driven CRC core |
| `0x23AA04` | `BuildOBDMode09CalibrationIdResponse` | returns MPM-populated records as `49 04` |
| `0x23ABA8` | `BuildOBDMode09CVNResponse` | returns MPM-populated records as `49 06` |
| `0x23CB30` | `AssembleMPMOBDCalibrationIdByte` | ordered 16-byte CAL-ID record assembler |
| `0x23C968` | `AssembleMPMOBDCVNByte` | ordered four-byte CVN record assembler |

## Scheduler cadence

`ServiceMPMCyclicScheduler` increments a phase counter and calls
`ServiceMPMSchedulerPhase(counter % 10)`.  Phase 4 performs the cyclic PCS1
exchange; phase 6 performs clock-trim processing.  Thus the cyclic service
frame occurs once per ten scheduler invocations.  The absolute time depends
on the indirect runtime dispatcher period, which is not encoded in these
functions as a literal millisecond delay.

The command/status service is called from the large application task at
`0x27E19C`, independently of the ten-phase cyclic scheduler.  Both clients use
the DSPI DMA queue and the header check to arbitrate the shared PCS1 buffer.

## What this says about fuel/output enable

The firmware now rules out several attractive but unsupported theories:

- RX[12:15] are not decoded as a fuel-enable bitmap.
- RX[16].bits1:0 is a packet/state acknowledgment gate, not a direct injector enable.
- RX[16].bits3:2 belongs to clock-trim readiness.
- RX selectors `00..3F` update OBD $09 calibration-ID/CVN records, retained
  statistics, and remote data; they do not directly call SIU/eTPU/C2MIO
  output enables.
- The optional RX[8:15] remote-analog path is compiled in but disabled by
  E78's zero guard byte at `0x0005CEEC`.
- The clear MPM safety transition is the combination of GPIO182 and cyclic
  TX[3:4] changing from `1,0` to `0,1` under global failsafe.

The MPM can still influence whether the overall application regards itself as
healthy, and its persistent event data can affect higher-level strategy later.
But no direct code path from an MPM response bit to the missing C2MIO injector
gate/FSE enable has been resolved.

## Ghidra annotations made during this pass

- Named the previously missed DMA RX-XOR callback at `0x82878` and DMA TX-XOR
  helper at `0x828E0`.
- Named the completion/snapshot path at `0xB25C8`, `0xB2560`, and `0xB271C`.
- Named the full command parser and response builder at `0x288FB8..0x289C80`.
- Named both remote-analog jump-table functions at `0x2113E0` and `0x2118B0`.
- Added user references for all eight entries in jump tables `0x211410` and
  `0x2118E8`.
- Named the CRC-16/IBM wrapper/core and the cyclic packet fields for navigation.
- Added plate comments describing byte ranges, checksums, sequence semantics,
  and state-field consumers.

## Remaining unknowns

The binary alone does not reveal:

- the schematic names for the retained event bits in RX[3:5];
- the connector/sensor names of the remote analog channels;
- the MPM vendor's names for the two 2-bit state machines in byte 16;
- whether bytes marked fixed/reserved have meanings used only by other MPM firmware revisions;
- the exact absolute scheduler period without resolving the surrounding runtime dispatcher timing.

Those unknowns are deliberately left as addresses and operational effects
rather than assigned speculative hardware names.
