#pragma GCC optimize ("Os")
#include "assets.h"
#include "slot.h"
#include "../platform/mmio.h"
#define AS_FN static
#include "../../fw/assets_core.h"

#define ASSETS_SLOT 8u
#define ASSETS_PSRAM_BASE 0xA4520000u        /* 64 KiB staging for a file larger than the 4 KiB window (the old player's address, free in Player 2's PSRAM map) */
extern char _tag_start;
uint8_t assets_state; uint32_t assets_err, assets_bytes;
static const uint8_t *file_base; static uint32_t file_total;

static int assets_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx; (void)dst;
    return slot_read_blocking(ASSETS_SLOT, off, (uint32_t)(uintptr_t)&_tag_start, len, CLK_HZ * 3u);
}
int assets_psram_ok(void)
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG_RD(0x80000088u) != 0x50535231u) return 0;                 /* the PSRAM bus is in this bitstream */
    if (!((REG_RD(0x800000A0u) >> 16) & 1u)) return 0;
    volatile uint32_t *w0 = (volatile uint32_t *)(uintptr_t)ASSETS_PSRAM_BASE, *w1 = (volatile uint32_t *)(uintptr_t)(ASSETS_PSRAM_BASE + AS_MAX_FILE - 4u);
    for (uint32_t k = 0; k < 2u; k++) { *w0 = pat[k]; *w1 = ~pat[k]; if (*w0 != pat[k] || *w1 != ~pat[k]) return 0; }
    return 1;
}

void assets_load(void)
{
    uint8_t *win = (uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start);     /* the tag zone through the uncached alias */
    const uint8_t *base = 0; uint32_t total = 0;
    int e = as_load(assets_rd, 0, win, (volatile uint8_t *)(uintptr_t)ASSETS_PSRAM_BASE, AS_MAX_FILE, assets_psram_ok, &base, &total);
    if (e == AS_NOFILE) { assets_state = 1; return; }
    if (e) { assets_state = 3; assets_err = (uint32_t)e; return; }
    assets_state = 2; assets_bytes = total;
    file_base = base; file_total = total;
}
const uint8_t *assets_file(uint32_t *total) { if (total) *total = file_total; return file_base; }
int assets_section(const char *tag, const uint8_t **p, uint32_t *n)
{
    uint32_t off, len;
    if (!file_base || as_find(file_base, file_total, tag, &off, &len) != AS_OK) return 0;
    *p = file_base + off; *n = len; return 1;
}
int assets_in_psram(void) { return file_base == (const uint8_t *)(uintptr_t)ASSETS_PSRAM_BASE; }
