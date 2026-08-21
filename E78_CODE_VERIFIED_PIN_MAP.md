# E78 MPC5566 code-verified pin map

This map is derived from `E78.bin` application and bootloader code, not merely
from the reset/static SIU PCR image.  A pad is called **active** only when the
code also initializes or accesses the owning peripheral/channel, calls a GPIO
object, or changes the mux at runtime.

Confidence terms:

- **Confirmed**: the channel/pad and its code role are directly demonstrated.
- **Strong inference**: the electrical role follows from a distinctive group
  of channels and their consumers, but no schematic/net name is available.
- **Unknown net**: code use and direction are proven; the board-level name is
  not.
- **Mux-only/template**: present in the PCR image but not supported by runtime
  peripheral or access evidence.

## High-value findings

- GPIO167–174 / eTPU-B20–B27 are the eight ignition-coil timing outputs
  (**confirmed by code and bench observation**).
- GPIO132–139 / eTPU-A18–A25 are the matching eight-channel fuel-injector
  timing array (**strong inference**).  They use eTPU function `0x19`; ignition
  uses function `0x17`.
- DSPI-D is a shared three-device bus.  The companion watchdog/output device
  uses PCS0/GPIO106, an 18-byte cyclic packet device uses PCS1/GPIO91, and a
  one-byte cyclic control/status device uses PCS3/GPIO87.
- DSPI-B is active and controls a four-channel programmable analog front end.
  It configures the device and immediately samples four external-mux ADC
  channels.  This is a **strong match for the four-channel knock front end**.
- Five externally routed eMIOS channels are outputs.  Four are independently
  commanded PWM channels and strongly match the four cam-phaser solenoids.
  The fifth PWM is paired with GPIO187/188 as an H-bridge control group and
  strongly matches the electronic-throttle motor driver.
- eMIOS8 is the bootloader periodic interrupt time base, but pad GPIO187 is not
  routed to eMIOS8.  The timer is internal; GPIO187 remains an SIU GPIO.
- Both CAN-A and CAN-B are active in the application.  The bootloader uses
  CAN-A.  CAN-C and CAN-D have no runtime evidence.
- The large EBI pin selection is incomplete and has no controller or external
  memory use.  It is a default/template configuration, not a board bus.

## CAN

| SIU pad | Signal | Code-supported use | Confidence |
|---:|---|---|---|
| 83 | CAN-A TX | Application and bootloader CAN-A transmit | Confirmed |
| 84 | CAN-A RX | Application and bootloader CAN-A receive | Confirmed |
| 85 | CAN-B TX | Application CAN-B transmit | Confirmed |
| 86 | CAN-B RX | Application CAN-B receive | Confirmed |

CAN-C (`0xFFFC8000`) and CAN-D (`0xFFFCC000`) have no code references and no
active pad assignment.

## DSPI

| SIU pad | Signal | Code-supported use | Confidence |
|---:|---|---|---|
| 98 | DSPI-D SCK | Companion-ASIC clock | Confirmed |
| 99 | DSPI-D SIN | Companion-ASIC receive | Confirmed |
| 100 | DSPI-D SOUT | Companion-ASIC transmit | Confirmed |
| 106 | DSPI-D PCS0 | Companion-ASIC chip select; transaction descriptor selects PCS0 | Confirmed |
| 87 | DSPI-D PCS3 | One-byte cyclic control/status device; descriptor mask `0x00080000`, CTAR2 | Confirmed |
| 91 | DSPI-D PCS1 | 18-byte cyclic packet device; descriptor mask `0x00020000`, CTAR4/5/3 | Confirmed |
| 102 | DSPI-B SCK | Four-channel analog-front-end clock | Confirmed |
| 103 | DSPI-B SIN | Four-channel analog-front-end receive | Confirmed |
| 104 | DSPI-B SOUT | Four-channel analog-front-end transmit | Confirmed |
| 105 | DSPI-B PCS0 | Four-channel analog-front-end chip select | Confirmed |
| 95 | DSPI-A SOUT | Final MOSI output of the 21-bit DSPI-C/DSPI-A DSI serial chain to C2MIO | Confirmed |
| 109 | DSPI-C SCK | Clock output for the transmit-only C2MIO DSI serial chain | Confirmed |
| 110 | DSPI-C PCS0 | Select output for the transmit-only C2MIO DSI serial chain | Confirmed |

The application creates contexts for A, B, C, and D. B and D perform normal
SPI transactions, while C and A form a hardware DSI serialization chain. The
context at `0xB98AC` begins with controller
index 3 and is therefore DSPI-D despite its older `DSPIA` symbol name.

DSPI-D has three static PCS configuration records at `0xB9934`, `0xB9940`,
and `0xB994C`, containing PCS masks `0x00010000`, `0x00080000`, and
`0x00020000` respectively.  These select PCS0, PCS3, and PCS1.  PCS1 exchanges
an 18-byte full-duplex packet with a byte-17 XOR checksum; its byte 0 alternates
between `0x06` and `0x19`.  PCS3 exchanges one control byte and one status byte
periodically and whenever the desired control state changes.

## eTPU external pins

### Timed outputs

| SIU pads | eTPU channels | Role | Confidence |
|---|---|---|---|
| 132–139 | A18–A25 | Eight fuel-injector timing outputs, eTPU function `0x19` | Strong inference |
| 167–174 | B20–B27 | Eight ignition-coil timing outputs, eTPU function `0x17` | Confirmed |

The two arrays are initialized as eight contiguous channel objects and are the
only symmetric eight-channel external-output groups.  Pad 131/eTPU-A17 is
muxed but has no corresponding active channel object.

### Engine-position and other timed inputs

| SIU pad(s) | eTPU channel(s) | Code-supported role | Confidence |
|---:|---|---|---|
| 147 | B0 | Main engine-position input; owns the large crank/position state machine | Crank input: strong inference |
| 148–151 | B1–B4 | Four position-sensor channel objects consumed by the same subsystem | Cam inputs: strong inference |
| 152–157 | B5–B10 | Six initialized pulse/frequency measurement inputs | Confirmed function; unknown nets |
| 163–166 | B16–B19 | Four initialized special-event/timing channels | Confirmed function; unknown nets |
| 175 | B28 | Initialized timed input/event channel | Confirmed function; unknown net |
| 160 | B29 or GPIO160 | Runtime-muxed between eTPU-B29 and SIU GPIO with input/readback enabled | Confirmed function; unknown net |
| 177–178 | B30–B31 | Two eTPU inputs read by a shared selector helper and diagnostics | Confirmed function; unknown nets |
| 130 | A16 or GPIO130 | Runtime-muxed GPIO input/eTPU-A16 input | Confirmed function; unknown net |
| 146 | TCRCLKB | Selected as the eTPU-B external time-base clock pad | Mux confirmed; external use unproven |

eTPU A0–A15 and B12–B15 also have software channel objects, but their package
pads are not routed to eTPU in this image.  They are internal timing channels,
not additional external board pins.  B11 has a software descriptor but pad 158
is not muxed to it.

## eMIOS external pins

| SIU pad | Channel | Hardware mode/use in code | Likely board role | Confidence |
|---:|---:|---|---|---|
| 185 | 6 | Dynamically commanded PWM output | Cam-phaser solenoid 1 | Strong inference |
| 196 | 17 | Dynamically commanded PWM output | Cam-phaser solenoid 2 | Strong inference |
| 199 | 20 | Dynamically commanded PWM output | Cam-phaser solenoid 3 | Strong inference |
| 201 | 22 | Dynamically commanded PWM output | Cam-phaser solenoid 4 | Strong inference |
| 202 | 23 | Commanded PWM output grouped with GPIO187/188 | Electronic-throttle motor PWM | Strong inference |
| 190 | 11 | IPWM (input pulse-width measurement) | Frequency/PWM sensor input, net unknown | Confirmed function |
| 200 | 21 | PEA (continuous pulse/edge accumulation), read atomically | Pulse-count/frequency input, net unknown | Confirmed function |

Application channels 2, 5, 8, 12, and 16 are active internally but their pads
are not routed to eMIOS.  Bootloader eMIOS8 is likewise an internal periodic
timer/interrupt and does not use pad 187.

## eQADC / analog inputs

All 44 ADC channel objects below have real callers.

### Direct physical analog inputs

| Physical inputs | Code use |
|---|---|
| AN0–AN7 | Direct conversions |
| AN15–AN26 | Direct conversions |
| AN28 | Direct conversion |
| AN30–AN39 | Direct conversions |

AN30 is combined with GPIO92 and GPIO203 to classify the hardware/board
variant.  The remaining direct analog pins are definitely acquired, but the
binary alone does not provide defensible schematic net names.  Likely sensor
names should not be assigned without a connector pinout or a second calibrated
binary.  AN27 and AN29 are the only direct AN0–AN39 inputs with no channel
object/use.

### External analog mux

| Physical pin | Virtual channels used | Selects used | Code-supported role |
|---|---|---|---|
| AN8 / ANW | 66, 69 | 2, 5 | Two analog-front-end outputs |
| AN9 / ANX | 74, 77 | 2, 5 | Two analog-front-end outputs |
| AN10 / ANY | 80–87 | 0–7 | Eight-way external sensor mux |
| AN11 / ANZ | 88 | 0 | One external-mux input |
| pad 215 / AN12 | MA0 | Address bit 0 | External-mux address output |
| pad 216 / AN13 | MA1 | Address bit 1 | External-mux address output |
| pad 217 / AN14 | MA2 | Address bit 2 | External-mux address output |

DSPI-B setup is immediately followed by conversions of channels 66, 74, 69,
and 77.  This couples the DSPI-B device to the four ANW/ANX mux selections and
is the main evidence that DSPI-B is the four-channel knock analog front end.

## SIU GPIOs with code evidence

Direction below follows the descriptor-to-PCR builder and the actual PCR OBE
and IBE bits; it does not rely on stale symbol comments.

| GPIO | Direction/use proven by code | Board-level interpretation |
|---:|---|---|
| 4–7 | Four input straps read once at application startup | Board revision ID nibble (confirmed) |
| 72–73 | Runtime direction-controlled/readback lines alternately selected by the shared-output configuration sequence | Configuration strobes/handshake lines; unknown nets |
| 89 or 186 | Revision-dependent input: revisions E/F select 89, all others select 186; initial state cached | Revision-routed input; unknown net |
| 92 | Input used with AN30 and GPIO203 | Hardware variant/classification strap |
| 101 | Output object; state is sampled/read back | Unknown output net |
| 114 | Output object; state is sampled/read back | Unknown output net |
| 118 | Discrete input read by runtime and diagnostic logic | Unknown input net |
| 119 | Output, first bit of a two-line selector | External 2-bit selector, unknown target |
| 121 | Periodically sampled input | Unknown input net |
| 130 | GPIO input or eTPU-A16 input, runtime muxed | Timed/discrete input, unknown net |
| 143 | Bidirectional/readback GPIO object | Unknown net |
| 159 | Bidirectional/readback GPIO object | Unknown net |
| 160 | Bidirectional GPIO or eTPU-B29, runtime muxed | Timed/discrete signal, unknown net |
| 176 | Driven as second bit of the GPIO119/176 selector; readback enabled | External 2-bit selector, unknown target |
| 180 | Bidirectional/readback GPIO object | Unknown net |
| 182 | Bidirectional/readback GPIO object | Unknown net |
| 187–188 | Two driven/readback control lines grouped with eMIOS23 PWM | Throttle H-bridge direction/control lines (strong inference) |
| 191 | Bidirectional/readback GPIO object | Unknown net |
| 197 | Bidirectional/readback GPIO object | Unknown net |
| 203 | Input used with AN30 and GPIO92 | Hardware variant/classification strap |
| 205 | Static output initialized high; object is registered in the initialization image but has no runtime caller | Unproven output/enable candidate |

GPIO115, 116, 117, 122, 123, 204, and (after initial registration) 205 have
descriptors/static PCR settings but no runtime code references.  They remain
template/unproven.  Other GPIO-looking PCR entries without a referenced object,
direct GPDI/GPDO access, or peripheral role are also not counted as board use.

## Clock and fixed-function observations

| Pad/signal | Code evidence | Classification |
|---|---|---|
| 214 / ENGCLK | Selected in PCR image | Mux-only; no consumer proves board connection |
| 229 / CLKOUT | Clock output selected in PCR image | Mux-only/test-clock candidate |
| EXTAL/XTAL | PLL/eMIOS/eTPU setup assumes the system clock tree | Fixed-function clock pins; SIU cannot identify the crystal net |
| JTAG/Nexus/reset/boot configuration | Fixed-function/reset-time behavior | Not inferable as populated board nets from application SIU accesses |

## Explicitly rejected false positives

### EBI

The PCR image selects CS0, address 13–29, data 0–31, RD/WR, BDIP, byte enables,
OE, and TS on pads 0, 9–25, 28–59, and 62–69.  This is not a functional EBI:

- required address/control coverage is incomplete;
- the application does not initialize/use the EBI controller;
- the Ghidra image contains only flash, standby RAM, and internal SRAM and no
  external-memory block.

Treat all those EBI selections as template/default configuration.

### DSPI-A and DSPI-C

These modules form a 21-bit transmit-only DSI chain rather than issuing
software SPI transactions. DSPI-C is the master and contributes 16 bits;
DSPI-A is the internally clocked slave, contributes five bits, and drives the
final SOUTA pin. Their apparently incomplete pad muxes are therefore the exact
three external signals required: SCKC, PCSC0, and SOUTA. This is evidence of a
connected board device.

## Limits of a code-only map

This report identifies every external signal class that the code proves and
keeps unknown nets explicit.  Code cannot distinguish connector names for the
remaining generic GPIO, analog, pulse-measurement, and eTPU event inputs.  A
schematic, continuity measurements, connector pinout, or a second E78-family
binary with different populated options is needed to name those safely.
