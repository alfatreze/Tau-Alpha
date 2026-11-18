/* Firmware-in-the-loop test of the save RAM's CPU side (MMIO SAVE_ST 0x1EC, SAVE_IDX 0x1F0, SAVE_DATA 0x1F4 of tau_soc.v) run by the real VexRiscv; sim/tb_save_soc.v is the other half and plays APF.
 * MODE 0: the testbench LOADED a pattern file (word i, file order = LE value 0xA0000000 ^ i * 0x01010101) while the CPU was in reset. The firmware checks the status, reads the whole file BACK TO BACK
 *   (an index write, then data reads with no status read between: the fastest the bus can ask) and again the way the accessor does (a status read after every index write), writes a few words
 *   (back to back, and by seek), reads them back, and prints SAVE DONE. The testbench then holds the CPU in reset, plays the flush and compares every word.
 * MODE 1: the self-test of fw2/cores/save_probe_core.h (the SAME code the Pocket runs) once; the testbench plays Quit, reloads the flushed file as the next boot's load, and restarts the CPU, which runs it again.
 * Every check prints "SAVE ok n" or "SAVE FAIL n". */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE    0x80000000u
#define R_SAVE_ST  0x800001ECu
#define R_SAVE_IDX 0x800001F0u
#define R_SAVE_DATA 0x800001F4u
#ifndef MODE
#define MODE 0
#endif
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex_(uint32_t v) { for (int i = 28; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 15u]); }
static void dec_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "SAVE ok " : "SAVE FAIL "); dec_(n); putc_('\n'); }
#define WORDS 2048u
#define PAT(i) (0xA0000000u ^ ((i) * 0x01010101u))

#if MODE == 0
int main(void)
{
    uint32_t st = REG(R_SAVE_ST);
    chk(1, (st >> 31) == 1u && ((st >> 30) & 1u) == 1u && ((st >> 24) & 31u) == 11u && (st & 0xFFFFu) == WORDS);     /* built in, the bridge wrote, 2^11 words, 2,048 words arrived */
    REG(R_SAVE_IDX) = 0u;                                                                                              /* back to back: the first data read comes right behind the index write */
    uint32_t bad = 0, firstbad = 0xFFFFu;
    for (uint32_t i = 0; i < WORDS; i++) { const uint32_t v = REG(R_SAVE_DATA); if (v != PAT(i)) { if (!bad) firstbad = i; bad++; } }
    chk(2, bad == 0u);
    if (bad) { puts_("SAVE first bad word "); dec_(firstbad); putc_('\n'); }
    chk(3, REG(R_SAVE_IDX) == 0u);                                                                                    /* 2,048 steps wrap the index to 0 */
    bad = 0;
    for (uint32_t i = 0; i < WORDS; i += 7u) { REG(R_SAVE_IDX) = i; (void)REG(R_SAVE_ST); if (REG(R_SAVE_DATA) != PAT(i)) bad++; if (i + 1u < WORDS && REG(R_SAVE_DATA) != PAT(i + 1u)) bad++; }   /* the accessor's way, and the step */
    chk(4, bad == 0u);
    REG(R_SAVE_IDX) = 0u;                                                                                              /* write-through, back to back */
    for (uint32_t i = 0; i < 16u; i++) REG(R_SAVE_DATA) = 0xC0DE0000u + i;
    chk(5, REG(R_SAVE_IDX) == 16u);
    REG(R_SAVE_IDX) = 100u;  REG(R_SAVE_DATA) = 0x12345678u;                                                          /* by seek */
    REG(R_SAVE_IDX) = 2047u; REG(R_SAVE_DATA) = 0xFEEDF00Du;
    REG(R_SAVE_IDX) = 0u; (void)REG(R_SAVE_ST);
    bad = 0; for (uint32_t i = 0; i < 16u; i++) if (REG(R_SAVE_DATA) != 0xC0DE0000u + i) bad++;
    REG(R_SAVE_IDX) = 100u; (void)REG(R_SAVE_ST); if (REG(R_SAVE_DATA) != 0x12345678u) bad++;
    REG(R_SAVE_IDX) = 2047u; (void)REG(R_SAVE_ST); if (REG(R_SAVE_DATA) != 0xFEEDF00Du) bad++;
    chk(6, bad == 0u);
    chk(7, REG(R_SAVE_IDX) == 0u);                                                                                    /* the last word wrapped the index */
    puts_("SAVE DONE\n");
    for (;;) { }
}
#else
#define SVP_WORDS 128u                                       /* the file is 8 KB on a Pocket: here the first 128 words, the logic is the same (the CPU simulation is slow) */
#include "../../fw2/cores/save_probe_core.h"
static void io_seek(uint32_t w) { REG(R_SAVE_IDX) = w; (void)REG(R_SAVE_ST); }
static uint32_t io_rd(void) { return REG(R_SAVE_DATA); }
static void io_wr(uint32_t v) { REG(R_SAVE_DATA) = v; }
int main(void)
{
    svp_io_t io; svp_res_t r; io.seek = io_seek; io.rd = io_rd; io.wr = io_wr;
    svp_run(&io, 1700000000u, &r);
    puts_("SV boots="); dec_(r.boots); puts_(" crc="); dec_(r.crc_state); puts_(" pat_bad="); dec_(r.pat_bad); puts_(" first="); dec_(r.first); puts_(" last="); hex_(r.last_value_loaded); puts_(" wrote="); hex_(r.last_value_written); putc_('\n');
    puts_("SAVE DONE\n");
    for (;;) { }
}
#endif
