#pragma GCC optimize ("Os")
#include "art.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../stream/slot.h"

#define PLANE_Y 400u                       /* 128 rows of index words, below the visible 360 */
#define PROBE_ROW 1022u
extern char _tag_start;
#define TAG_OFF ((uint32_t)(uintptr_t)&_tag_start)
#define TAG_SZ  4096u
#define TAGB    ((const volatile uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start))

uint32_t art_loaded, art_failed, art_last_err, art_ms, art_probe_state, art_clut_start;
static uint8_t probed, swap, clut_start; static uint16_t img_w, img_h;

/* One known-answer probe settles both unknowns of this bitstream: (1) which half of a mailbox word is the lower-address pixel, (2) whether a palette load starting at index 0 lands entry n in slot n
 * (the registered address) or in slot n + 1 (older bitstreams). The palette entry i holds 0x1000 + i; the source word carries index 5 in its high half and 7 in its low half; one palette blit of one
 * pixel reads the lower-address one: entry 7 = low half first, no skew; 5 = high half first, no skew; 6 = low first with skew; 4 = high first with skew. */
static int probe(void)
{
    const uint32_t src = PROBE_ROW * GFX_STRIDE + GFX_CELL_X, dst = (PROBE_ROW - 1u) * GFX_STRIDE + GFX_CELL_X;
    uint32_t r = 0;
    if (gfx_mb_write(src, (5u << 16) | 7u) || gfx_mb_write(dst, 0u)) return TIMG_E_MAILBOX;
    gfx_clut_begin(0u);
    for (uint32_t i = 0; i < 256u; i++) gfx_clut_put((uint16_t)(0x1000u + i));
    gfx_cblit(GFX_CELL_X, PROBE_ROW, GFX_CELL_X, PROBE_ROW - 1u, 1u, 1u);
    gfx_fence();
    if (gfx_mb_read(dst, &r)) return TIMG_E_MAILBOX;
    uint32_t px = (r & 0xFFFFu) | (r >> 16);                  /* one half holds the pixel, the other is the zero we wrote */
    if (px == 0x1007u) { swap = 0; clut_start = 0; }
    else if (px == 0x1005u) { swap = 1; clut_start = 0; }
    else if (px == 0x1006u) { swap = 0; clut_start = 255; }
    else if (px == 0x1004u) { swap = 1; clut_start = 255; }
    else return TIMG_E_PROBE;
    art_clut_start = clut_start;
    return 0;
}

int art_load(const char *path)
{
    const uint32_t t0 = cycles();
    int e = 0;
    if (!probed) { e = probe(); if (e) goto fail; probed = 1; art_probe_state = 1; }
    if (!slot_open_path(ART_SLOT, path)) { e = TIMG_E_OPEN; goto fail; }
    timg_hdr_t h;
    if (!slot_read_blocking(ART_SLOT, 0u, TAG_OFF, TIMG_HDR_BYTES, CLK_HZ * 2u)) { e = TIMG_E_READ; goto fail; }
    uint8_t hb[16]; for (uint32_t i = 0; i < 16u; i++) hb[i] = TAGB[i];
    e = timg_parse(hb, &h); if (e) goto fail;
    if (!slot_read_blocking(ART_SLOT, TIMG_HDR_BYTES, TAG_OFF, TIMG_CLUT_BYTES, CLK_HZ * 2u)) { e = TIMG_E_READ; goto fail; }
    gfx_clut_begin(clut_start);
    for (uint32_t i = 0; i < 256u; i++) gfx_clut_put((uint16_t)(TAGB[2u * i] | ((uint32_t)TAGB[2u * i + 1u] << 8)));
    const uint32_t total = (uint32_t)h.w * h.h;
    uint32_t pos = 0, col = 0, row = 0, pend = 0;
    while (pos < total) {
        uint32_t n = total - pos; if (n > TAG_SZ) n = TAG_SZ;
        if (!slot_read_blocking(ART_SLOT, TIMG_HDR_BYTES + TIMG_CLUT_BYTES + pos, TAG_OFF, n, CLK_HZ * 2u)) { e = TIMG_E_READ; goto fail; }
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t b = TAGB[i];
            if (!(col & 1u)) pend = b;
            if ((col & 1u) || col + 1u == h.w) {
                uint32_t w32 = (col & 1u) ? timg_pair(pend, b, swap) : timg_pair(pend, 0u, swap);
                if (gfx_mb_write((PLANE_Y + row) * GFX_STRIDE + (col & ~1u), w32)) { e = TIMG_E_READ; goto fail; }
            }
            if (++col == h.w) { col = 0u; row++; }
        }
        pos += n;
    }
    img_w = h.w; img_h = h.h;
    art_ms = (cycles() - t0) / (CLK_HZ / 1000u); art_loaded++; art_last_err = 0;
    return TIMG_OK;
fail:
    art_failed++; art_last_err = (uint32_t)e;
    return e;
}

void art_draw(uint32_t dx, uint32_t dy)
{
    const uint32_t cw = img_w < 128u ? img_w : 128u, ch = img_h < 128u ? img_h : 128u;
    const uint32_t ox = dx + (128u - cw) / 2u, oy = dy + (128u - ch) / 2u;
    for (uint32_t x = 0; x < cw; x += 120u) {                  /* one palette blit is at most 127 pixels wide */
        const uint32_t w = cw - x < 120u ? cw - x : 120u;
        gfx_cblit(x, PLANE_Y, ox + x, oy, w, ch);
    }
}
