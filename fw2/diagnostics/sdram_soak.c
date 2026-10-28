#pragma GCC optimize ("Os")
#include "sdram_soak.h"
#include "../gfx/gfx.h"
#include "../platform/platform.h"
/* The block under test: 2 MiB of SDRAM words (1 M pixels) at pixel address 0xC00000 (24 MiB in), far above the display buffers (rows 0..1023 of two buffers end at 1.5 M pixels) and the stash, and above
 * everything Player 2 keeps in SDRAM (nothing: the library and the rings live in PSRAM). The mailbox is the CPU's only access path here; each op is atomic, so it cannot interleave with other mailbox users. */
#define SOAK_BASE  0x00C00000u
#define SOAK_PAIRS 0x00080000u
sk_t soak_st; static uint8_t level; static uint32_t last;
static const uint32_t gap_cyc[4] = { 0u, CLK_HZ / 16000u, CLK_HZ / 64000u, CLK_HZ / 128000u };
uint32_t soak_level(void) { return level; }
void soak_cycle(void)
{
    level = (uint8_t)((level + 1u) & 3u);
    if (level == 1u) { sk_init(&soak_st, SOAK_BASE, SOAK_PAIRS, cycles() | 1u); last = cycles(); }    /* a fresh run starts from level 1; levels 2 and 3 carry on */
    soak_run = level != 0u;
}
void soak_step(void)
{
    if (!level) return;
    const uint32_t now = cycles(), g = gap_cyc[level], n = (now - last) / g;
    if (!n) return;
    last += n * g;
    sk_step(&soak_st, gfx_mb_write, gfx_mb_read, n > 16u ? 16u : n);
    if (n > 16u) last = now;                       /* behind (a long frame): do not catch up in one burst */
}
