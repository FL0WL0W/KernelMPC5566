	.section .startup, "ax"
	.align 2
	.globl _start
	.type _start, @function
_start:
	wrteei	0

;# Initialize every GPR before any value can be spilled in lock-step mode.

;# r1 is deliberately preserved.  The resident bootloader constructed a
;# fully initialized cache-as-RAM stack at 0x60000000 and calls this entry as
;# a normal function.  Cold internal SRAM does not yet have valid ECC.
	li	r0, 0
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

;# Establish valid ECC across the complete application-side SRAM window.
;# This includes 0x40010000, where the RAM-download kernel is linked, so an
;# upload through UDS can write it without first entering BAM/boot-pin mode.
;# The resident bootloader's live 0x40000000..0x40007FFF window is preserved.
	lis	r5, __APPLICATION_SRAM_START@h
	ori	r5, r5, __APPLICATION_SRAM_START@l
	lis	r6, __APPLICATION_SRAM_END@h
	ori	r6, r6, __APPLICATION_SRAM_END@l
	bl	InitializeSramEccRange
	b	ApplicationSramEccInitialized

;# MPC5566 cold SRAM requires aligned 64-bit writes before smaller accesses.
;# stmw r30 writes the zeroed r30/r31 pair as one aligned 64-bit operation.
InitializeSramEccRange:
	subf	r9, r5, r6
	srwi	r9, r9, 3
	cmpwi	r9, 0
	beqlr
	mtctr	r9
InitializeApplicationSramEccLoop:
	stmw	r30, 0(r5)
	addi	r5, r5, 8
	bdnz	InitializeApplicationSramEccLoop
	blr
ApplicationSramEccInitialized:

;# SRAM now has valid ECC, so move off the bootloader's 16 KiB cache stack
;# and onto the application's dedicated SRAM stack before calling any C code.
	lis	r1, __SP_INIT@h
	ori	r1, r1, __SP_INIT@l

;# Copy ordinary initialized data from its flash load address to SRAM.
	lis	r4, __DATA_LOAD@h
	ori	r4, r4, __DATA_LOAD@l
	lis	r5, __DATA_START@h
	ori	r5, r5, __DATA_START@l
	lis	r9, __DATA_SIZE@h
	ori	r9, r9, __DATA_SIZE@l
	bl	CopyInitializedBytes

;# Copy the writable small-data section separately.
	lis	r4, __SDATA_LOAD@h
	ori	r4, r4, __SDATA_LOAD@l
	lis	r5, __SDATA_START__@h
	ori	r5, r5, __SDATA_START__@l
	lis	r9, __SDATA_SIZE@h
	ori	r9, r9, __SDATA_SIZE@l
	bl	CopyInitializedBytes

;# Clear BSS.
	lis	r5, __BSS_START@h
	ori	r5, r5, __BSS_START@l
	lis	r9, __BSS_SIZE@h
	ori	r9, r9, __BSS_SIZE@l
	cmpwi	r9, 0
	beq	BssInitialized
	mtctr	r9
	subi	r5, r5, 1
	li	r4, 0
ClearBssLoop:
	stbu	r4, 1(r5)
	bdnz	ClearBssLoop
BssInitialized:

;# Establish this image's EABI small-data bases.
	lis	r13, _SDA_BASE_@h
	ori	r13, r13, _SDA_BASE_@l
	lis	r2, _SDA2_BASE_@h
	ori	r2, r2, _SDA2_BASE_@l

	bl	main
ApplicationReturned:
	b	ApplicationReturned
	.size _start, .-_start

CopyInitializedBytes:
	cmpwi	r9, 0
	beqlr
	mtctr	r9
	subi	r4, r4, 1
	subi	r5, r5, 1
CopyInitializedBytesLoop:
	lbzu	r6, 1(r4)
	stbu	r6, 1(r5)
	bdnz	CopyInitializedBytesLoop
	blr

;# Application callbacks exported to the resident bootloader through the
;# pointer table at 0x002FFFE8.  These routines can be called before _start,
;# so they must not depend on the application's r2/r13, data, BSS, or heap.
	.section .boot_callback_code, "ax"
	.align 2

E78LoadBootParameterBlock:
	stwu	r1, -32(r1)
	mflr	r0
	stw	r0, 36(r1)
	stw	r30, 24(r1)
	stw	r31, 28(r1)

;# Match the stock application's preparation of 0x40000480..0x400008BF.
	lis	r3, 0x4000
	ori	r3, r3, 0x0480
	li	r30, 0
	li	r31, 0
	li	r5, 0x0088
	mtctr	r5
E78ClearBootParameterWorkspace:
	stmw	r30, 0(r3)
	addi	r3, r3, 8
	bdnz	E78ClearBootParameterWorkspace

;# LoadBootParameterBlock is resident in the preserved stock bootloader.
	lis	r12, 0x0000
	ori	r12, r12, 0x8B28
	lis	r3, 0x4000
	ori	r3, r3, 0x0480
	mtctr	r12
	bctrl

	lwz	r30, 24(r1)
	lwz	r31, 28(r1)
	lwz	r0, 36(r1)
	mtlr	r0
	addi	r1, r1, 32
	blr

	.globl E78BootValidateApplicationState
	.type E78BootValidateApplicationState, @function
E78BootValidateApplicationState:
	stwu	r1, -16(r1)
	mflr	r0
	stw	r0, 20(r1)
	bl	E78LoadBootParameterBlock

	lis	r4, 0x4000
	lhz	r5, 0x0824(r4)
	li	r3, 0
	cmpwi	r5, 0
	beq	E78BootValidationComplete
	cmplwi	r5, 0xFFFF
	beq	E78BootValidationComplete
	lhz	r6, 0x081C(r4)
	not	r6, r6
	clrlwi	r6, r6, 16
	cmpw	r5, r6
	beq	E78BootValidationComplete
	li	r3, 1
E78BootValidationComplete:
	lwz	r0, 20(r1)
	mtlr	r0
	addi	r1, r1, 16
	blr
	.size E78BootValidateApplicationState, .-E78BootValidateApplicationState

	.globl E78BootGetParameterBlock758
	.type E78BootGetParameterBlock758, @function
E78BootGetParameterBlock758:
	stwu	r1, -16(r1)
	mflr	r0
	stw	r0, 20(r1)
	bl	E78LoadBootParameterBlock
	lis	r3, 0x4000
	ori	r3, r3, 0x0758
	lwz	r0, 20(r1)
	mtlr	r0
	addi	r1, r1, 16
	blr
	.size E78BootGetParameterBlock758, .-E78BootGetParameterBlock758

	.globl E78BootGetApplicationIdentifier
	.type E78BootGetApplicationIdentifier, @function
E78BootGetApplicationIdentifier:
	lis	r3, E78ApplicationIdentifier@h
	ori	r3, r3, E78ApplicationIdentifier@l
	blr
	.size E78BootGetApplicationIdentifier, .-E78BootGetApplicationIdentifier

	.globl E78BootGetParameterBlock754
	.type E78BootGetParameterBlock754, @function
E78BootGetParameterBlock754:
	stwu	r1, -16(r1)
	mflr	r0
	stw	r0, 20(r1)
	bl	E78LoadBootParameterBlock
	lis	r3, 0x4000
	ori	r3, r3, 0x0754
	lwz	r0, 20(r1)
	mtlr	r0
	addi	r1, r1, 16
	blr
	.size E78BootGetParameterBlock754, .-E78BootGetParameterBlock754

	.align 2
E78ApplicationIdentifier:
;# The four bytes returned by the stock E78 application for boot service 1A C1.
	.byte 0x00, 0xC1, 0x52, 0x45

;# No-return transition back into the resident secondary bootloader.
	.section .text_booke, "ax"
	.align 2
	.globl ExitToBootloaderUploadRoutine
	.type ExitToBootloaderUploadRoutine, @function
ExitToBootloaderUploadRoutine:
	wrteei	0
;# Discard the application's SRAM call stack and restore the locked
;# cache-as-RAM stack used by the stock bootloader/application handoff.
;# EnterSecondaryBootloaderMode clears 0x40000400..0x4001BFFF, which includes
;# our normal 0x40008000..0x4000FCFF runtime stack.
	lis	r1, 0x6000
	ori	r1, r1, 0x3FF0
	lis	r12, 0x0002
	lwz	r12, -0x5060(r12)
	mtctr	r12
	bctr
	.size ExitToBootloaderUploadRoutine, .-ExitToBootloaderUploadRoutine
