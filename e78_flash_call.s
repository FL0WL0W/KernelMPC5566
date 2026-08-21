	.section .text_booke, "ax"

	/*
	 * Call gates for selected routines in the untouched E78 application.
	 *
	 * The kernel and application use different small-data bases.  Preserve the
	 * kernel's ABI state, install the application bases for the duration of the
	 * call, then restore the kernel before returning.
	 */

	.globl E78FlashCall0
E78FlashCall0:
	stwu	r1, -32(r1)
	mflr	r0
	stw	r0, 28(r1)
	stw	r2, 8(r1)
	stw	r13, 12(r1)
	stw	r14, 16(r1)
	mr	r12, r3
	b	E78FlashCallCommon

	.globl E78FlashCall1
E78FlashCall1:
	stwu	r1, -32(r1)
	mflr	r0
	stw	r0, 28(r1)
	stw	r2, 8(r1)
	stw	r13, 12(r1)
	stw	r14, 16(r1)
	mr	r12, r3
	mr	r3, r4
	b	E78FlashCallCommon

	.globl E78FlashCall2
E78FlashCall2:
	stwu	r1, -32(r1)
	mflr	r0
	stw	r0, 28(r1)
	stw	r2, 8(r1)
	stw	r13, 12(r1)
	stw	r14, 16(r1)
	mr	r12, r3
	mr	r3, r4
	mr	r4, r5

E78FlashCallCommon:
	lis	r13, 0x4000
	ori	r13, r13, 0x8000
	lis	r14, 0x4001
	ori	r14, r14, 0x8000
	lis	r2, 0x0008
	ori	r2, r2, 0x8210
	mtctr	r12
	bctrl
	lwz	r2, 8(r1)
	lwz	r13, 12(r1)
	lwz	r14, 16(r1)
	lwz	r0, 28(r1)
	mtlr	r0
	addi	r1, r1, 32
	blr
