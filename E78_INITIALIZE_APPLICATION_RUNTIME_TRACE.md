# E78 `InitializeApplicationRuntime` execution trace

This is a code-derived, execution-order map of `InitializeApplicationRuntime`
at `0x00080010` in `E78.bin`.  It descends through its direct callees until the
operation is a hardware access, a message transmission, a delay/wait, a memory
initialization, or an unresolved low-level object method.

The distinction between **confirmed** and **inferred** is intentional.  For
example, GPIO187/188 and their written levels are confirmed from the SIU
descriptors, while their throttle-H-bridge board role is still an inference.

## Important startup messages

| Bus | Point in startup | Bytes/words sent | Meaning |
|---|---|---|---|
| DSPI-D | Initial Group 5 transfer | `6A 0C 00 00 00 00` | Base companion watchdog/service message |
| DSPI-D | Descriptor `0x157 <- 1` | `6A 2C 00 00 00 00` | Sets Group 5 word bit 5; likely starts/enables companion watchdog |
| DSPI-D | Descriptor `0x18A <- 1` | `80 FC 00 00 00 00` | Sets Group 6 word bit 2; confirmed ignition-output gate enable |
| DSPI-B | Identification, twice | `0E1B 0000` | Read ASIC silicon/revision class |
| DSPI-B | Main initialization | 19 words, revision dependent | Load ASIC configuration table |
| DSPI-B | Revision-3 prefix | `CF4C 25B7 16EC 0011` | Sent only for class 3 |
| DSPI-B | Later identification | `0E1B 0000` | Refresh and cache revision bits |
| DSPI-D | Later Group 4 transfer | `00 00 00 00 00 00` initially | 22-bit/request group; no startup bit is set here |

The application DSPI-D transaction objects clock six bytes.  Only the first
three are command bytes; the final three are clocks for the returned state.

## Linear top-level trace

1. `0x80018`: clear MSR external-interrupt enable (`wrteei 0`).
2. `0x80024`: call `InitializeApplicationHardwareAndSerialInterfaces`
   (`0xB78EC`).  Its complete expansion is in the next section.
3. `0x80028`: execute two `isync` instructions
   (`SynchronizeInstructionStreamAndReturn150`).
4. `0x80058`: synchronously send DSPI-D Group 5 using the application DMA
   context.  The newly copied base payload is `6A 0C 00 00 00 00`.
5. `0x80078`: set descriptor `0x157` to logical 1.  Descriptor decoding is:
   bank 0, active-high, Group 5, bit 5.  This changes `6A0C` to `6A2C` and
   immediately transmits `6A 2C 00 00 00 00`.
6. `0x8007C`: read the boot/runtime-start reason through `GetBootReasonCode`.
7. If that result is zero, initialize the five-object companion startup state:
   clear the retry flag, set phase 7 temporarily, sample the five control
   objects, then call `AdvanceCompanionStartupHandshake`.
8. Service `ServiceCompanionTransactionStateMachine` once.  Depending on the
   five sampled control states it selects phases 1, 3, or 5 and performs one or
   more DSPI-D startup transactions using the Group 6 buffer.  The state
   machine details are below.
9. Clear startup byte `0x40009A43`.
10. Read TBL and save it at `0x40009E28` as the startup timebase reference.
11. If cold/recovery startup or a persisted-clear request is active, clear
    `0x17AC` 32-bit words at `0x20000000` (6060 words / `0x5EB0` bytes).
12. Initialize the inter-core handshake.  Cold startup writes ASCII `RACK`
    (`0x5241434B`) to both handshake copies; warm startup preserves a valid
    `RACK`, `WACK`, `RREQ`, or `WREQ`, otherwise falls back to `RACK`.
13. Detect shared-output ownership:
    - On the cold path, delay 200 timebase units and read GPIO72.
    - Cache ownership state 0 when GPIO72 is high, otherwise state 1.
    - When required, enable the shared-output configuration drivers: set
      GPIO72 and GPIO73 logical high, then configure both as driven outputs.
    - Clear the persisted ownership-request flag.
14. If ownership state is 1 and no `RREQ`/`WREQ` is present, poll up to 50
    times, delaying 100 timebase units per poll, for an inter-core request.
15. Apply `RACK/RREQ/WACK/WREQ`:
    - disable external interrupts;
    - install the corresponding three TLB entries through
      `InstallIntercoreMemoryMapVariantA` or
      `InstallIntercoreMemoryMapVariantB`;
    - store the selected memory mode (0 or 1) at `0x40009AB8`;
    - restore external interrupts;
    - update the persistent and live handshake words and flush them.
16. Run `InitializeRetainedCompanionStatus`, the persistent
    companion/boot-status check:
    - on applicable boots, refresh companion descriptor `0xFE` and inspect it;
    - update persistent status 0/1/2;
    - configure the associated low-level field object;
    - delay `0x5C` timebase units;
    - read back and persist the resulting low-level value when enabled.
17. On a zero boot-reason result, clear persistent byte `0x400069E0` and flush.
18. Set runtime mode byte `0x40009E14` bits 1:0 to 1 and clear bits 3:2.
19. Initialize runtime timing fields:
    - `0x40009A0A = 40000`
    - `0x40009A0C = 0`
    - `0x40009A0E = 2`
    - `0x40009904 = 0`
    - `0x40009902 = 40000`
20. Drive GPIO187 low.
21. Drive GPIO188 low.
22. Configure the hardware field object at `0x400098F8`: select its normal
    path, then write value 0 with width 2 and mask `0xFFFF`.  This object is
    part of the timer/output initialization image; its exact board net is not
    encoded in the binary.
23. Initialize the eTPU hardware and channel objects (expanded below).
24. Read GPIO4 and place it in board-revision bit 0.
25. Read GPIO5 and place it in board-revision bit 1.
26. Read GPIO6 and place it in board-revision bit 2.
27. Read GPIO7 and place it in board-revision bit 3.
28. Store the packed revision nibble at `0x4000B202`.
29. Send DSPI-B selector 8, `0E1B 0000`, then cache `(RX[1] >> 12) & 7` at
    `0x4000B204`.
30. Initialize an eight-byte runtime record at `0x4000BE09` to
    `05 03 01 00 00 00 48 B8`.
31. Build DSPI-B selector-6 control word `0x3E00`: bit 8 is clear and bits
    9, 10, 11, 12, and 13 are set.  This step modifies the RAM message only;
    it does not transmit it.
32. Clear 48 channel runtime slots and their hardware status fields for one
    eTPU engine (`ResetETPUEngineAChannelRuntime`).
33. Clear 48 channel runtime slots and their hardware status fields for the
    second eTPU engine (`ResetETPUEngineBChannelRuntime`).
34. Execute two `isync` instructions.
35. Configure the revision-routed input:
    - board revisions `E`/`F`: configure/read GPIO89;
    - all other revisions: configure/read GPIO186;
    - cache its initial state at `0x40009133` and set initialized flag
      `0x40009134 = 1`.
36. Send DSPI-D Group 5 again, now using the `6A2C` first word.
37. Send DSPI-D Group 4.  No startup descriptor modifies it, so its initial
    command is six zero bytes.
38. Reset engine-position runtime:
    - set `0x4000A180 = 0xFFFFFFFF`;
    - clear `0x4000A1B9/BA`;
    - reset event `0x601` and the crank/cam position helpers;
    - copy the current reference time and initialize the remaining position
      state machines.
39. Initialize eleven additional eTPU/timed-channel runtime objects through
    `InitializeAdditionalETPUChannelRuntime`.  These calculate channel timing from the configured eTPU
    timebases and install each channel's mode/period values.
40. Set startup-ready byte `0x4000A9FF = 1`.
41. Apply actuator startup state 0:
    - set GPIO114 low;
    - clear the associated DSPI-B control-object states;
    - set GPIO182 low, GPIO191 low, GPIO159 low, GPIO180 low, and GPIO143 low;
    - place the associated eTPU output objects at zero duty/inactive state;
    - set the GPIO160 path high;
    - initialize/cache all corresponding command objects.
    The binary contains wrappers and cached-state logic around several writes,
    so repeated writes are intentional.
42. Request logical state 1 through the GPIO197 cached-output object
    (`SetGPIO197HighAtStartup -> SetGPIO197CommandStateCached`).
43. Execute two `isync` instructions.
44. Queue (asynchronous, rather than execute synchronously) the next DSPI-D
    Group 5 watchdog/service transaction.
45. Set bits `0xC000` in the low-level control register represented by
    `_DAT_C3F88000`, then run its completion/status helper
    (`WaitForSystemClockModeCompletion`).
46. On cold startup, repeatedly service the companion startup state machine
    until both conditions hold:
    - TBL has reached at least `0x14F000`; and
    - companion phase at `0x4000C5AC` is 7.
47. Set `0x4000A9FE = 4` and `0x4000B186 = 4`, then enable the associated
    runtime mode through `SetCompanionDescriptor19FState(1)`.
48. Enable external interrupts (`wrteei 1`).
49. Run the late timing/diagnostic initializer
    `InitializeLateMeasurementAndDiagnostics`:
    - snapshot the initial states of its GPIO/eTPU objects;
    - initialize the DSPI-B-dependent analog-front-end settings;
    - issue the DSPI-B selector-12 bulk diagnostic exchange (`835F` followed
      by eight zero words) through its queued transaction object;
    - initialize per-channel measurement and timing caches.
50. Select the application RAM/window mode from boot status:
    - mode 1: base 0, limit `0x1C000`, selector 3;
    - mode 2: base `0x1C000`, limit 0, selector 0;
    - persist which mode was selected.
51. Validate/refresh the retained boot block through
    `ValidateAndRecoverRetainedBootBlock`.  It saves
    fields, runs the retained-data validator, restores the fields, and retries
    with interrupts masked if the first validation path fails.
52. Perform the second-core handshake check in
    `SynchronizeSecondaryCoreStartupHandshake`: wait up to 30
    one-unit delays for `0x22000000`; on timeout call the recovery helper and
    store the observed handshake value.
53. Initialize the inter-core barrier object and call its reset method.
54. Disable external interrupts, execute five `isync`s, restore the saved MSR
    external-enable state, and execute a final `isync`.
55. Classify the hardware variant through
    `ClassifyHardwareVariantFromAnalogAndGPIO`:
    - acquire six analog channels and compare three against `0xF60` and three
      against `0xA0`;
    - when in range, write guard `0x12345678`, repeat acquisition, classify
      another analog channel plus GPIO203 and GPIO92, then write guard
      `0xEDCBA987`;
    - store the resulting class at `0x40009A62`.
56. Select final runtime table type 2 when classification succeeded, otherwise
    type 3.
57. Initialize the final interrupt/runtime dispatch tables with the selected
    type, install the appropriate table, and activate the pending interrupt
    dispatcher if required (`InitializeRuntimeInterruptDispatcher`).
58. Enter `EnterRuntimeInterruptDispatcherLoop`, which waits forever in the installed runtime.  The
    following fallback thunk also disables interrupts and loops forever and is
    not expected to return.

## Expansion: `InitializeApplicationHardwareAndSerialInterfaces` (`0xB78EC`)

This routine performs the early hardware setup before the top-level function
sends its first companion watchdog message.

1. Establish SDA bases used by the application: `r13 = 0x40008000`,
   `r14 = 0x40018000`, and `r2 = 0x00088210`.
2. Mask external interrupts and initialize the interrupt controller:
   - clear INTC configuration;
   - install the software-vector table pointer;
   - initialize eight priority bytes to 1;
   - copy 329 interrupt-priority entries from ROM;
   - clear current priority and restore the prior external-enable state.
3. Zero application RAM `0x40009100..0x4000E1FF` (`0x5100` bytes).
4. Zero `0x40009000..0x4000901F`.
5. Depending on reset/boot reason, clear retained block
   `0x400069A0..0x400069FF`; on the special retained-start path preserve its
   pointer, word, byte, and 12-byte record around the clear.
6. Copy `0x307` words (`0xC1C` bytes) of the ROM initialization image at
   `0x000D2400` into `0x40009100`.
7. Clear `0xB86` words (`0x2E18` bytes) beginning at `0x40009D20`.
8. Validate and, if necessary, reset the retained boot/status structure.  The
   reset value includes handshake `RACK` and initial status 2.
9. Install the application TLB entries selected by the 32-entry ROM table.
10. Initialize the system clock/PLL path and clear its `0xC000` mode bits.
11. Initialize SIU interrupt/mux support state.
12. Copy 214 bytes of SIU input-mux configuration into `0xC3F90600`.
13. Copy 231 halfwords of SIU PCR configuration into `0xC3F90040`.
14. Initialize the auxiliary timing/peripheral block at `0xC3F84000` from its
    eight ROM pairs.
15. Initialize eDMA at `0xFFF44000`: control `0x0000E400`, clear error and
    interrupt state, and clear channel state.
16. Set both low bits in hardware byte `0xFFF40043`.
17. Initialize/validate the main SRAM retained-data/ECC state, preserving the
    retained boot fields around the operation.
18. Initialize both eQADC modules:
    - stop/reset queues;
    - load command queue tables;
    - install DMA channel requests;
    - enable the configured queues;
    - set the shared control word to `0x81600000`.
19. Calibrate eQADC converter/configuration `0x0100000C`.
20. Calibrate eQADC converter/configuration `0x0100030C`.
21. Initialize eMIOS global control and all 24 channel register images from
    ROM; the special channel mode value `0x0B` is normalized explicitly.
22. Set hardware bit `0x0400` in `0xFFF80050`.
23. Initialize DSPI DMA context A from descriptor `0xB984C`.
24. Initialize DSPI DMA context B from descriptor `0xB986C`.
25. Initialize DSPI DMA context C from descriptor `0xB988C`.
26. Initialize DSPI DMA context D from descriptor `0xB98AC`.

Each DSPI context step clears its 48-byte state object, resolves its controller
base, builds RX and TX DMA descriptors, disables both DMA channels, initializes
the controller, clears its TX command ring, installs completion state, and
starts the controller.  Only B and D have live protocol traffic in this image.

27. Copy DSPI-D companion templates from ROM into RAM:
    - Group 5: `6A0C 0000 0000`
    - Group 4: `0000 0000 0000`
    - Group 6: `80F8 0000 0000`
28. Build selector 8 as `0E1B 0000`, send it, read revision bits 14:12, and
    copy a provisional 19-word revision table to selector 0 RAM.
29. Build and send selector 8 a second time; classify raw revision `< 4` as
    class 3 and all others as class 4, then copy the final selector-0 table.
30. Initialize selector 3 from its revision image (`0F1A` plus control word).
31. Initialize selector 7 to `0B01 0000 0000 1FC0`.
32. Initialize selector 2 opcode `0F19`.
33. Reinitialize selector 8 to `0E1B 0000`.
34. Copy all four selector-14 bank templates (`0804`, `0806`, `0808`,
    `080A`, each with two data words) into RAM.
35. Initialize selector 11 opcode `011F` for the rotating/protected query.
36. Initialize selector 1 to `0F17 0080 0080`.
37. Initialize selector 6 opcode `0F14`; its data word is filled later.
38. Initialize selector 9 opcode `0215` (the passed `0x26` is masked by the
    wrapper before choosing bank 0).
39. Initialize selector 10 opcode `0F12`.
40. Initialize selector 4 from the class-3 or class-4 ROM default (`0F1D`).
41. Install DSPI interrupt/DMA routing state for controller slots 0 and 2 and
    clear their pending callback fields.
42. Start those two DSPI controller slots.
43. Install the first DSPI-B callback-table entry.
44. Send DSPI-B selector 0, the 19-word ASIC initialization table:

    Class 4:
    `CF4C 25B7 16EC 0011 1E10 1E11 1331 7575 0055 3E00 0013 FB82 0080 0080 0A80 0000 0000 0000 00F0`

    Class 3:
    `CF4C 25B7 16EC 0011 1E10 1E11 1331 3030 0000 3E00 0013 FB82 0080 0080 0000 0000 0000 0000 0541`
45. If class 3, send selector 13 prefix `CF4C 25B7 16EC 0011`.
46. Set DSPI-D descriptor `0x18A` to 1.  It is bank 0, active-high, Group 6,
    bit 2; the payload changes from `80F8` to `80FC` and the routine sends
    `80 FC 00 00 00 00` immediately.

## Expansion: companion startup state machine

`InitializeCompanionInterfaceState` samples five control objects and stores the
states at `0x4000C5C8..CC`.  `AdvanceCompanionStartupHandshake` then chooses:

- Any of objects 2, 3, or 4 active: phase 1.
- Otherwise object 0 active: phase 3.
- Otherwise object 1 active: phase 5.
- Otherwise, if a retry was already requested, delay 4000, set all five
  control objects to 1, issue a DSPI-D startup transaction, and finish at
  phase 7.
- Otherwise remain idle until another service pass.

The service sequence uses an approximately `0x7D000`-tick deadline per phase:

1. Phase 1: drive objects 2..4 low, transact, drive them high, transact, start
   the deadline, clear their pending flags, advance to phase 2.
2. Phase 2: after the deadline, issue a startup transaction, sample objects
   2..4, clear any asserted object, transact again, and reevaluate the phase.
3. Phase 3/4: perform the same low/transact/high/transact/deadline/sample flow
   for object 0.
4. Phase 5/6: perform the same flow for object 1.
5. Completion: delay 4000, set all five objects high, perform the final startup
   transaction, and set phase 7.

These startup transfers use the application startup transaction object at
`0xBA284`, whose TX pointer is the Group 6 RAM buffer.  The five objects are
DSPI-D descriptors `0x18F`, `0x193`, `0x197`, `0x19B`, and `0x19F`: Group 6
bits 3 through 7.  The state machine therefore clears and restores those five
bits in the first word around each transfer.  With descriptor `0x18A` already
set, the fully asserted/restored command is `80 FC 00 00 00 00`; intermediate
startup transfers contain the corresponding subsets of bits 3..7.

## Expansion: eTPU and output initialization

`InitializeETPUChannelHardware` performs four layers:

1. Initialize the core eTPU engine object.
2. Install the individual channel objects from the ROM application image.
3. Calculate channel periods from a 32 MHz reference (16 MHz on the divide-by-1
   path) and write channel timing registers.
4. Enable the eTPU engine/channel interrupt/DMA support state.

The channel set includes the code-proven external arrays:

- GPIO132-139 / eTPU-A18-A25: eight injector timing outputs, function `0x19`.
- GPIO167-174 / eTPU-B20-B27: eight ignition timing outputs, function `0x17`.
- GPIO147-151 / eTPU-B0-B4: crank/cam position inputs.
- The additional pulse/frequency and special-event channels documented in
  `E78_CODE_VERIFIED_PIN_MAP.md`.

`InitializeActuatorOutputStartupStates(0)` is the explicit safe-state pass.  It
does not enable injector or ignition timing; it makes the discrete and timed
commands inactive, sets the GPIO160 selection path high, and initializes the
cached state wrappers.  The actual companion ignition path was already enabled
earlier by `80FC`.

## Final hardware-variant classification

The late `ClassifyHardwareVariantFromAnalogAndGPIO` call is separate from the
GPIO4-7 board-revision nibble.
It first validates six analog readings, then combines another analog reading
with GPIO203 and GPIO92 to produce a class value 0..6.  Its result controls
which final interrupt/runtime dispatch table (`type 2` or `type 3`) is
installed before startup transfers control to the permanent dispatcher loop.

## Ghidra naming status

Every function reached in the three-level `InitializeApplicationRuntime` call
graph has been assigned a non-default name in Ghidra.  This covers 498 call
edges, including the startup routine's direct callees, the expanded hardware
initializers, and their eTPU, SIU, DSPI, eDMA, EQADC, retained-state,
diagnostic, and compiler-runtime helpers.  Names remain implementation-level
where the binary proves a register or object operation but does not prove the
external board-net purpose.
