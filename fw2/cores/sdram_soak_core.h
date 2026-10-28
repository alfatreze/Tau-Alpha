/* fw2/cores/sdram_soak_core.h -- the SDRAM soak (S0 of the shared SDRAM controller, D-T09): a pattern write pass and a read-verify pass over a block of SDRAM, forever, counting every
 * word that reads back wrong. Pure and host-tested (sim/test_p2_sdram_soak.py). The old player's stress pump (A-094, fw/stress.inc) is archived; this is its small replacement for Player 2, run beside
 * playback and drawing so the scanout, the engine and the CPU all contend for the chip while it checks the data path. Words are the mailbox's 32-bit units (two RGB565 pixels at a pixel address).
 * One pass = write every pair with a pattern of (seed, index), then read every pair and compare; the next pass uses a new seed, so a bit that is stuck from the last pass is caught. */
#ifndef SDRAM_SOAK_CORE_H
#define SDRAM_SOAK_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint32_t base, pairs, i, pass, seed, phase, ops, fails, tmo, first_addr, first_exp, first_got; } sk_t;
typedef int (*sk_wr_t)(uint32_t a, uint32_t d);               /* 0 ok */
typedef int (*sk_rd_t)(uint32_t a, uint32_t *d);
static inline uint32_t sk_pat(uint32_t seed, uint32_t i)
{
    uint32_t x = (BUG == 2 ? 0u : i) * 0x9E3779B1u + seed;      /* BUG 2 (mutant): the pattern does not depend on the address, so an address fault cannot show */
    x ^= x >> 15; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
    return x;
}
static inline void sk_init(sk_t *s, uint32_t base, uint32_t pairs, uint32_t seed)
{
    s->base = base; s->pairs = pairs; s->i = 0; s->pass = 0; s->seed = seed; s->phase = 0; s->ops = 0; s->fails = 0; s->tmo = 0; s->first_addr = 0; s->first_exp = 0; s->first_got = 0;
}
static inline void sk_step(sk_t *s, sk_wr_t wr, sk_rd_t rd, uint32_t n)
{
    while (n--) {
        const uint32_t a = s->base + 2u * s->i;
        if (s->phase == 0u) { if (wr(a, sk_pat(s->seed, s->i))) s->tmo++; }
        else {
            uint32_t g = 0; const uint32_t e = sk_pat(s->seed, s->i);
            if (rd(BUG == 3 ? a + 2u : a, &g)) s->tmo++;      /* BUG 3 (mutant): reads the next pair */
            else if (BUG != 1 && g != e) { if (!s->fails) { s->first_addr = a; s->first_exp = e; s->first_got = g; } s->fails++; }      /* BUG 1 (mutant): no comparison */
        }
        s->ops++;
        if (++s->i >= s->pairs) { s->i = 0; if (s->phase) { s->pass++; if (BUG != 4) s->seed = s->seed * 1664525u + 1013904223u; } s->phase ^= 1u; }     /* BUG 4 (mutant): the seed never changes */
    }
}
#endif
