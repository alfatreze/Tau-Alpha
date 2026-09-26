/* Host harness for fw/chladni.inc: emulates the SDRAM mailbox and the draw engine's BLIT/SBLIT on a 512-wide word memory,
 * then checks the screen against an independent rendering of the same figure. Built and run by sim/test_chladni_module.py.
 * Usage: harness <hw_swap 0|1> <mailbox_ok 0|1> */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLK_HZ 60000000u
#define COLD_FN3 /* the harness runs on the host: no cold section */
#define FB_STRIDE 512u
#define FB_H 360u
#define UI_WAVE_Y 150u
#define UI_WAVE_H 110u
#define UI_OVERLAY_UP 0
#define R_SDR_ADDR   0x80000074u
#define R_SDR_DATA   0x80000078u
#define R_SDR_CTRL   0x8000007Cu
#define R_SDR_RDATA  0x80000080u
#define R_SDR_STATUS 0x80000084u

static int hw_swap, mailbox_ok;
static uint16_t sdram[1024u * 512u];
static uint32_t regs[8];
static uint32_t cyc;
static uint32_t *reg_ptr(uint32_t a);
#define REG(a) (*reg_ptr(a))
static uint32_t cycles(void) { cyc += 100u; return cyc; }

/* mailbox: on CTRL the transaction is applied when the driver polls STATUS or reads RDATA (it always does) */
static uint32_t *reg_ptr(uint32_t a) {
    uint32_t i = (a - 0x80000074u) / 4u;
    if ((a == R_SDR_STATUS || a == R_SDR_RDATA) && regs[2] != 0u) {   /* a command is pending */
        uint32_t c = regs[2], addr = regs[0], data = regs[1];
        if (mailbox_ok) {
            if (c & 2u) {                                   /* write */
                uint16_t lo = (uint16_t)data, hi = (uint16_t)(data >> 16);
                sdram[addr]      = hw_swap ? hi : lo;
                sdram[addr + 1u] = hw_swap ? lo : hi;
            } else {                                        /* read */
                uint32_t lo = sdram[addr], hi = sdram[addr + 1u];
                regs[3] = hw_swap ? ((lo << 16) | hi) : ((hi << 16) | lo);
            }
        } else regs[3] = 0xDEADBEEFu;
        regs[4] = 0; regs[2] = 0u;                          /* consumed: the next identical command is a new one */
    }
    return &regs[i];
}

static int engine_calls, sblit_calls, blit_calls, max_blit_w;
static void fb_wait(void) {}
static void fb_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    blit_calls++; if (w > max_blit_w) max_blit_w = (int)w;
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++)
        sdram[(dy + y) * FB_STRIDE + dx + x] = sdram[(sy + y) * FB_STRIDE + sx + x];
}
static void fb_sblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t scx, uint32_t scy) {
    sblit_calls++;                                          /* w, h are SOURCE cells (RTL sblit_ext): output = source * scale, clamped to 127 */
    uint32_t k = (scx == 3u) ? 3u : (scx == 2u) ? 2u : 1u, ow = w * k, oh = h * k; (void)scy;
    if (ow > 127u) ow = 127u;
    if (oh > 127u) oh = 127u;
    for (uint32_t y = 0; y < oh; y++) for (uint32_t x = 0; x < ow; x++)
        sdram[(dy + y) * FB_STRIDE + dx + x] = sdram[(sy + y / k) * FB_STRIDE + sx + x / k];
}
static int toasts;
static void ui_toast_msg(const char *m) { (void)m; toasts++; }
static unsigned char spec_lvl[16];
static int afford = 1;
static int meter_afford(void) { return afford; }
static uint32_t ui_cpu_pct(void) { return 0u; }
static uint16_t ui_accent = 0x07E0u;
static uint16_t ui_grad_at(uint32_t y) { (void)y; return 0x1082u; }
static uint16_t ui_mix(uint16_t a, uint16_t b, uint32_t t, uint32_t d) {      /* per-channel linear blend, as the firmware's */
    uint32_t r = 0, sh[3] = {11, 5, 0}, mk[3] = {31, 63, 31};
    for (int k = 0; k < 3; k++) { uint32_t ca = (a >> sh[k]) & mk[k], cb = (b >> sh[k]) & mk[k]; r |= ((ca * (d - t) + cb * t) / d) << sh[k]; }
    return (uint16_t)r;
}

#include "../fw/chladni.inc"

struct exp_ctx { uint8_t lv[CHL_MAX_RY][CHL_MAX_RX]; };
static void exp_row(uint32_t j, const uint8_t *lvl, void *ud) { memcpy(((struct exp_ctx *)ud)->lv[j], lvl, chl_pre->Rx); }

int main(int argc, char **argv) {
    hw_swap = atoi(argv[1]); mailbox_ok = atoi(argv[2]);
    for (int i = 0; i < 16; i++) spec_lvl[i] = (unsigned char)((i * 37 + 11) & 0xFF);
    const uint32_t X0 = 20u, WW = 360u;

    cyc += 60000u * 1000u;                                   /* well past the rate limit */
    chladni_tick(X0, UI_WAVE_Y, WW, 1u);
    if (!mailbox_ok) { printf("ok=%u toasts=%d drawn=%u\n", chl_ok, toasts, chl_drawn_n); return 0; }
    printf("ok=%u swap=%u drawn=%u sblit=%d blit=%d maxw=%d toasts=%d\n", chl_ok, chl_swap, chl_drawn_n, sblit_calls, blit_calls, max_blit_w, toasts);
    if (chl_ok != 1u || chl_swap != (uint8_t)hw_swap || chl_drawn_n != 1u) return 1;

    /* independent expectation: run the same core from an identical state on a copy and build the screen by hand */
    chl_state_t st; chl_init(&st, &chl_cfg);
    /* the module already advanced chl_st; re-derive the modes from the module's state after the draw is not possible, so
       compare instead against a re-render of the module's OWN state (modes/weights used for the draw) -- select is pure. */
    chl_mode_t md[CHL_MAX_K]; uint32_t K = chl_select(&chl_st, &chl_cfg, md);
    struct exp_ctx ec; int16_t ring[3u * CHL_MAX_RX], half[CHL_HALF_N], cxm[CHL_MAX_K * CHL_MAX_RX], cxn[CHL_MAX_K * CHL_MAX_RX]; uint8_t lv[CHL_MAX_RX];
    const uint32_t rx = chl_pre->Rx, ry = chl_pre->Ry;
    chl_render(md, K, rx, ry, chl_hw_q8(&chl_st, &chl_cfg, rx), ring, chl_cfg.fold ? half : (int16_t *)0, cxm, cxn, lv, exp_row, &ec);
    uint16_t pal[4] = { 0x1082u, ui_mix(0x1082u, 0x07E0u, 1, 3), ui_mix(0x1082u, 0x07E0u, 2, 3), 0x07E0u };
    long bad = 0, n = 0;
    const uint32_t tw = rx * 3u, th = (ry * 3u < UI_WAVE_H) ? ry * 3u : UI_WAVE_H;
    for (uint32_t y = 0; y < th; y++) for (uint32_t x = 0; x < WW; x++) {
        uint16_t want = pal[ec.lv[(y / 3u) % ry][((x % tw) / 3u)] & 3u], got = sdram[(UI_WAVE_Y + y) * FB_STRIDE + X0 + x];
        n++; if (want != got) { if (bad < 4) printf("MISMATCH x=%u y=%u want %04X got %04X\n", x, y, want, got); bad++; }
    }
    printf("compared %ld screen pixels, %ld wrong\n", n, bad);
    if (bad) return 1;

    /* rate limit: an immediate second call (no time passed) draws nothing; after the period it draws again */
    unsigned d0 = chl_drawn_n; cyc += 100u; chladni_tick(X0, UI_WAVE_Y, WW, 0u);
    printf("second call drew %u more (want 0)\n", chl_drawn_n - d0); if (chl_drawn_n != d0) return 1;
    cyc += 60000u * 100u; afford = 0; chladni_tick(X0, UI_WAVE_Y, WW, 0u);
    printf("with the audio FIFO low drew %u more (want 0), skipped=%u\n", chl_drawn_n - d0, chl_skipped_n); if (chl_drawn_n != d0 || !chl_skipped_n) return 1;
    afford = 1; chladni_tick(X0, UI_WAVE_Y, WW, 0u);
    printf("after the period drew %u more (want 1)\n", chl_drawn_n - d0); if (chl_drawn_n != d0 + 1u) return 1;
    return 0;
}
