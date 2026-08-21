# E78 external hardware startup setup

This document extracts the externally visible setup performed by
`InitializeApplicationRuntime` at `0x00080010` in `E78.bin`. It focuses on:

- SIU pad mux and direction setup;
- explicit GPIO input sampling and output levels;
- externally routed eTPU/eMIOS outputs;
- every confirmed DSPI-B and DSPI-D transfer made during application startup.

It is intentionally narrower than
`E78_INITIALIZE_APPLICATION_RUNTIME_TRACE.md`: RAM, TLB, interrupt-controller,
and inter-core operations are omitted unless they change an external pin or
serial transaction.

## Conventions

- GPIO states are the physical GPDO level when the descriptor is known to be
  active-high. All explicitly driven GPIO descriptors listed below are
  active-high.
- An eTPU/eMIOS channel described as **inactive** has been given its safe
  software/timing state. That does not guarantee a DC-low pad at every instant;
  the peripheral owns the pad after muxing.
- DSPI-B values are shown as 16-bit transmitted words and, where useful, as
  big-endian wire bytes.
- DSPI-D transfers clock six bytes. The first three are command bytes; the
  final three are dummy clocks used to receive the returned three-byte state.
- “Conditional” means the exact branch depends on boot reason, ASIC revision,
  a sampled input, or companion response.

## 1. Bulk SIU setup

`CopySIUPadAndInputMuxConfiguration` copies:

- 214 input-mux bytes from ROM `0x000C4508` to SIU `0xC3F90600`;
- 231 PCR halfwords from ROM `0x000C45DE` to SIU `0xC3F90040`.

This happens before any application DSPI transfer or explicit GPIO write.

### Active DSPI pads

| Bus | Pad | Function | PCR |
|---|---:|---|---:|
| DSPI-D | 98 | SCKD | `0x0A04` |
| DSPI-D | 99 | SIND | `0x0914` |
| DSPI-D | 100 | SOUTD | `0x0A14` |
| DSPI-D | 87 | PCSD3 | `0x0A04` |
| DSPI-D | 91 | PCSD1 | `0x0A04` |
| DSPI-D | 106 | PCSD0 | `0x0A04` |
| DSPI-B | 102 | SCKB | `0x0604` |
| DSPI-B | 103 | SINB | `0x0514` |
| DSPI-B | 104 | SOUTB | `0x0614` |
| DSPI-B | 105 | PCSB0 | `0x0604` |

DSPI-B uses PCS0. DSPI-D uses PCS0, PCS1, and PCS3 for three separate devices.
DSPI-A and DSPI-C form a transmit-only 21-bit DSI chain to the C2MIO ASIC.
DSPI-C supplies SCKC and PCSC0; its serialized data is internally chained
through DSPI-A and exits on SOUTA. The source is normally live eTPU state,
so this path has no ordinary software transaction call sites and requires no
MISO pin.

### GPIO pads used during startup

| GPIO | PCR | Startup use |
|---:|---:|---|
| 4–7 | `0x0110` each | Input straps sampled into the four-bit board revision |
| 72 | `0x0110` initially | Shared-output ownership input; conditionally changed to driven/readback output |
| 73 | `0x0210` | Conditional shared-output configuration driver |
| 89 | `0x0110` | Revision E/F routed input |
| 92 | `0x0500` | Hardware-variant input combined with AN30 and GPIO203 |
| 101 | `0x0204` | Active-high output object, commanded low in the safe-state pass |
| 114 | `0x0210` | Active-high output object, commanded low in the safe-state pass |
| 118 | `0x0110` | Discrete input captured by startup diagnostics |
| 119 | `0x0210` | First line of an external two-bit selector |
| 121 | `0x0110` | Sampled discrete input |
| 130 | `0x0114` | Runtime-routed GPIO/eTPU input |
| 143 | `0x0310` | Active-high bidirectional/readback object, commanded low |
| 159 | `0x0310` | Active-high bidirectional/readback object, commanded low |
| 160 | `0x0310` | Active-high GPIO/eTPU selection path, commanded high |
| 176 | `0x0310` | Second line of the GPIO119/176 selector |
| 177–178 | `0x0510` each | eTPU input/readback channels |
| 180 | `0x0310` | Active-high bidirectional/readback object, commanded low |
| 182 | `0x0310` | Active-high bidirectional/readback object, commanded low |
| 186 | `0x0110` | Non-E/F revision-routed input |
| 187–188 | `0x0310` each | Active-high throttle/H-bridge control candidates, explicitly driven low |
| 191 | `0x0310` | Active-high bidirectional/readback object, commanded low |
| 197 | `0x0310` | Active-high bidirectional/readback object, commanded high |
| 203 | `0x0110` | Hardware-variant input combined with AN30 and GPIO92 |
| 205 | `0x0210` | Static output initialized high; no normal runtime caller |

PCR values describe the SIU setup image. A bidirectional/readback PCR does not
by itself prove whether the attached board net is an input, open-drain-style
control, or push-pull output; the explicit command calls below prove the
startup command direction.

### Externally routed timed outputs

| Pads | Peripheral channels | PCR | Startup condition |
|---|---|---:|---|
| 132–139 | eTPU-A18–A25 | `0x0700` each | Eight inferred injector timing outputs; initialized with no active pulse command |
| 167–174 | eTPU-B20–B27 | `0x0700` each | Eight confirmed ignition timing outputs; initialized with no active pulse command |
| 185 | eMIOS6 | `0x0700` | PWM output initialized from the eMIOS register image |
| 196 | eMIOS17 | `0x0700` | PWM output initialized from the eMIOS register image |
| 199 | eMIOS20 | `0x0700` | PWM output initialized from the eMIOS register image |
| 201 | eMIOS22 | `0x0700` | PWM output initialized from the eMIOS register image |
| 202 | eMIOS23 | `0x0704` | PWM output grouped with GPIO187/188 |

The SIU image also configures the corresponding timed inputs listed in
`E78_CODE_VERIFIED_PIN_MAP.md`; they are omitted here because they are not
driven externally by the ECM.

## 2. Explicit GPIO setup in execution order

### Shared-output ownership branch

`DetectAndConfigureSharedOutputOwnership` waits 200 timebase units and reads
GPIO72 on cold/recovery startup.

- GPIO72 high: record ownership state 0; do not enable the two drivers.
- GPIO72 low, or a retained ownership request: call
  `EnableSharedOutputConfigurationDrivers`.

The enable call performs this exact order:

1. write GPIO72 GPDO high;
2. write GPIO73 GPDO high;
3. configure GPIO72 as output with readback;
4. configure GPIO73 as output with readback.

The output latch is deliberately written before output-enable is asserted.

### Throttle/H-bridge candidate controls

Before eTPU channel setup:

1. `SetGPIO187OutputState(0)` drives GPIO187 low;
2. `SetGPIO188OutputState(0)` drives GPIO188 low.

Both descriptors are active-high and both pins have PCR `0x0310`.

### Board and hardware identification inputs

The application samples:

1. GPIO4 → board-revision bit 0;
2. GPIO5 → board-revision bit 1;
3. GPIO6 → board-revision bit 2;
4. GPIO7 → board-revision bit 3.

It later selects one additional routed input:

- board revision E/F: configure and read GPIO89;
- every other revision: configure and read GPIO186.

The late hardware classifier samples GPIO92 and GPIO203 together with AN30.
These produce a hardware class used to choose runtime dispatch table type 2 or
3; they are distinct from the GPIO4–7 board-revision nibble.

### Actuator safe-state pass

`InitializeActuatorOutputStartupStates(0)` issues the following externally
relevant GPIO commands in this order:

| Order | Command | Physical level |
|---:|---|---|
| 1 | GPIO114 ← 0 | Low |
| 2 | GPIO182 ← 0 | Low |
| 3 | GPIO159 ← 0 | Low |
| 4 | GPIO180 ← 0 | Low |
| 5 | GPIO143 ← 0 | Low |
| 6 | GPIO182 ← 0 again | Low; intentional repeated wrapper call |
| 7 | GPIO191 ← 0 | Low |
| 8 | GPIO101 ← 0 | Low |
| 9 | GPIO160 ← 1 | High |

The same pass also:

- clears three companion/diagnostic object commands associated with logical
  descriptors `0x152`, `0x154`, and `0x156`;
- places the timed-output objects at zero/inactive state;
- marks the command objects initialized.

Those three cached object calls are not additional proven DSPI wire
transactions. The selector-10 transmitter still has no proven E78 caller.

Immediately afterward:

- `SetGPIO197HighAtStartup` commands GPIO197 high.

GPIO205 is already initialized high by its static object/setup image and is
not rewritten by the normal startup path.

## 3. Exact DSPI-B startup transfers

DSPI-B uses 16-bit frames, MSB first, CPOL=0, CPHA=0, CTAR0, and PCS0. The
controller setup values are:

```text
MCR while halted: 813F0C01
CTAR0:            78015503
SR clear mask:    9A0A0000
```

### Chronological transfer list

#### B1 — first identification

`IdentifyDSPIBAsicAndLoadInitTable`:

```text
words: 0E1B 0000
bytes: 0E 1B 00 00
```

The response revision bits provisionally select a 19-word initialization
table.

#### B2 — second identification and final revision classification

`ReidentifyDSPIBAsicAndLoadInitTable`:

```text
words: 0E1B 0000
bytes: 0E 1B 00 00
```

Received word 1 bits 14:12 are classified as ASIC class 3 when below 4, or
class 4 otherwise.

#### B3 — main ASIC initialization

ASIC class 4:

```text
CF4C 25B7 16EC 0011 1E10 1E11 1331 7575 0055 3E00
0013 FB82 0080 0080 0A80 0000 0000 0000 00F0
```

Wire bytes:

```text
CF 4C 25 B7 16 EC 00 11 1E 10 1E 11 13 31 75 75 00 55
3E 00 00 13 FB 82 00 80 00 80 0A 80 00 00 00 00 00 00
00 00 00 F0
```

ASIC class 3:

```text
CF4C 25B7 16EC 0011 1E10 1E11 1331 3030 0000 3E00
0013 FB82 0080 0080 0000 0000 0000 0000 0541
```

Wire bytes:

```text
CF 4C 25 B7 16 EC 00 11 1E 10 1E 11 13 31 30 30 00 00
3E 00 00 13 FB 82 00 80 00 80 00 00 00 00 00 00 00 00
00 00 05 41
```

#### B4 — class-3-only prefix

Only when ASIC class 3:

```text
words: CF4C 25B7 16EC 0011
bytes: CF 4C 25 B7 16 EC 00 11
```

#### B5 — third identification/cache refresh

After eTPU setup and the GPIO4–7 revision sample:

```text
words: 0E1B 0000
bytes: 0E 1B 00 00
```

This result is cached at `0x4000B204`; it does not reload selector 0.

#### B6/B7 — revision-4 analog-front-end startup test

Only for ASIC class 4, the application programs selector 4 to:

```text
words: 0F1D 1450
bytes: 0F 1D 14 50
```

It waits 20 timebase units and samples four eQADC commands:

```text
84000180
94010180
8A020180
9A030180
```

It then changes selector 4 and sends:

```text
words: 0F1D 04F0
bytes: 0F 1D 04 F0
```

It waits another 20 timebase units. These two selector-4 writes occur inside
`RunDSPIBRevision4AnalogFrontendStartupTest`.

#### B8 — bulk diagnostic query

The late diagnostic initializer sends selector 12:

```text
words: 835F 0000 0000 0000 0000 0000 0000 0000 0000
bytes: 83 5F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

The nine returned words feed the 54-entry diagnostic decoder.

### Selector-6 transition into normal service

The selector-6 buffer is initialized to `0F14 3E00`. The recovered transition
at startup/run entry is:

```text
0F14 3E00
0F14 3E00
0F14 3E20
```

The later heartbeat toggles bit 10:

```text
0F14 3A20
0F14 3E20
```

These are normal service/control writes rather than all being direct calls
from the linear `InitializeApplicationRuntime` body.

### First normal cyclic DSPI-B queries

After the runtime scheduler starts, the stock application repeatedly sends:

```text
selector 11: 011F xxxx / 0F00 xxxx   rotating protected diagnostic query
selector 7:  0B01 0000 0000 1FC0    four-word status query
selector 12: 835F + eight 0000 words bulk diagnostic query
```

Selector 3 (`0F1A xxxx`) and selector 6 (`0F14 xxxx`) are queued only when
their runtime dirty flags are set.

## 4. Exact DSPI-D startup transfers

The three RAM templates are initialized as:

```text
Group 5: 6A 0C 00 00 00 00
Group 4: 00 00 00 00 00 00
Group 6: 80 F8 00 00 00 00
```

### Chronological fixed transfers

| Order | Cause | Six transmitted bytes |
|---:|---|---|
| D1 | Set descriptor `0x18A = 1` during hardware initialization | `80 FC 00 00 00 00` |
| D2 | First top-level Group 5 service | `6A 0C 00 00 00 00` |
| D3 | Set descriptor `0x157 = 1` | `6A 2C 00 00 00 00` |
| D4 | Conditional descriptor-`0xFE` refresh on applicable warm boots | `00 00 00 00 00 00` |
| D5 | Later synchronous Group 5 service | `6A 2C 00 00 00 00` |
| D6 | Explicit Group 4 transfer | `00 00 00 00 00 00` |
| D7 | Queued Group 5 service after output safe-state setup | `6A 2C 00 00 00 00` |

Descriptor `0x18A` changes Group 6 bit 2 from `80F8` to `80FC`. Bench
testing confirms that `80 FC 00` opens four ignition-output paths.

Descriptor `0x157` changes Group 5 bit 5 from `6A0C` to `6A2C`; this is
the companion watchdog/service enable.

### Companion startup state-machine transfers

The state machine controls five Group-6 bits:

| Object | Descriptor | Group-6 bit |
|---:|---:|---:|
| 0 | `0x18F` | 3 |
| 1 | `0x193` | 4 |
| 2 | `0x197` | 5 |
| 3 | `0x19B` | 6 |
| 4 | `0x19F` | 7 |

The restored/all-asserted command is:

```text
80 FC 00 00 00 00
```

The first low/high pulse for each possible startup branch is exactly:

| Selected phase | Cleared object bits | Low transfer | Restored transfer |
|---|---|---|---|
| Phase 1 | objects 2–4, bits 5–7 | `80 1C 00 00 00 00` | `80 FC 00 00 00 00` |
| Phase 3 | object 0, bit 3 | `80 F4 00 00 00 00` | `80 FC 00 00 00 00` |
| Phase 5 | object 1, bit 4 | `80 EC 00 00 00 00` | `80 FC 00 00 00 00` |

After the approximately `0x7D000`-tick phase deadline, the application:

1. sends the current/restored Group-6 word;
2. samples the relevant object or objects;
3. clears each object whose returned state is asserted;
4. sends the resulting subset word;
5. reevaluates the phase.

That second subset transfer is response-dependent. For objects 2–4 it can
contain any combination of bits 5–7, so it cannot be represented by one fixed
byte without the received state. Completion waits 4000 ticks, restores all five
objects, sends `80 FC 00 00 00 00`, and enters phase 7.

## 5. Compact external startup chronology

1. Load the SIU input-mux and PCR images.
2. Initialize eQADC, eMIOS, eDMA, and DSPI controller/DMA contexts.
3. DSPI-B: send identification twice.
4. DSPI-B: send the revision-selected 19-word ASIC initialization.
5. DSPI-B: send the four-word prefix if class 3.
6. DSPI-D: send `80 FC 00 00 00 00` to enable the Group-6 bit-2 path.
7. DSPI-D: send `6A 0C`, then set watchdog bit 5 and send `6A 2C`.
8. Run the response-dependent Group-6 companion startup pulse sequence.
9. Conditionally drive GPIO72/73 high and output-enable them.
10. Drive GPIO187/188 low.
11. Initialize eTPU channels and sample GPIO4–7.
12. DSPI-B: send the third `0E1B 0000` identification query.
13. Configure the revision-routed GPIO89 or GPIO186 input.
14. DSPI-D: send `6A 2C` and Group-4 zeros.
15. Put the GPIO and timed actuator objects into safe state; drive GPIO160 and
    GPIO197 high.
16. Queue another `6A 2C` watchdog/service transfer.
17. Finish the Group-6 startup state machine at `80 FC`.
18. DSPI-B class 4: send `0F1D 1450`, sample four analog paths, then send
    `0F1D 04F0`.
19. DSPI-B: send the nine-word `835F` bulk diagnostic query.
20. Sample GPIO92/GPIO203 and AN30 for the final hardware class, then enter the
    permanent runtime dispatcher.

## Limits

The code proves pad numbers, mux values, physical GPIO levels, and transmitted
serial words. It does not provide connector cavity names or ASIC register
names. Board-net interpretations such as injector, ignition, throttle, and
knock are retained only where supported by the existing code analysis and
bench observations.
