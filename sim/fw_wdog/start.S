/* Firmware-in-the-loop test of the pack watchdog (tau_wdog + the VexRiscv timer interrupt): start-up. Like sim/fw_ifetch/start.S plus the trap vector. */
    .section .text._start
    .globl _start
_start:
    la   sp, _stack_top
    la   t0, trap_entry
    csrw mtvec, t0                      /* direct mode: every trap enters at trap_entry */
    la   t0, __bss_start
    la   t1, __bss_end
1:  bgeu t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:  call main
3:  j    3b
