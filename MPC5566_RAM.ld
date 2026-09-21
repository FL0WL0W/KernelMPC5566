/* Entry Point */
ENTRY(_start)

/* define heap and stack size */
__HEAP_SIZE            = 24000 ;
__STACK_SIZE           = 16000 ;
    
MEMORY
{
    /*
     * Keep the E78 application's fixed small-data windows available for
     * calls into the untouched application image:
     *
     *   r13 base / initialized state: 0x40008000
     *   r14 base:                     0x40018000
     *
     * Kernel code fits below the r14 window.  Kernel writable state lives in
     * the otherwise separate 0x40020000 window so the application RAM image
     * at 0x40009100..0x4000E1FF can be reconstructed later.
     */
    SRAM_CODE : org = 0x40014000, len = 48K
    SRAM      : org = 0x40008000, len = 48K
}

SECTIONS
{
    /* The kernel never returns or performs C++ runtime teardown. */
    /DISCARD/ :
    {
      *(.dtors)
      *(.dtors.*)
      *(.init)
      *(.init.*)
      *(.fini)
      *(.fini.*)
      *(.fini_array)
      *(.fini_array.*)
      *(.eh_frame)
      *(.eh_frame.*)
      *(.eh_frame_hdr)
    }

    . = ORIGIN(SRAM_CODE);
    
    .startup ALIGN(4) :
    {
    __start = . ;
    	*(.startup)
    } > SRAM_CODE
    
    .text_booke :
    { 
      *(.text*)    
    } > SRAM_CODE            
           
    .ctors :
    {
      __CTOR_LIST__ = .;
      KEEP (*(SORT(.ctors.*)))
      KEEP (*(.ctors))
      __CTOR_END__ = .;
    } > SRAM_CODE
        
    .preinit_array :
    {
      PROVIDE_HIDDEN (__preinit_array_start = .);
      KEEP (*(.preinit_array*))
      PROVIDE_HIDDEN (__preinit_array_end = .);
    } > SRAM_CODE

    .init_array :
    {
      PROVIDE_HIDDEN (__init_array_start = .);
      KEEP (*(SORT(.init_array.*)))
      KEEP (*(.init_array*))
      PROVIDE_HIDDEN (__init_array_end = .);
    } > SRAM_CODE

    .rodata :
    {
      *(.rodata)
      *(.rodata.*)
    } > SRAM_CODE
    
    .data   :
    {
      *(.data)
      *(.data.*)
    } > SRAM_CODE
    
    .sdata2  :
    {
      __SDATA2_START__ = .;
      *(.sdata2)
      *(.sdata2.*)
    } > SRAM_CODE

    .sbss2    (NOLOAD)   :
    {
      /* _SDA2_BASE_ = .; */ 
      *(.sbss2)
      *(.sbss2.*)
      __SBSS2_END__ = .;
    } > SRAM_CODE
    
    .sdata  :
    {
      __SDATA_START__ = .;
      *(.sdata)
      *(.sdata.*)
    } > SRAM_CODE
    
    .bss   (NOLOAD)  :
    {
      __BSS_START = .;
      *(.sbss)
      *(.sbss.*)	
      __SBSS_END__ = .;
      *(.bss)
      *(.bss.*)
      *(COMMON)
      __BSS_END = .;
    } > SRAM_CODE

    .heap (NOLOAD) : ALIGN(16)
    {
      __HEAP = . ;
      PROVIDE (_end = . );
      PROVIDE (end = . );
      . += __HEAP_SIZE ;
      __HEAP_END = . ;
    } > SRAM
    
    .stack ORIGIN(SRAM) + LENGTH(SRAM) - __STACK_SIZE (NOLOAD) :
    {
      _stack_end = . ;
      . +=  __STACK_SIZE ;
      _stack_addr = . ;
      __SP_INIT = . ;
    } > SRAM
 
/*-------- LABELS USED IN CODE -------------------------------*/
     
__BSS_SIZE    = __BSS_END - __BSS_START;

}
