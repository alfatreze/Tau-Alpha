#pragma GCC optimize ("Os")
#include "cold.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "slot.h"
#include "../../fw/cold_core.h"

#define COLD_SLOT_ID 6u
#define COLD_E_NOFETCH 18
#define COLD_E_PROBE   19
extern char _cold_start, _cold_end, _cold_img_size, _tag_start;
/* Patched into the ROM by tools/pack_cold.py: an image built for another layout is refused. */
volatile uint32_t tau_cold_id = 0xC01DC01Du;
uint32_t cold_err, cold_ms, cold_bytes;

__attribute__((section(".cold_text"), noinline)) uint32_t cold_probe(uint32_t a, uint32_t b) { return a * b + 1u; }

static int cold_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx; (void)dst;
    return slot_read_blocking(COLD_SLOT_ID, off, (uint32_t)(uintptr_t)&_tag_start, len, CLK_HZ * 3u);
}

static int cold_psram_prove(void)
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG_RD(0x80000088u) != 0x50535231u) return 0;                               /* the PSRAM controller must be in this bitstream */
    if (!((REG_RD(0x800000A0u) >> 16) & 1u)) return 0;                              /* and its CPU window */
    volatile uint32_t *w0 = (volatile uint32_t *)&_cold_start;
    volatile uint32_t *w1 = (volatile uint32_t *)((uintptr_t)&_cold_start + (uintptr_t)&_cold_img_size - 4u);
    for (uint32_t k = 0; k < 2u; k++) {
        *w0 = pat[k]; *w1 = ~pat[k];
        if (*w0 != pat[k] || *w1 != ~pat[k]) return 0;
    }
    return 1;
}

int cold_boot(void)
{
    uint32_t size = (uint32_t)(uintptr_t)&_cold_img_size;
    cold_err = 0;
    if (!size) return 0;                                                            /* nothing cold in this build */
    uint32_t t0 = cycles();
    if (!slot_read_blocking(COLD_SLOT_ID, 0u, (uint32_t)(uintptr_t)&_tag_start, 1u, CLK_HZ * 3u)) { cold_err = COLD_E_NOFILE; return (int)cold_err; }
    uint8_t *win = (uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start);      /* the tag zone through the uncached alias */
    int e = cold_psram_prove() ? cold_load(cold_rd, 0, win, (volatile uint8_t *)&_cold_start, 0x100000u, size, tau_cold_id) : COLD_E_PSRAM;
    cold_ms = (cycles() - t0) / (CLK_HZ / 1000u);
    if (e) { cold_err = (uint32_t)e; return e; }
    cold_bytes = size;
    if (!(REG_RD(0x800000B8u) & 1u)) { cold_err = COLD_E_NOFETCH; return (int)cold_err; }   /* the bitstream would fetch the alias as a copy of RAM */
    if (cold_probe(6u, 7u) != 43u) { cold_err = COLD_E_PROBE; return (int)cold_err; }
    return 0;
}
