
	.section .startup, "ax"
        .globl	_start
_start:
		nop

;#****************************** Turn off SWT ********************************
	lis	r4, 0xFFF3
	ori	r4, r4, 0x8000

	lis	r3, 0xC520
	stw	r3, 0x10(r4)

	lis	r3, 0xD928
	stw	r3, 0x10(r4)

	lis	r3, 0xFF00
	ori r3,	r3, 0x010A
	stw	r3, 0(r4)

;#********************************* Enable BTB ********************************
;# Flush & Enable BTB - Set BBFI bit in BUCSR
	li	r3, 0x201
	mtspr	1013, r3
	isync

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

;# Init any other CPU register which might be stacked (before being used).

	mtspr	1, r1		;#XER
	mtcrf   0xFF, r1
	mtspr   CTR, r1
	mtspr	272, r1		;#SPRG0
	mtspr	273, r1		;#SPRG1
	mtspr	274, r1		;#SPRG2
	mtspr	275, r1		;#SPRG3
	mtspr	58, r1		;#CSRR0
	mtspr	59, r1		;#CSRR1
	mtspr	570, r1		;#MCSRR0
	mtspr	571, r1		;#MCSRR1
	mtspr	61, r1		;#DEAR
	mtspr	63, r1		;#IVPR
	mtspr	256, r1		;#USPRG0
	mtspr	62, r1		;#ESR
	mtspr	8, r31		;#LR

;#*************************** Enable ME Bit in MSR *****************************
	mfmsr	r6
	ori      r6, r6,0x1000
	mtmsr	r6

#if defined(SPE_ENABLE)
;#*************************** Enable SPE Bit in MSR *****************************
		mfmsr r3
		ori      r3, r3,0x0200
		mtmsr r3
#endif

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

;#***************************** DISABLE INTERRUPTS ****************************/
	wrteei 0

;#********************************* INITIVORS *********************************/
	lis		r4, VTABLE@ha
	ori		r4, r4, VTABLE@l
	rlwinm 	r4, r4, 0, 16, 27
	mtspr	400, r4
	addi	r4, r4, 0x10
	mtspr	401, r4
	addi	r4, r4, 0x10
	mtspr	402, r4
	addi	r4, r4, 0x10
	mtspr	403, r4
	addi	r4, r4, 0x10
	mtspr	404, r4
	addi	r4, r4, 0x10
	mtspr	405, r4
	addi	r4, r4, 0x10
	mtspr	406, r4
	addi	r4, r4, 0x10
	mtspr	407, r4
	addi	r4, r4, 0x10
	mtspr	408, r4
	addi	r4, r4, 0x10
	mtspr	409, r4
	addi	r4, r4, 0x10
	mtspr	410, r4
	addi	r4, r4, 0x10
	mtspr	411, r4
	addi	r4, r4, 0x10
	mtspr	412, r4
	addi	r4, r4, 0x10
	mtspr	413, r4
	addi	r4, r4, 0x10
	mtspr	414, r4
	addi	r4, r4, 0x10
	mtspr	415, r4

;#********************************* INITINTC  *********************************/
    lis		r4, INTCVTABLE@ha
    ori		r4, r4, INTCVTABLE@l
	;# set IVPR
	mtspr	63, r4
    
	;# INTC base address 0xFFF48000
    lis		r5, 0xFFF4
    ori		r5, r5, 0x8000
    
    ;# Configure INTC_MCR (Module Control Register at offset 0x00)
    ;# Set VTES bit (Vector Table Entry Size) for hardware vector mode
    lwz		r3, 0(r5)
    ori		r3, r3, 0x0001		;# Set HVEN (Hardware Vector Enable) or appropriate bit
    stw		r3, 0(r5)

	;# Set INTC Priority to a lower level than exceptions
    ;# INTC_CPR at offset 0x08 - Current Priority Register
    lis		r3, 0x0000
    stw		r3, 0x08(r5)	;# Set priority to 0 (lowest)

;# Jump to Main
	bl	main

Default_Handler:
	b  Default_Handler

	.macro set_weak_default name
	.weak \name
	.set \name,Default_Handler
	.endm

.macro intc_wrapper wrapper, body
	.globl \wrapper
	.type  \wrapper, @function
\wrapper:
	stwu  r1,-80(r1)

	stw   r0,  8(r1)
	stw   r3, 12(r1)
	stw   r4, 16(r1)
	stw   r5, 20(r1)
	stw   r6, 24(r1)
	stw   r7, 28(r1)
	stw   r8, 32(r1)
	stw   r9, 36(r1)
	stw   r10,40(r1)
	stw   r11,44(r1)
	stw   r12,48(r1)

	mflr  r0
	stw   r0,52(r1)
	mfcr  r0
	stw   r0,56(r1)
	mfxer r0
	stw   r0,60(r1)
	mfctr r0
	stw   r0,64(r1)
	mfspr r0,26          /* SRR0: interrupted instruction address */
	stw   r0,68(r1)
	mfspr r0,27          /* SRR1: interrupted machine state */
	stw   r0,72(r1)

	/* Allow a higher-priority INTC source to preempt this handler only after
	 * the interrupted context has been completely saved. INTC.CPR prevents
	 * equal- and lower-priority sources from nesting. */
	wrteei 1
	isync

	bl    \body

	/* Close the nesting window before acknowledging the interrupt and
	 * restoring the interrupted context. */
	wrteei 0
	isync

	/* Peripheral request must be cleared by body before EOIR. */
	mbar  0

	lis   r4,0xFFF4
	ori   r4,r4,0x8018
	li    r3,0
	stw   r3,0(r4)       /* INTC.EOIR = 0 */

	lwz   r0,64(r1)
	mtctr r0
	lwz   r0,60(r1)
	mtxer r0
	lwz   r0,56(r1)
	mtcrf 0xff,r0
	lwz   r0,52(r1)
	mtlr  r0

	lwz   r3,12(r1)
	lwz   r4,16(r1)
	lwz   r5,20(r1)
	lwz   r6,24(r1)
	lwz   r7,28(r1)
	lwz   r8,32(r1)
	lwz   r9,36(r1)
	lwz   r10,40(r1)
	lwz   r11,44(r1)
	lwz   r12,48(r1)
	lwz   r0,68(r1)
	mtspr 26,r0
	lwz   r0,72(r1)
	mtspr 27,r0
	lwz   r0,8(r1)
	addi  r1,r1,80
	rfi
.endm

	.globl IVOR0_Vector
	.globl IVOR1_Vector
	.globl IVOR2_Vector
	.globl IVOR3_Vector
	.globl IVOR4_Vector
	.globl IVOR5_Vector
	.globl IVOR6_Vector
	.globl IVOR7_Vector
	.globl IVOR8_Vector
	.globl IVOR9_Vector
	.globl IVOR10_Vector
	.globl IVOR11_Vector
	.globl IVOR12_Vector
	.globl IVOR13_Vector
	.globl IVOR14_Vector
	.globl IVOR15_Vector

	set_weak_default	IVOR0_Vector
	set_weak_default	IVOR1_Vector
	set_weak_default	IVOR2_Vector
	set_weak_default	IVOR3_Vector
	set_weak_default	IVOR4_Vector
	set_weak_default	IVOR5_Vector
	set_weak_default	IVOR6_Vector
	set_weak_default	IVOR7_Vector
	set_weak_default	IVOR8_Vector
	set_weak_default	IVOR9_Vector
	set_weak_default	IVOR10_Vector
	set_weak_default	IVOR11_Vector
	set_weak_default	IVOR12_Vector
	set_weak_default	IVOR13_Vector
	set_weak_default	IVOR14_Vector
	set_weak_default	IVOR15_Vector

	.globl CAN_A_RXFIFO_Handler
	.globl CAN_B_RXFIFO_Handler
	.globl CAN_C_RXFIFO_Handler
	.globl CAN_D_RXFIFO_Handler
	.globl EMIOS_11_Handler

	set_weak_default	CAN_A_RXFIFO_Handler
	set_weak_default	CAN_B_RXFIFO_Handler
	set_weak_default	CAN_C_RXFIFO_Handler
	set_weak_default	CAN_D_RXFIFO_Handler
	set_weak_default	EMIOS_11_Handler

	intc_wrapper CAN_A_RXFIFO_ISR, CAN_A_RXFIFO_Handler
	intc_wrapper CAN_B_RXFIFO_ISR, CAN_B_RXFIFO_Handler
	intc_wrapper CAN_C_RXFIFO_ISR, CAN_C_RXFIFO_Handler
	intc_wrapper CAN_D_RXFIFO_ISR, CAN_D_RXFIFO_Handler
	intc_wrapper EMIOS_11_ISR, EMIOS_11_Handler

.macro align_and_branch handler
    .balign 0x10
    b \handler
.endm

	.section .core_exceptions_table, "ax"
    .balign 0x10
VTABLE:
	align_and_branch IVOR0_Vector
	align_and_branch IVOR1_Vector
	align_and_branch IVOR2_Vector
	align_and_branch IVOR3_Vector
	align_and_branch IVOR4_Vector
	align_and_branch IVOR5_Vector
	align_and_branch IVOR6_Vector
	align_and_branch IVOR7_Vector
	align_and_branch IVOR8_Vector
	align_and_branch IVOR9_Vector
	align_and_branch IVOR10_Vector
	align_and_branch IVOR11_Vector
	align_and_branch IVOR12_Vector
	align_and_branch IVOR13_Vector
	align_and_branch IVOR14_Vector
	align_and_branch IVOR15_Vector

	.section .intc_vector_table, "ax"
    .balign 0x10
	.globl Reset_Entry
INTCVTABLE:
Reset_Entry:
	b _start
	b _start
	b _start
	b _start
	align_and_branch Default_Handler 		# ISR01
	align_and_branch Default_Handler 		# ISR02
	align_and_branch Default_Handler 		# ISR03
	align_and_branch Default_Handler 		# ISR04
	align_and_branch Default_Handler 		# ISR05
	align_and_branch Default_Handler 		# ISR06
	align_and_branch Default_Handler 		# ISR07
	align_and_branch Default_Handler 		# ISR08
	align_and_branch Default_Handler 		# ISR09
	align_and_branch Default_Handler 		# ISR10
	align_and_branch Default_Handler 		# ISR11
	align_and_branch Default_Handler 		# ISR12
	align_and_branch Default_Handler 		# ISR13
	align_and_branch Default_Handler 		# ISR14
	align_and_branch Default_Handler 		# ISR15
	align_and_branch Default_Handler 		# ISR16
	align_and_branch Default_Handler 		# ISR17
	align_and_branch Default_Handler 		# ISR18
	align_and_branch Default_Handler 		# ISR19
	align_and_branch Default_Handler 		# ISR20
	align_and_branch Default_Handler 		# ISR21
	align_and_branch Default_Handler 		# ISR22
	align_and_branch Default_Handler 		# ISR23
	align_and_branch Default_Handler 		# ISR24
	align_and_branch Default_Handler 		# ISR25
	align_and_branch Default_Handler 		# ISR26
	align_and_branch Default_Handler 		# ISR27
	align_and_branch Default_Handler 		# ISR28
	align_and_branch Default_Handler 		# ISR29
	align_and_branch Default_Handler 		# ISR30
	align_and_branch Default_Handler 		# ISR31
	align_and_branch Default_Handler 		# ISR32
	align_and_branch Default_Handler 		# ISR33
	align_and_branch Default_Handler 		# ISR34
	align_and_branch Default_Handler 		# ISR35
	align_and_branch Default_Handler 		# ISR36
	align_and_branch Default_Handler 		# ISR37
	align_and_branch Default_Handler 		# ISR38
	align_and_branch Default_Handler 		# ISR39
	align_and_branch Default_Handler 		# ISR40
	align_and_branch Default_Handler 		# ISR41
	align_and_branch Default_Handler 		# ISR42
	align_and_branch Default_Handler 		# ISR43
	align_and_branch Default_Handler 		# ISR44
	align_and_branch Default_Handler 		# ISR45
	align_and_branch Default_Handler 		# ISR46
	align_and_branch Default_Handler 		# ISR47
	align_and_branch Default_Handler 		# ISR48
	align_and_branch Default_Handler 		# ISR49
	align_and_branch Default_Handler 		# ISR50
	align_and_branch Default_Handler 		# ISR51
	align_and_branch Default_Handler 		# ISR52
	align_and_branch Default_Handler 		# ISR53
	align_and_branch Default_Handler 		# ISR54
	align_and_branch Default_Handler 		# ISR55
	align_and_branch Default_Handler 		# ISR56
	align_and_branch Default_Handler 		# ISR57
	align_and_branch Default_Handler 		# ISR58
	align_and_branch Default_Handler 		# ISR59
	align_and_branch Default_Handler 		# ISR60
	align_and_branch Default_Handler 		# ISR61
	align_and_branch EMIOS_11_ISR 			# ISR62
	align_and_branch Default_Handler 		# ISR63
	align_and_branch Default_Handler 		# ISR64
	align_and_branch Default_Handler 		# ISR65
	align_and_branch Default_Handler 		# ISR66
	align_and_branch Default_Handler 		# ISR67
	align_and_branch Default_Handler 		# ISR68
	align_and_branch Default_Handler 		# ISR69
	align_and_branch Default_Handler 		# ISR70
	align_and_branch Default_Handler 		# ISR71
	align_and_branch Default_Handler		# ISR72
	align_and_branch Default_Handler 		# ISR73
	align_and_branch Default_Handler 		# ISR74
	align_and_branch Default_Handler 		# ISR75
	align_and_branch Default_Handler 		# ISR76
	align_and_branch Default_Handler 		# ISR77
	align_and_branch Default_Handler 		# ISR78
	align_and_branch Default_Handler 		# ISR79
	align_and_branch Default_Handler 		# ISR80
	align_and_branch Default_Handler 		# ISR81
	align_and_branch Default_Handler 		# ISR82
	align_and_branch Default_Handler 		# ISR83
	align_and_branch Default_Handler 		# ISR84
	align_and_branch Default_Handler 		# ISR85
	align_and_branch Default_Handler 		# ISR86
	align_and_branch Default_Handler 		# ISR87
	align_and_branch Default_Handler 		# ISR88
	align_and_branch Default_Handler 		# ISR89
	align_and_branch Default_Handler 		# ISR90
	align_and_branch Default_Handler 		# ISR91
	align_and_branch Default_Handler 		# ISR92
	align_and_branch Default_Handler 		# ISR93
	align_and_branch Default_Handler 		# ISR94
	align_and_branch Default_Handler 		# ISR95
	align_and_branch Default_Handler 		# ISR96
	align_and_branch Default_Handler 		# ISR97
	align_and_branch Default_Handler 		# ISR98
	align_and_branch Default_Handler 		# ISR99
	align_and_branch Default_Handler 		# ISR100
	align_and_branch Default_Handler 		# ISR101
	align_and_branch Default_Handler 		# ISR102
	align_and_branch Default_Handler 		# ISR103
	align_and_branch Default_Handler 		# ISR104
	align_and_branch Default_Handler 		# ISR105
	align_and_branch Default_Handler 		# ISR106
	align_and_branch Default_Handler 		# ISR107
	align_and_branch Default_Handler 		# ISR108
	align_and_branch Default_Handler 		# ISR109
	align_and_branch Default_Handler 		# ISR110
	align_and_branch Default_Handler 		# ISR111
	align_and_branch Default_Handler 		# ISR112
	align_and_branch Default_Handler 		# ISR113
	align_and_branch Default_Handler 		# ISR114
	align_and_branch Default_Handler 		# ISR115
	align_and_branch Default_Handler 		# ISR116
	align_and_branch Default_Handler 		# ISR117
	align_and_branch Default_Handler 		# ISR118
	align_and_branch Default_Handler 		# ISR119
	align_and_branch Default_Handler 		# ISR120
	align_and_branch Default_Handler 		# ISR121
	align_and_branch Default_Handler 		# ISR122
	align_and_branch Default_Handler 		# ISR123
	align_and_branch Default_Handler 		# ISR124
	align_and_branch Default_Handler 		# ISR125
	align_and_branch Default_Handler 		# ISR126
	align_and_branch Default_Handler 		# ISR127
	align_and_branch Default_Handler 		# ISR128
	align_and_branch Default_Handler 		# ISR129
	align_and_branch Default_Handler 		# ISR130
	align_and_branch Default_Handler 		# ISR131
	align_and_branch Default_Handler 		# ISR132
	align_and_branch Default_Handler 		# ISR133
	align_and_branch Default_Handler 		# ISR134
	align_and_branch Default_Handler 		# ISR135
	align_and_branch Default_Handler 		# ISR136
	align_and_branch Default_Handler 		# ISR137
	align_and_branch Default_Handler 		# ISR138
	align_and_branch Default_Handler 		# ISR139
	align_and_branch Default_Handler 		# ISR140
	align_and_branch Default_Handler 		# ISR141
	align_and_branch Default_Handler 		# ISR142
	align_and_branch Default_Handler 		# ISR143
	align_and_branch Default_Handler 		# ISR144
	align_and_branch Default_Handler 		# ISR145
	align_and_branch Default_Handler 		# ISR146
	align_and_branch Default_Handler 		# ISR147
	align_and_branch Default_Handler 		# ISR148
	align_and_branch Default_Handler 		# ISR149
	align_and_branch Default_Handler 		# ISR150
	align_and_branch Default_Handler 		# ISR151
	align_and_branch Default_Handler 		# ISR152
	align_and_branch Default_Handler 		# ISR153
	align_and_branch Default_Handler 		# ISR154
	align_and_branch Default_Handler 		# ISR155
	align_and_branch Default_Handler 		# ISR156
	align_and_branch Default_Handler 		# ISR157
	align_and_branch Default_Handler 		# ISR158
	align_and_branch Default_Handler 		# ISR159
	align_and_branch CAN_A_RXFIFO_ISR 		# ISR160: CAN A BUF5 / RXFIFO
	align_and_branch Default_Handler 		# ISR161
	align_and_branch Default_Handler 		# ISR162
	align_and_branch Default_Handler 		# ISR163
	align_and_branch Default_Handler 		# ISR164
	align_and_branch Default_Handler 		# ISR165
	align_and_branch Default_Handler 		# ISR166
	align_and_branch Default_Handler 		# ISR167
	align_and_branch Default_Handler 		# ISR168
	align_and_branch Default_Handler 		# ISR169
	align_and_branch Default_Handler 		# ISR170
	align_and_branch Default_Handler 		# ISR171
	align_and_branch Default_Handler 		# ISR172
	align_and_branch Default_Handler 		# ISR173
	align_and_branch Default_Handler 		# ISR174
	align_and_branch Default_Handler 		# ISR175
	align_and_branch Default_Handler 		# ISR176
	align_and_branch Default_Handler 		# ISR177
	align_and_branch Default_Handler 		# ISR178
	align_and_branch Default_Handler 		# ISR179
	align_and_branch Default_Handler 		# ISR180
	align_and_branch CAN_C_RXFIFO_ISR 		# ISR181: CAN C BUF5 / RXFIFO
	align_and_branch Default_Handler 		# ISR182
	align_and_branch Default_Handler 		# ISR183
	align_and_branch Default_Handler 		# ISR184
	align_and_branch Default_Handler 		# ISR185
	align_and_branch Default_Handler 		# ISR186
	align_and_branch Default_Handler 		# ISR187
	align_and_branch Default_Handler 		# ISR188
	align_and_branch Default_Handler 		# ISR189
	align_and_branch Default_Handler 		# ISR190
	align_and_branch Default_Handler 		# ISR191
	align_and_branch Default_Handler 		# ISR192
	align_and_branch Default_Handler 		# ISR193
	align_and_branch Default_Handler 		# ISR194
	align_and_branch Default_Handler 		# ISR195
	align_and_branch Default_Handler 		# ISR196
	align_and_branch Default_Handler 		# ISR197
	align_and_branch Default_Handler 		# ISR198
	align_and_branch Default_Handler 		# ISR199
	align_and_branch Default_Handler 		# ISR200
	align_and_branch Default_Handler 		# ISR201
	align_and_branch Default_Handler 		# ISR202
	align_and_branch Default_Handler 		# ISR203
	align_and_branch Default_Handler 		# ISR204
	align_and_branch Default_Handler 		# ISR205
	align_and_branch Default_Handler 		# ISR206
	align_and_branch Default_Handler 		# ISR207
	align_and_branch Default_Handler 		# ISR208
	align_and_branch Default_Handler 		# ISR209
	align_and_branch Default_Handler 		# ISR210
	align_and_branch Default_Handler 		# ISR211
	align_and_branch Default_Handler 		# ISR212
	align_and_branch Default_Handler 		# ISR213
	align_and_branch Default_Handler 		# ISR214
	align_and_branch Default_Handler 		# ISR215
	align_and_branch Default_Handler 		# ISR216
	align_and_branch Default_Handler 		# ISR217
	align_and_branch Default_Handler 		# ISR218
	align_and_branch Default_Handler 		# ISR219
	align_and_branch Default_Handler 		# ISR220
	align_and_branch Default_Handler 		# ISR221
	align_and_branch Default_Handler 		# ISR222
	align_and_branch Default_Handler 		# ISR223
	align_and_branch Default_Handler 		# ISR224
	align_and_branch Default_Handler 		# ISR225
	align_and_branch Default_Handler 		# ISR226
	align_and_branch Default_Handler 		# ISR227
	align_and_branch Default_Handler 		# ISR228
	align_and_branch Default_Handler 		# ISR229
	align_and_branch Default_Handler 		# ISR230
	align_and_branch Default_Handler 		# ISR231
	align_and_branch Default_Handler 		# ISR232
	align_and_branch Default_Handler 		# ISR233
	align_and_branch Default_Handler 		# ISR234
	align_and_branch Default_Handler 		# ISR235
	align_and_branch Default_Handler 		# ISR236
	align_and_branch Default_Handler 		# ISR237
	align_and_branch Default_Handler 		# ISR238
	align_and_branch Default_Handler 		# ISR239
	align_and_branch Default_Handler 		# ISR240
	align_and_branch Default_Handler 		# ISR241
	align_and_branch Default_Handler 		# ISR242
	align_and_branch Default_Handler 		# ISR243
	align_and_branch Default_Handler 		# ISR244
	align_and_branch Default_Handler 		# ISR245
	align_and_branch Default_Handler 		# ISR246
	align_and_branch Default_Handler 		# ISR247
	align_and_branch Default_Handler 		# ISR248
	align_and_branch Default_Handler 		# ISR249
	align_and_branch Default_Handler 		# ISR250
	align_and_branch Default_Handler 		# ISR251
	align_and_branch Default_Handler 		# ISR252
	align_and_branch Default_Handler 		# ISR253
	align_and_branch Default_Handler 		# ISR254
	align_and_branch Default_Handler 		# ISR255
	align_and_branch Default_Handler 		# ISR256
	align_and_branch Default_Handler 		# ISR257
	align_and_branch Default_Handler 		# ISR258
	align_and_branch Default_Handler 		# ISR259
	align_and_branch Default_Handler 		# ISR260
	align_and_branch Default_Handler 		# ISR261
	align_and_branch Default_Handler 		# ISR262
	align_and_branch Default_Handler 		# ISR263
	align_and_branch Default_Handler 		# ISR264
	align_and_branch Default_Handler 		# ISR265
	align_and_branch Default_Handler 		# ISR266
	align_and_branch Default_Handler 		# ISR267
	align_and_branch Default_Handler 		# ISR268
	align_and_branch Default_Handler 		# ISR269
	align_and_branch Default_Handler 		# ISR270
	align_and_branch Default_Handler 		# ISR271
	align_and_branch Default_Handler 		# ISR272
	align_and_branch Default_Handler 		# ISR273
	align_and_branch Default_Handler 		# ISR274
	align_and_branch Default_Handler 		# ISR275
	align_and_branch Default_Handler 		# ISR276
	align_and_branch Default_Handler 		# ISR277
	align_and_branch Default_Handler 		# ISR278
	align_and_branch Default_Handler 		# ISR279
	align_and_branch Default_Handler 		# ISR280
	align_and_branch Default_Handler 		# ISR281
	align_and_branch Default_Handler 		# ISR282
	align_and_branch Default_Handler 		# ISR283
	align_and_branch Default_Handler 		# ISR284
	align_and_branch Default_Handler 		# ISR285
	align_and_branch Default_Handler 		# ISR286
	align_and_branch Default_Handler 		# ISR287
	align_and_branch CAN_B_RXFIFO_ISR 		# ISR288: CAN B BUF5 / RXFIFO
	align_and_branch Default_Handler 		# ISR289
	align_and_branch Default_Handler 		# ISR290
	align_and_branch Default_Handler 		# ISR291
	align_and_branch Default_Handler 		# ISR292
	align_and_branch Default_Handler 		# ISR293
	align_and_branch Default_Handler 		# ISR294
	align_and_branch Default_Handler 		# ISR295
	align_and_branch Default_Handler 		# ISR296
	align_and_branch Default_Handler 		# ISR297
	align_and_branch Default_Handler 		# ISR298
	align_and_branch Default_Handler 		# ISR299
	align_and_branch Default_Handler 		# ISR300
	align_and_branch Default_Handler 		# ISR301
	align_and_branch Default_Handler 		# ISR302
	align_and_branch Default_Handler 		# ISR303
	align_and_branch Default_Handler 		# ISR304
	align_and_branch Default_Handler 		# ISR305
	align_and_branch Default_Handler 		# ISR306
	align_and_branch Default_Handler 		# ISR307
	align_and_branch Default_Handler 		# ISR308
	align_and_branch Default_Handler 		# ISR309
	align_and_branch Default_Handler 		# ISR310
	align_and_branch Default_Handler 		# ISR311
	align_and_branch Default_Handler 		# ISR312
	align_and_branch Default_Handler 		# ISR313
	align_and_branch Default_Handler 		# ISR314
	align_and_branch Default_Handler 		# ISR315
	align_and_branch CAN_D_RXFIFO_ISR 		# ISR316: CAN D BUF5 / RXFIFO
	align_and_branch Default_Handler 		# ISR317
	align_and_branch Default_Handler 		# ISR318
	align_and_branch Default_Handler 		# ISR319
	align_and_branch Default_Handler 		# ISR320
	align_and_branch Default_Handler 		# ISR321
	align_and_branch Default_Handler 		# ISR322
	align_and_branch Default_Handler 		# ISR323
	align_and_branch Default_Handler 		# ISR324
	align_and_branch Default_Handler 		# ISR325
	align_and_branch Default_Handler 		# ISR326
	align_and_branch Default_Handler 		# ISR327
	align_and_branch Default_Handler 		# ISR328
	align_and_branch Default_Handler 		# ISR329
	align_and_branch Default_Handler 		# ISR330
	align_and_branch Default_Handler 		# ISR331
	align_and_branch Default_Handler 		# ISR332
	align_and_branch Default_Handler 		# ISR333
	align_and_branch Default_Handler 		# ISR334
	align_and_branch Default_Handler 		# ISR335
	align_and_branch Default_Handler 		# ISR336
	align_and_branch Default_Handler 		# ISR337
	align_and_branch Default_Handler 		# ISR338
	align_and_branch Default_Handler 		# ISR339
	align_and_branch Default_Handler 		# ISR340
	align_and_branch Default_Handler 		# ISR341
	align_and_branch Default_Handler 		# ISR342
	align_and_branch Default_Handler 		# ISR343
	align_and_branch Default_Handler 		# ISR344
	align_and_branch Default_Handler 		# ISR345
	align_and_branch Default_Handler 		# ISR346
	align_and_branch Default_Handler 		# ISR347
	align_and_branch Default_Handler 		# ISR348
	align_and_branch Default_Handler 		# ISR349
	align_and_branch Default_Handler 		# ISR350
	align_and_branch Default_Handler 		# ISR351
	align_and_branch Default_Handler 		# ISR352
	align_and_branch Default_Handler 		# ISR353
	align_and_branch Default_Handler 		# ISR354
	align_and_branch Default_Handler 		# ISR355
	align_and_branch Default_Handler 		# ISR356
	align_and_branch Default_Handler 		# ISR357
	align_and_branch Default_Handler 		# ISR358
	align_and_branch Default_Handler 		# ISR359
	align_and_branch Default_Handler 		# ISR360
	align_and_branch Default_Handler 		# ISR361
	align_and_branch Default_Handler 		# ISR362
	align_and_branch Default_Handler 		# ISR363
	align_and_branch Default_Handler 		# ISR364
	align_and_branch Default_Handler 		# ISR365
	align_and_branch Default_Handler 		# ISR366
	align_and_branch Default_Handler 		# ISR367
	align_and_branch Default_Handler 		# ISR368
	align_and_branch Default_Handler 		# ISR369
	align_and_branch Default_Handler 		# ISR370
	align_and_branch Default_Handler 		# ISR371
	align_and_branch Default_Handler 		# ISR372
	align_and_branch Default_Handler 		# ISR373
	align_and_branch Default_Handler 		# ISR374
	align_and_branch Default_Handler 		# ISR375
	align_and_branch Default_Handler 		# ISR376
	align_and_branch Default_Handler 		# ISR377
	align_and_branch Default_Handler 		# ISR378
	align_and_branch Default_Handler 		# ISR379
	align_and_branch Default_Handler 		# ISR380
	align_and_branch Default_Handler 		# ISR381
	align_and_branch Default_Handler 		# ISR382
	align_and_branch Default_Handler 		# ISR383
	align_and_branch Default_Handler 		# ISR384
	align_and_branch Default_Handler 		# ISR385
	align_and_branch Default_Handler 		# ISR386
	align_and_branch Default_Handler 		# ISR387
	align_and_branch Default_Handler 		# ISR388
	align_and_branch Default_Handler 		# ISR389
	align_and_branch Default_Handler 		# ISR390
	align_and_branch Default_Handler 		# ISR391
	align_and_branch Default_Handler 		# ISR392
	align_and_branch Default_Handler 		# ISR393
	align_and_branch Default_Handler 		# ISR394
	align_and_branch Default_Handler 		# ISR395
	align_and_branch Default_Handler 		# ISR396
	align_and_branch Default_Handler 		# ISR397
	align_and_branch Default_Handler 		# ISR398
	align_and_branch Default_Handler 		# ISR399
	align_and_branch Default_Handler 		# ISR400
	align_and_branch Default_Handler 		# ISR401
	align_and_branch Default_Handler 		# ISR402
	align_and_branch Default_Handler 		# ISR403
	align_and_branch Default_Handler 		# ISR404
	align_and_branch Default_Handler 		# ISR405
	align_and_branch Default_Handler 		# ISR406
	align_and_branch Default_Handler 		# ISR407
	align_and_branch Default_Handler 		# ISR408
	align_and_branch Default_Handler 		# ISR409
	align_and_branch Default_Handler 		# ISR410
	align_and_branch Default_Handler 		# ISR411
	align_and_branch Default_Handler 		# ISR412
	align_and_branch Default_Handler 		# ISR413
	align_and_branch Default_Handler 		# ISR414
	align_and_branch Default_Handler 		# ISR415
	align_and_branch Default_Handler 		# ISR416
	align_and_branch Default_Handler 		# ISR417
	align_and_branch Default_Handler 		# ISR418
	align_and_branch Default_Handler 		# ISR419
	align_and_branch Default_Handler 		# ISR420
	align_and_branch Default_Handler 		# ISR421
	align_and_branch Default_Handler 		# ISR422
	align_and_branch Default_Handler 		# ISR423
	align_and_branch Default_Handler 		# ISR424
	align_and_branch Default_Handler 		# ISR425
	align_and_branch Default_Handler 		# ISR426
	align_and_branch Default_Handler 		# ISR427
	align_and_branch Default_Handler 		# ISR428
	align_and_branch Default_Handler 		# ISR429
	align_and_branch Default_Handler 		# ISR430
	align_and_branch Default_Handler 		# ISR431
	align_and_branch Default_Handler 		# ISR432
	align_and_branch Default_Handler 		# ISR433
	align_and_branch Default_Handler 		# ISR434
	align_and_branch Default_Handler 		# ISR435
	align_and_branch Default_Handler 		# ISR436
	align_and_branch Default_Handler 		# ISR437
	align_and_branch Default_Handler 		# ISR438
	align_and_branch Default_Handler 		# ISR439
	align_and_branch Default_Handler 		# ISR440
	align_and_branch Default_Handler 		# ISR441
	align_and_branch Default_Handler 		# ISR442
	align_and_branch Default_Handler 		# ISR443
	align_and_branch Default_Handler 		# ISR444
	align_and_branch Default_Handler 		# ISR445
	align_and_branch Default_Handler 		# ISR446
	align_and_branch Default_Handler 		# ISR447
	align_and_branch Default_Handler 		# ISR448
	align_and_branch Default_Handler 		# ISR449
	align_and_branch Default_Handler 		# ISR450
	align_and_branch Default_Handler 		# ISR451
	align_and_branch Default_Handler 		# ISR452
	align_and_branch Default_Handler 		# ISR453
	align_and_branch Default_Handler 		# ISR454
	align_and_branch Default_Handler 		# ISR455
	align_and_branch Default_Handler 		# ISR456
	align_and_branch Default_Handler 		# ISR457
	align_and_branch Default_Handler 		# ISR458
	align_and_branch Default_Handler 		# ISR459
	align_and_branch Default_Handler 		# ISR460
	align_and_branch Default_Handler 		# ISR461
	align_and_branch Default_Handler 		# ISR462
	align_and_branch Default_Handler 		# ISR463
	align_and_branch Default_Handler 		# ISR464
	align_and_branch Default_Handler 		# ISR465
	align_and_branch Default_Handler 		# ISR466
	align_and_branch Default_Handler 		# ISR467
	align_and_branch Default_Handler 		# ISR468
	align_and_branch Default_Handler 		# ISR469
	align_and_branch Default_Handler 		# ISR470
	align_and_branch Default_Handler 		# ISR471
	align_and_branch Default_Handler 		# ISR472
	align_and_branch Default_Handler 		# ISR473
	align_and_branch Default_Handler 		# ISR474
	align_and_branch Default_Handler 		# ISR475
	align_and_branch Default_Handler 		# ISR476
	align_and_branch Default_Handler 		# ISR477
	align_and_branch Default_Handler 		# ISR478
	align_and_branch Default_Handler 		# ISR479
