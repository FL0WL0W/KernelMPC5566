/* ELF function annotations used by Picolibc's PowerPC setjmp assembly. */
#ifndef E200_PPC_ASM_H
#define E200_PPC_ASM_H
#define FUNC_START(name) .text; .balign 4; .globl name; .type name,@function; name:
#define FUNC_END(name) .size name,.-name
#endif
