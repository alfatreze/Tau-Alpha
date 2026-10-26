/* Host check of fw2/settings/settings.c settings_init() on a register file with a write log and a write budget (a power loss: later writes are dropped). Commands on stdin:
 *   "R w0 .. w31"        load the register file
 *   "W n"                allow n more writes (default unlimited), "N 0|1" narrow file (16 words: an index of 16 and up aliases onto 0..15)
 *   "I"                  settings_init: prints the written words in order ("w <word>" lines), then "regs" with the 32 words, "vals" with the SF_COUNT values and "migrated n"
 *   "E w0 .. w31"        prints the migrated words of that legacy register file (sf_migrate_words) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define P2_HOST 1
static uint32_t regs[32], idx, now, budget = 0xFFFFFFFFu, narrow, nlog, log_[512];
#define R_CYCLES_ADDR 0x80000000u
static uint32_t host_rd(uint32_t a);
static void host_wr(uint32_t a, uint32_t v);
#define REG_RD(a) host_rd((uint32_t)(a))
#define REG_WR(a, v) host_wr((uint32_t)(a), (v))
#include "../fw2/settings/settings.c"
static uint32_t slot(void) { return narrow ? (idx & 15u) : (idx & 31u); }
static uint32_t host_rd(uint32_t a) { if (a == R_SET_DAT) return regs[slot()]; if (a == R_CYCLES) return now; return 0; }
static void host_wr(uint32_t a, uint32_t v)
{
    if (a == R_SET_IDX) { idx = v; return; }
    if (a != R_SET_DAT) return;
    if (budget == 0) return;                                    /* power lost: the write never happened */
    if (budget != 0xFFFFFFFFu) budget--;
    regs[slot()] = v; if (nlog < 512u) log_[nlog++] = slot();
}
int main(void)
{
    char c; unsigned x;
    while (scanf(" %c", &c) == 1) {
        if (c == 'R') { for (int i = 0; i < 32; i++) { scanf("%u", &x); regs[i] = x; } }
        else if (c == 'W') { scanf("%u", &x); budget = x; }
        else if (c == 'N') { scanf("%u", &x); narrow = x; }
        else if (c == 'E') { uint32_t w[32], o[32]; for (int i = 0; i < 32; i++) { scanf("%u", &x); w[i] = x; } sf_migrate_words(w, o); for (int i = 0; i < 32; i++) printf("%u ", o[i]); printf("\n"); }
        else if (c == 'I') {
            uint32_t vals[SF_COUNT]; nlog = 0; memset(&settings_stats, 0, sizeof settings_stats);
            settings_init(vals);
            for (uint32_t i = 0; i < nlog; i++) printf("w %u\n", log_[i]);
            printf("regs"); for (int i = 0; i < 32; i++) printf(" %u", regs[i]); printf("\nvals"); for (uint32_t i = 0; i < SF_COUNT; i++) printf(" %u", vals[i]); printf("\nmigrated %u wide %u layout %u\n", settings_stats.migrated, settings_stats.wide, settings_stats.layout);
        }
    }
    return 0;
}
