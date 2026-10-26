/* Host check of the cover palette probe in fw2/art/art.c against a draw-engine model whose palette blit LANDS some time after it is queued (behind earlier drawing, as after a track change).
 * Args: land_cycles half(0 low,1 high) clut_skew(0|1). Time is a model: every REG_RD of the cycle counter advances it. Prints "RC <code> RAW <hex> CLUT <start>". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/art/art.c"
#include "../fw2/platform/platform.h"
caps_t caps;
static uint32_t land_cycles_arg, now_c, land_at, half, skew, queued, dst_word, mem_dst, mem_src;
uint32_t host_rd(uint32_t a) { if (a == 0x8000000Cu) { now_c += 100u; return now_c; } return 0; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint32_t slot_opens, slot_open_fails, slot_open_ms_last, slot_open_ms_max; uint8_t slot_err, slot_errcode;
int slot_open_path(uint32_t s, const char *p) { (void)s; (void)p; return 0; }
int slot_read_blocking(uint32_t s, uint32_t o, uint32_t d, uint32_t l, uint32_t t) { (void)s; (void)o; (void)d; (void)l; (void)t; return 0; }
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static uint32_t dst_addr;
int gfx_mb_write(uint32_t a, uint32_t d) { if (a == (PROBE_ROW * GFX_STRIDE + GFX_CELL_X)) mem_src = d; else { dst_word = d; dst_addr = a; } return 0; }
int gfx_mb_read(uint32_t a, uint32_t *o)
{
    (void)a; now_c += 300u;
    if (queued && now_c >= land_at) {                         /* the palette blit has landed: entry (5 or 7, shifted by the skew) into the chosen half */
        uint32_t idx = half ? (mem_src >> 16) : (mem_src & 0xFFFFu); idx = (idx + 0xFFFFFFFFu * 0u) & 0xFFu;   /* the index byte of the lower-address pixel */
        uint32_t entry = 0x1000u + (idx + (skew ? 0xFFu : 0u)) % 256u;   /* skew: the palette is written one slot high, so index n reads entry n-1 */
        dst_word = half ? ((dst_word & 0x0000FFFFu) | (entry << 16)) : ((dst_word & 0xFFFF0000u) | entry);
        queued = 0;
    }
    *o = dst_word; return 0;
}
/* the real gfx_fence() (fw2/gfx/gfx.c) writes 0xDEADBEEF to the cell (row 1021, column 480) and queues a 1-pixel rectangle on it: whatever the probe keeps there is gone (B-859, found on the Pocket: ART PROBE RAW DEAD1007) */
void gfx_fence(void) { if (dst_addr == 1021u * GFX_STRIDE + 480u) dst_word = (0xDEADu << 16) | 1u; }
void gfx_clut_begin(uint32_t s) { (void)s; gfx_fence(); }          /* the real one fences first (fw2/gfx/gfx.c) */
void gfx_clut_put(uint16_t v) { (void)v; }
void gfx_cblit_abs(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { (void)sx; (void)sy; (void)dx; (void)dy; (void)w; (void)h; queued = 1; land_at = now_c + land_cycles_arg; }

int main(int argc, char **argv)
{
    land_cycles_arg = (uint32_t)atoi(argv[1]); half = (uint32_t)atoi(argv[2]); skew = (uint32_t)atoi(argv[3]);
    int rc = probe();
    printf("RC %d RAW %08x CLUT %u\n", rc, art_probe_raw, art_clut_start);
    return 0;
}
