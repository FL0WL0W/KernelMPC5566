/* Entry Point */
ENTRY(_start)

/* define heap and stack size */
__HEAP_SIZE            = 16000 ;
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
    SRAM_CODE : org = 0x40010000, len = 64K
    SRAM      : org = 0x40008000, len = 32K
}

SECTIONS
{
    . = ORIGIN(SRAM_CODE);
    
    .startup ALIGN(4) :
    {
    __start = . ;
    	*(.startup)
    } > SRAM_CODE
    
    .text_booke :
    { 
      *(.text*)    
      KEEP (*(.init))
      KEEP (*(.fini))    
    } > SRAM_CODE            
           
    .ctors :
    {
      __CTOR_LIST__ = .;
      /* gcc uses crtbegin.o to find the start of
         the constructors, so we make sure it is
         first.  Because this is a wildcard, it
         doesn't matter if the user does not
         actually link against crtbegin.o; the
         linker won't look for a file to match a
         wildcard.  The wildcard also means that it
         doesn't matter which directory crtbegin.o
         is in.  */
      KEEP (*crtbegin.o(.ctors))
      KEEP (*crtbegin?.o(.ctors))
      /* We don't want to include the .ctor section from
         from the crtend.o file until after the sorted ctors.
         The .ctor section from the crtend file contains the
         end of ctors marker and it must be last */
      KEEP (*(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors))
      KEEP (*(SORT(.ctors.*)))
      KEEP (*(.ctors))
      __CTOR_END__ = .;
    } > SRAM_CODE

    .dtors :
    {
      __DTOR_LIST__ = .;
      KEEP (*crtbegin.o(.dtors))
      KEEP (*crtbegin?.o(.dtors))
      KEEP (*(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors))
      KEEP (*(SORT(.dtors.*)))
      KEEP (*(.dtors))
      __DTOR_END__ = .;
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

    .fini_array :
    {
      PROVIDE_HIDDEN (__fini_array_start = .);
      KEEP (*(SORT(.fini_array.*)))
      KEEP (*(.fini_array*))
      PROVIDE_HIDDEN (__fini_array_end = .);
    } > SRAM_CODE
        
    .rodata :
    {
      *(.rodata)
      *(.rodata.*)
    } > SRAM_CODE
    
    .eh_frame_hdr : { *(.eh_frame_hdr) } > SRAM_CODE
    .eh_frame     : { KEEP (*(.eh_frame)) } > SRAM_CODE
    
    .data   :
    {
      *(.data)
      *(.data.*)
    } > SRAM_CODE
    
    .sdata2  :
    {  
      *(.sdata2)
      *(.sdata2.*)
    } > SRAM_CODE

    .sbss2    (NOLOAD)   :
    {
      /* _SDA2_BASE_ = .; */ 
      *(.sbss2)
      *(.sbss2.*)
    } > SRAM_CODE
    
    .sdata  :
    {
      *(.sdata)
      *(.sdata.*)
    } > SRAM_CODE
    
    .bss   (NOLOAD)  :
    {
      __BSS_START = .;
      *(.sbss)
      *(.sbss.*)	
      *(.bss)
      *(.bss.*)
      *(COMMON)
      __BSS_END = .;
    } > SRAM_CODE
    
    .stack (NOLOAD) : ALIGN(16) 
    {
      __HEAP = . ;
      PROVIDE (_end = . );
      PROVIDE (end = . );            
      . += __HEAP_SIZE ;
      __HEAP_END = . ;
      _stack_end = . ;
      . +=  __STACK_SIZE ;
      _stack_addr = . ;
      __SP_INIT = . ;
      . += 4;
    } > SRAM
 
/*-------- LABELS USED IN CODE -------------------------------*/
     
__BSS_SIZE    = __BSS_END - __BSS_START;

}
