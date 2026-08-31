/* E78 flash application image.  The resident bootloader calls 0x00080010. */
ENTRY(_start)

__HEAP_SIZE  = 16000;
__STACK_SIZE = 16000;

MEMORY
{
	/* MPC5566 flash ends at 0x002FFFFF. */
	FLASH (rx)  : ORIGIN = 0x00080000, LENGTH = 0x00280000
	/* Match the SRAM windows used by the proven RAM-download kernel. */
	SRAM_RUNTIME (rwx) : ORIGIN = 0x40008000, LENGTH = 0x00008000
	SRAM_DATA    (rwx) : ORIGIN = 0x40016D10, LENGTH = 0x000052F0
}

__APPLICATION_RUNTIME_SRAM_START = ORIGIN(SRAM_RUNTIME);
__APPLICATION_RUNTIME_SRAM_END = ORIGIN(SRAM_RUNTIME) + LENGTH(SRAM_RUNTIME);
__APPLICATION_DATA_SRAM_START = ORIGIN(SRAM_DATA);
/* Complete SRAM available to the application above the bootloader window. */
__APPLICATION_SRAM_START = 0x40008000;
__APPLICATION_SRAM_END = 0x40020000;

SECTIONS
{
	.application_header ORIGIN(FLASH) :
	{
		/* Stock E78 layout: AA55 followed by fourteen erased bytes. */
		SHORT(0xAA55)
		SHORT(0xFFFF)
		SHORT(0xFFFF)
		SHORT(0xFFFF)
		SHORT(0xFFFF)
		SHORT(0xFFFF)
		SHORT(0xFFFF)
		SHORT(0xFFFF)
	} > FLASH

	.startup 0x00080010 : ALIGN(4)
	{
		KEEP(*(.startup))
	} > FLASH

	.boot_callback_code : ALIGN(4)
	{
		KEEP(*(.boot_callback_code))
	} > FLASH

	.text_booke : ALIGN(4)
	{
		*(.text*)
		KEEP(*(.init))
		KEEP(*(.fini))
	} > FLASH

	.ctors : ALIGN(4)
	{
		__CTOR_LIST__ = .;
		KEEP(*crtbegin.o(.ctors))
		KEEP(*crtbegin?.o(.ctors))
		KEEP(*(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors))
		KEEP(*(SORT(.ctors.*)))
		KEEP(*(.ctors))
		__CTOR_END__ = .;
	} > FLASH

	.dtors : ALIGN(4)
	{
		__DTOR_LIST__ = .;
		KEEP(*crtbegin.o(.dtors))
		KEEP(*crtbegin?.o(.dtors))
		KEEP(*(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors))
		KEEP(*(SORT(.dtors.*)))
		KEEP(*(.dtors))
		__DTOR_END__ = .;
	} > FLASH

	.preinit_array : ALIGN(4)
	{
		PROVIDE_HIDDEN(__preinit_array_start = .);
		KEEP(*(.preinit_array*))
		PROVIDE_HIDDEN(__preinit_array_end = .);
	} > FLASH

	.init_array : ALIGN(4)
	{
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array*))
		PROVIDE_HIDDEN(__init_array_end = .);
	} > FLASH

	.fini_array : ALIGN(4)
	{
		PROVIDE_HIDDEN(__fini_array_start = .);
		KEEP(*(SORT(.fini_array.*)))
		KEEP(*(.fini_array*))
		PROVIDE_HIDDEN(__fini_array_end = .);
	} > FLASH

	.rodata : ALIGN(8)
	{
		*(.rodata)
		*(.rodata.*)
	} > FLASH

	.sdata2 : ALIGN(4)
	{
		__SDATA2_START__ = .;
		*(.sdata2)
		*(.sdata2.*)
		*(.sbss2)
		*(.sbss2.*)
	} > FLASH
	PROVIDE(_SDA2_BASE_ = ADDR(.sdata2) + 0x8000);

	.eh_frame_hdr : ALIGN(4) { *(.eh_frame_hdr) } > FLASH
	.eh_frame : ALIGN(4) { KEEP(*(.eh_frame)) } > FLASH

	.data : ALIGN(8)
	{
		__DATA_START = .;
		*(.data)
		*(.data.*)
		*(.got2)
		*(.got2.*)
		__DATA_END = .;
	} > SRAM_DATA AT> FLASH
	__DATA_LOAD = LOADADDR(.data);
	__DATA_SIZE = SIZEOF(.data);

	.sdata : ALIGN(4)
	{
		__SDATA_START__ = .;
		*(.sdata)
		*(.sdata.*)
	} > SRAM_DATA AT> FLASH
	__SDATA_LOAD = LOADADDR(.sdata);
	__SDATA_SIZE = SIZEOF(.sdata);
	PROVIDE(_SDA_BASE_ = ADDR(.sdata) + 0x8000);

	.bss (NOLOAD) : ALIGN(8)
	{
		__BSS_START = .;
		*(.sbss)
		*(.sbss.*)
		*(.bss)
		*(.bss.*)
		*(COMMON)
		__BSS_END = .;
	} > SRAM_DATA
	__BSS_SIZE = SIZEOF(.bss);
	__APPLICATION_DATA_SRAM_END = ALIGN(__BSS_END, 8);

	.heap ORIGIN(SRAM_RUNTIME) (NOLOAD) : ALIGN(16)
	{
		__HEAP = .;
		PROVIDE(_end = .);
		PROVIDE(end = .);
		. += __HEAP_SIZE;
		__HEAP_END = .;
	} > SRAM_RUNTIME

	.stack (NOLOAD) : ALIGN(16)
	{
		__STACK_BOTTOM = .;
		. += __STACK_SIZE;
		__SP_INIT = .;
	} > SRAM_RUNTIME

	/* ABI exported by the application to the resident E78 bootloader. */
	.application_exports 0x002FFFE8 :
	{
		LONG(E78BootValidateApplicationState)
		LONG(E78BootGetParameterBlock758)
		LONG(E78BootGetApplicationIdentifier)
		LONG(E78BootGetParameterBlock754)
		SHORT(0x55AA)
		BYTE(0xFF)
		BYTE(0xFF)
		BYTE(0xFF)
		BYTE(0xFF)
		BYTE(0xFF)
		BYTE(0xFF)
	} > FLASH

	ASSERT(__SP_INIT <= ORIGIN(SRAM_RUNTIME) + LENGTH(SRAM_RUNTIME), "runtime heap/stack exceeds working SRAM window")
	ASSERT(__APPLICATION_DATA_SRAM_END <= ORIGIN(SRAM_DATA) + LENGTH(SRAM_DATA), "application data exceeds working SRAM window")
	ASSERT(ADDR(.startup) == 0x00080010, "bootloader application entry moved")
	ASSERT(ADDR(.application_exports) == 0x002FFFE8, "bootloader export table moved")
	ASSERT(SIZEOF(.application_exports) == 0x18, "bootloader export table has wrong size")
}
