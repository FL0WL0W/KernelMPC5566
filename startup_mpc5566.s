
	.section .startup, "ax"
        .globl	_start
_start:
		nop


;#**************************** Init Core Registers ****************************
;# The E200Z4 core needs its registers initialising before they are used
;# otherwise in Lock Step mode the two cores will contain different random data.
;# If this is stored to memory (e.g. stacked) it will cause a Lock Step error.

;# GPRs 0-31
	li	r0, 0
	li	r1, 0
	li	r2, 0
	li	r3, 0
	li	r4, 0
	li	r5, 0
	li	r6, 0
	li	r7, 0
	li	r8, 0
	li	r9, 0
	li	r10, 0
	li	r11, 0
	li	r12, 0
	li	r13, 0
	li	r14, 0
	li	r15, 0
	li	r16, 0
	li	r17, 0
	li	r18, 0
	li	r19, 0
	li	r20, 0
	li	r21, 0
	li	r22, 0
	li	r23, 0
	li	r24, 0
	li	r25, 0
	li	r26, 0
	li	r27, 0
	li	r28, 0
	li	r29, 0
	li	r30, 0
	li	r31, 0

;#****************************** Initialize BSS section ******************************/
bss_Init:
    lis        r9, __BSS_SIZE@h       # Load upper BSS load size (# of bytes) into R9
    ori      r9, r9, __BSS_SIZE@l       # Load lower BSS load size into R9 and compare to zero
    cmpwi     r9,0
    beq        bss_Init_end           # Exit if size is zero (no data to initialise)

    mtctr        r9                     # Store no. of bytes to be moved in counter

    lis        r5, __BSS_START@h      # Load upper BSS address into R5 (from linker file)
    ori      r5, r5, __BSS_START@l      # Load lower BSS address into R5 (from linker file)
    subi       r5, r5, 1              # Decrement address to prepare for bss_Init_loop

    lis        r4, 0x0

bss_Init_loop:
    stbu       r4, 1(r5)              # Store zero byte into BSS at R5 and update BSS address
    bdnz       bss_Init_loop          # Branch if more bytes to load

bss_Init_end:

;#****************************** Configure Stack ******************************/
	lis	r1, __SP_INIT@h	;# Initialize stack pointer r1 to
	ori	r1, r1, __SP_INIT@l	;# value in linker command file.

	lis	r13, _SDA_BASE_@h	;# Initialize r13 to sdata base
	ori	r13, r13,  _SDA_BASE_@l	;# (provided by linker).

	lis	r2, _SDA2_BASE_@h	;# Initialize r2 to sdata2 base
	ori	r2, r2, _SDA2_BASE_@l	;# (provided by linker).

	stwu	r0,-64(r1)			;# Terminate stack.

;# Jump to Main
	bl	main

;# Enter the flash-resident secondary bootloader without retaining any kernel
;# call frames.  The bootloader clears 0x40000400-0x4001BFFF during entry, so
;# the kernel's normal stack at 0x4000FD00 cannot be used for this transition.
	.section .text_booke, "ax"
	.align 2
	.globl ExitToBootloaderUploadRoutine
	.type ExitToBootloaderUploadRoutine, @function
ExitToBootloaderUploadRoutine:
	wrteei	0
;# Use the bootloader's locked cache-as-RAM stack.  Its secondary-mode entry
;# clears 0x40000400..0x4001BFFF, so no ordinary SRAM stack is safe here.
	lis	r1, 0x6000
	ori	r1, r1, 0x3FF0
	lis	r12, 0x0002
	lwz	r12, -0x5060(r12)
	mtctr	r12
	bctr
	.size ExitToBootloaderUploadRoutine, .-ExitToBootloaderUploadRoutine
