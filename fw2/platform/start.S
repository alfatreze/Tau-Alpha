/* Player 2 startup (written from the behavioural contract: the ROM is loaded at RAM address 0 and entered at _start; the stack top is the linker's _stack_top).
 * 1. set the stack pointer; 2. paint the whole stack with a sentinel so the high-water mark can be read later (platform_stack_peak); 3. clear .bss; 4. main().
 * The register file is not cleared: nothing here or in the compiler's output reads a register before writing it, and the build uses -mno-relax so gp is never used. */
	.section .text._start
	.global _start
_start:
	la sp, _stack_top

	la a0, _stack_size
	la a1, _stack_top
	sub a0, a1, a0
	li a2, 0xDEADC0DE
1:
	bge a0, a1, 2f
	sw a2, 0(a0)
	addi a0, a0, 4
	j 1b
2:
	la a0, __bss_start
	la a1, __bss_end
3:
	bge a0, a1, 4f
	sw zero, 0(a0)
	addi a0, a0, 4
	j 3b
4:
	call main
5:
	j 5b
