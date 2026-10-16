/* Host check of fw2/ui/meter_chladni.c against a model of the draw engine and the SDRAM mailbox. After every figure the visible box must equal the tile plane (as the model's framebuffer holds it) scaled by the
 * cell size and repeated across the box; the plane itself is checked against a direct render of the core. argv: seed steps swap(0/1 = which mailbox half is the lower address) broken(1 = mailbox ignores writes).
 * Prints MISMATCH n FIGURES n UNAVAIL u. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t trace[64][64];
#define CHL_TRACE(j, l, n) do { for (uint32_t k_ = 0; k_ < (n); k_++) trace[j][k_] = (l)[k_] & 3u; } while (0)
#include "../fw2/ui/meter_chladni.c"
#define FW 512u
#define FH 1024u
static uint16_t fb[FW * FH];
static int hw_swap, broken; static uint32_t cyc;
uint32_t host_rd(uint32_t a) { (void)a; cyc += 3000u; return cyc; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < 400u && y + j < FH) fb[(y + j) * FW + x + i] = c; }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg) { (void)s; (void)fg; (void)bg; (void)y; return x; }
int gfx_mb_write(uint32_t a, uint32_t d)
{
    if (broken) return 0;
    uint16_t lo = d & 0xFFFFu, hi = d >> 16;
    fb[a] = hw_swap ? hi : lo; fb[a + 1] = hw_swap ? lo : hi; return 0;
}
int gfx_mb_read(uint32_t a, uint32_t *o)
{
    uint16_t p = fb[a], q = fb[a + 1];
    *o = hw_swap ? ((uint32_t)p << 16 | q) : ((uint32_t)q << 16 | p); return 0;
}
void gfx_fence(void) {}
void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h)
{
    if (w > 127u) { fprintf(stderr, "blit too wide %u\n", w); exit(2); }
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) fb[(dy + j) * FW + dx + i] = fb[(sy + j) * FW + sx + i];
}
void gfx_sblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t s)
{
    uint32_t ow = w * s, oh = h * s; if (ow > 127u) ow = 127u;
    for (uint32_t j = 0; j < oh; j++) for (uint32_t i = 0; i < ow; i++) fb[(dy + j) * FW + dx + i] = fb[(sy + j / s) * FW + sx + i / s];
}
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
int main(int argc, char **argv)
{
    rng = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u; int steps = argc > 2 ? atoi(argv[2]) : 200; hw_swap = argc > 3 ? atoi(argv[3]) : 0; broken = argc > 4 ? atoi(argv[4]) : 0;
    th_role[TR_ACCENT] = 0xF7BEu; th_role[TR_ERROR] = 0xF800u;
    uint8_t spec[MTR_BANDS] = { 0 }; int bad = 0, figs = 0;
    const uint32_t BX = 20, BY = 100, BW = 360, BH = 110;
    for (int n = 0; n < steps; n++) {
        for (uint32_t b = 0; b < MTR_BANDS; b++) spec[b] = (uint8_t)((rnd() & 1u) ? rnd() : spec[b] / 2u);
        mtr_in_t in = { spec, (uint32_t)n, 26, BX, BY, BW, BH, 0x0000u, (uint8_t)(n == 0), 0, 0, 0 };
        const uint32_t before = chladni_drawn();
        meter_chladni_tick(&in);
        if (chladni_drawn() == before) continue;
        figs++;
        const uint32_t cell = pre->cell, rx = pre->Rx, ry = pre->Ry;
        const uint32_t sw = rx * cell > 127u ? 127u / cell : rx, sh = ry * cell > BH ? BH / cell : ry, tw = sw * cell, th = sh * cell;
        int diff = 0;
        for (uint32_t y = 0; y < BH && !diff; y++) for (uint32_t x = 0; x < BW; x++) {
            uint16_t want = fb[(CHL_PLANE_Y + (y % th) / cell) * FW + (x % tw) / cell];
            if (fb[(BY + y) * FW + BX + x] != want) { diff = 1; break; }
        }
        for (uint32_t j = 0; j < ry && !diff; j++) for (uint32_t i = 0; i < rx; i++) {            /* the plane holds the levels the core produced, through the palette */
            const uint16_t pal[4] = { 0, mix(0, 0xF7BEu, 1u, 3u), mix(0, 0xF7BEu, 2u, 3u), 0xF7BEu };
            if (fb[(CHL_PLANE_Y + j) * FW + i] != pal[trace[j][i]]) { diff = 1; break; }
        }
        bad += diff;
    }
    printf("MISMATCH %d FIGURES %d UNAVAIL %u\n", bad, figs, chladni_unavailable());
    return 0;
}
