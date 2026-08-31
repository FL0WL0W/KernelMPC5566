# E78 flash application build

The flash build is separate from the RAM download kernel. It matches the
resident E78 bootloader's application contract:

- image base: `0x00080000`
- header: `AA55` followed by fourteen `FF` bytes
- executable entry: `0x00080010`
- initialized data and BSS: `0x40008000` upward
- pre-entry callback stack: the resident bootloader's initialized
  cache-as-RAM stack in `0x60000000..0x60003FFF`
- application runtime heap/stack: the same `0x40008000..0x4000FFFF` window
  used by the proven RAM-download kernel

The startup procedure disables external interrupts while preserving the
bootloader-provided r1 and establishes valid ECC with aligned 64-bit writes
across `0x40008000..0x4001FFFF`. This includes the RAM kernel's preferred load
address at `0x40010000`, allowing the running flash application to receive a
new RAM kernel without a BAM/boot-pin cycle. The live resident-bootloader
window at `0x40000000..0x40007FFF` remains untouched. Startup then copies
`.data` and `.sdata` from flash, clears BSS, installs the EABI small-data bases,
and calls `main`. Only after the SRAM ECC pass does startup move r1 from the
bootloader's cache-as-RAM stack to the application SRAM stack.

Build it with:

```sh
cmake --preset MPC5566-Flash-Release
cmake --build build/MPC5566-Flash-Release
```

Outputs are written to:

- `build/Kernel-E78-Flash.bin` — raw bytes beginning at flash address
  `0x00080000`
- `build/Kernel-E78-Flash-Full.bin` — complete `0x300000`-byte flash image
  for whole-device writers. It contains the stock bootloader from
  `bootloader.bin` at `0x00000000`, the new application at `0x00080000`, and
  erased `FF` padding through `0x002FFFFF`, except for the required application
  validity marker `55AA` at `0x002FFFF8`.
- `build/Kernel-E78-Flash.hex` — Intel HEX containing absolute addresses
- `build/MPC5566-Flash-Release/firmware.elf` — symbols and load/run addresses

`bootloader.bin` is exactly the first `0x80000` bytes of the stock E78 image.
The full image therefore preserves the resident bootloader while replacing
the complete application region.

At normal startup, the resident bootloader checks the `AA55` marker at
`0x00080000` and the `55AA` marker at `0x002FFFF8`. It does not calculate a
checksum across the complete application. The bootloader does perform a
16-bit additive, zero-sum check over ranges supplied through its diagnostic
download protocol, but that transfer-time check is not used when an external
tool writes the complete raw flash image.

The bootloader also treats the final 24 bytes as an application ABI:

- `0x002FFFE8`: boot-state validation callback
- `0x002FFFEC`: boot-parameter block `0x40000758` callback
- `0x002FFFF0`: application identification callback
- `0x002FFFF4`: boot-parameter block `0x40000754` callback
- `0x002FFFF8`: `55AA` validity marker

The callbacks are implemented without dependencies on the application C
runtime because the bootloader can invoke them before the application entry
at `0x00080010`. Their boot-parameter workspace is also initialized with
aligned 64-bit writes so the callbacks are safe immediately after power-on.

When entering programming mode, the application restores r1 to the locked
cache-as-RAM stack at `0x60003FF0` before branching to the resident
`EnterSecondaryBootloaderMode` entry. That bootloader path clears
`0x40000400..0x4001BFFF`; using the application's normal SRAM stack during the
handoff would erase the bootloader's active frames and cause an immediate
reset back into the application.

The original `MPC5566-Release` preset and `Kernel-E78.bin` RAM image remain
independent.
