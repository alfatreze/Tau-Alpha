# Minimal bare-metal entry point for the Stage 0 PicoRV32 benchmark firmware.
# Zeroes every register (gp/x3 included -- picorv32 does NOT zero registers
# at reset, and this build has no real __global_pointer$ setup, so leaving
# gp as reset-time garbage caused a wild gp-relative store the very first
# time GCC used one; -mno-relax is also set at compile time as a second
# guard against that class of bug). Then sets sp, clears bss, calls main().

	.section .text._start
	.global _start
_start:
	addi x1, zero, 0
	addi x2, zero, 0
	addi x3, zero, 0
	addi x4, zero, 0
	addi x5, zero, 0
	addi x6, zero, 0
	addi x7, zero, 0
	addi x8, zero, 0
	addi x9, zero, 0
	addi x10, zero, 0
	addi x11, zero, 0
	addi x12, zero, 0
	addi x13, zero, 0
	addi x14, zero, 0
	addi x15, zero, 0
	addi x16, zero, 0
	addi x17, zero, 0
	addi x18, zero, 0
	addi x19, zero, 0
	addi x20, zero, 0
	addi x21, zero, 0
	addi x22, zero, 0
	addi x23, zero, 0
	addi x24, zero, 0
	addi x25, zero, 0
	addi x26, zero, 0
	addi x27, zero, 0
	addi x28, zero, 0
	addi x29, zero, 0
	addi x30, zero, 0
	addi x31, zero, 0

	la sp, _stack_top

	# B-204 (PHASE_F_SPEC.md section 4.1): paint the whole 16 KB stack region with a
	# known sentinel before anything can push a single byte onto it, so a later
	# high-water-mark scan (stack_high_water(), fw/suite.inc) can tell how deep
	# execution has ever actually gone -- the standard embedded technique. `la`
	# on a plain linker-script scalar (here `_stack_size`) yields its VALUE, the
	# same "address of" trick this codebase already uses for `_cold_img_size`
	# etc. Must run before the bss-clear loop below, which is itself the first
	# thing capable of using the stack (branch return addresses, however small).
	la a0, _stack_size
	la a1, _stack_top
	sub a0, a1, a0
	li a2, 0xDEADC0DE
3:
	bge a0, a1, 4f
	sw a2, 0(a0)
	addi a0, a0, 4
	j 3b
4:

	la a0, __bss_start
	la a1, __bss_end
1:
	bge a0, a1, 2f
	sw zero, 0(a0)
	addi a0, a0, 4
	j 1b
2:
	call main
	ebreak
loop:
	j loop
