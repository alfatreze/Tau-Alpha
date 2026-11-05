/* Player 2 startup (written from the behavioural contract: the ROM is loaded at RAM address 0 and entered at _start; the stack top is the linker's _stack_top).
 * 1. set the stack pointer; 2. paint the whole stack with a sentinel so the high-water mark can be read later (platform_stack_peak); 3. clear .bss; 4. main().
 * The register file is not cleared: nothing here or in the compiler's output reads a register before writing it, and the build uses -mno-relax so gp is never used. */
	.option arch, +zicsr					/* the build's -march is rv32im: allow the CSR instructions in this file */
	.section .text._start
	.global _start
_start:
	la sp, _stack_top
	la t0, trap_entry
	csrw mtvec, t0				/* the pack watchdog's timer interrupt (and any other trap) enters at trap_entry */

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

/* ---- pack watchdog (docs/features/PACK_WATCHDOG_PLAN.md) ---------------------------------------------------------------------------------------------------------------
 * wd_save(jb) saves ra, sp, s0..s11 (a minimal setjmp) and returns 0; when the watchdog interrupt fires, trap_entry acknowledges it, closes the interrupt window (MPIE := 0, so MIE is 0 after mret) and "returns"
 * into wd_resume, which restores the saved registers from *wd_jb_ptr and returns 1 from wd_save. Any other trap restarts the firmware (what the unset mtvec did before: it jumped to address 0). */
	.text
	.globl wd_save
wd_save:
	sw ra, 0(a0)
	sw sp, 4(a0)
	sw s0, 8(a0)
	sw s1, 12(a0)
	sw s2, 16(a0)
	sw s3, 20(a0)
	sw s4, 24(a0)
	sw s5, 28(a0)
	sw s6, 32(a0)
	sw s7, 36(a0)
	sw s8, 40(a0)
	sw s9, 44(a0)
	sw s10, 48(a0)
	sw s11, 52(a0)
	li a0, 0
	ret
	.globl wd_resume
wd_resume:
	la t0, wd_jb_ptr
	lw t0, 0(t0)
	lw ra, 0(t0)
	lw sp, 4(t0)
	lw s0, 8(t0)
	lw s1, 12(t0)
	lw s2, 16(t0)
	lw s3, 20(t0)
	lw s4, 24(t0)
	lw s5, 28(t0)
	lw s6, 32(t0)
	lw s7, 36(t0)
	lw s8, 40(t0)
	lw s9, 44(t0)
	lw s10, 48(t0)
	lw s11, 52(t0)
	li a0, 1
	ret
	.balign 4
	.globl trap_entry
trap_entry:
	csrr t0, mcause
	li t1, 0x80000007				/* machine timer interrupt = the watchdog */
	bne t0, t1, 1f
	la t3, wd_cut_flag				/* set only while a guarded window is open (wd_run): a stray interrupt outside it restarts like any other trap */
	lw t4, 0(t3)
	beqz t4, 1f
	li t3, 0x800001E4				/* R_WDOG_ACK */
	sw zero, 0(t3)
	li t3, 0x80
	csrc mstatus, t3				/* MPIE := 0: interrupts are off after the mret */
	la t3, wd_resume
	csrw mepc, t3
	mret
1:	la t0, _start
	jr t0
	.globl wd_irq_on
wd_irq_on:
	li t0, 0x80
	csrs mie, t0
	li t0, 8
	csrs mstatus, t0
	ret
	.globl wd_irq_off
wd_irq_off:
	li t0, 8
	csrc mstatus, t0
	ret
