	.section .text._start
	.global _start
_start:
	la sp, _stack_top
	la a0, __bss_start
	la a1, __bss_end
1:	bge a0, a1, 2f
	sw zero, 0(a0)
	addi a0, a0, 4
	j 1b
2:	call main
	li t0, 0xF0000004
	sw a0, 0(t0)
3:	j 3b
