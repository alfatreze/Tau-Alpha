/* B-572: host model of the double-buffered draw engine (H2) around the REAL Settings meter-preview code.
 *
 * sim/test_thumb_dbuf.py extracts the real functions (fb_clut_load, fb_cblit, fb_set_bases, ui_mix, ui_bg_cur_buf from fw/player.c;
 * set_thumb_pal, set_draw_thumb_soft, set_thumb_flat_build, set_draw_thumb from fw/settingsui.inc) into build/thumb_dbuf/thumb_fw.inc,
 * and this file supplies the engine they run against. The model follows the documented semantics, and deliberately queues commands:
 *   - RECT-class commands (fb_rect) target the buffer R_DBUF_CPU names AT THE TIME THEY EXECUTE;
 *   - blit-mode commands (CBLIT) read and write through the sticky SRC/DST bases AT THE TIME THEY EXECUTE, never R_DBUF_CPU;
 *   - the CLUT is one shared table, read when a command executes; its write path has the RTL's one-slot skew (B-569);
 *   - commands execute in order when the FIFO is full (fb_wait) or at fb_fence(): so a base, a CPU-buffer selection or a CLUT reload
 *     done while commands are still queued affects them (B-412's lesson).
 * The test draws the six CLUT-blit previews with the displayed buffer 0 and 1 and compares each with the software-drawn one. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TAU_ART_TIMG 1
#define COLD_FN
#define COLD_DATA
#define COLD_FN2
#define COLD_FN3
#define FB_W 400u
#define FB_H 360u
#define FB_STRIDE 512u
#define DBUF_BASE1_W 1048576u
#define ART_IMG 128u
#define ART_PAD 0u
#define ART_H (ART_IMG + 2u * ART_PAD)
#define ART_STASH_Y 360u
#define THUMB_STASH_Y (ART_STASH_Y + ART_H)
#define FB_OP_CBLIT 7u
#define R_FB_ADDR   0x80000048u
#define R_FB_SIZE   0x8000004Cu
#define R_FB_COLOR  0x80000050u
#define R_FB_GO     0x80000054u
#define R_BLT_IDX   0x800000C0u
#define R_BLT_DATA  0x800000C4u
#define R_CLUT_IDX  0x800000C8u
#define R_CLUT_DATA 0x800000CCu
#define R_DBUF_CPU  0x80000118u
#define R_DBUF_DISP 0x8000011Cu
#ifndef CLUT_START_IDX
#define CLUT_START_IDX 255u
#endif
#ifndef CLUT_SKEW
#define CLUT_SKEW 1u
#endif
#ifndef FIFO_DEPTH
#define FIFO_DEPTH 16
#endif

#include "../fw/meter_gen_enum.h"
#include "../fw/theme.h"
#include "../fw/meter_thumbs.h"
#include "../fw/meter_core.h"   /* mtr_ramp: fw/player.c ui_mix() forwards to it (meter-builder) */

/* ---- the engine model ---- */
static uint16_t mem[2u * DBUF_BASE1_W];
static uint32_t r_fb_addr, r_fb_size, r_fb_color, go_val, go_pending, r_cpu, r_disp;
static uint32_t blt_field[8], blt_idx, clut_idx, clut_data_dummy, blt_idx_dummy, scratch;
static uint16_t clut[256];
typedef struct { int kind; uint32_t a, b, c, d, e; } cmd_t;       /* RECT: x,y,w,h,colour   CBLIT: addr,size,color */
static cmd_t q[4096];
static int qn;
static void exec(const cmd_t *c)
{
    if (c->kind == 0) {                                                         /* RECT: follows R_DBUF_CPU now */
        const uint32_t base = r_cpu ? DBUF_BASE1_W : 0u;
        for (uint32_t j = 0; j < c->d; j++) for (uint32_t i = 0; i < c->c; i++)
            mem[base + (c->b + j) * FB_STRIDE + c->a + i] = (uint16_t)c->e;
    } else {                                                                    /* CBLIT: follows the sticky bases now */
        const uint32_t w = c->b & 0x1FFu, h = (c->b >> 9) & 0x1FFu;
        const uint32_t src = ((c->c >> 16) & 0xFFFFu) | ((c->c & 7u) << 16);
        for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++)
            mem[blt_field[2] + c->a + j * FB_STRIDE + i] = clut[mem[blt_field[0] + src + j * FB_STRIDE + i] & 0xFFu];
    }
}
static void drain(int upto) { for (int i = 0; i < upto; i++) exec(&q[i]); memmove(q, q + upto, (size_t)(qn - upto) * sizeof q[0]); qn -= upto; }
static void process_go(void)
{
    if (!go_pending) return;
    go_pending = 0;
    if ((go_val & 3u) == 3u || go_val == FB_OP_CBLIT) { q[qn++] = (cmd_t){ 1, r_fb_addr, r_fb_size, r_fb_color, 0, 0 }; }
}
extern uint32_t clut_pending_slot;
static void clut_flush(void);
static uint32_t *reg_ptr(uint32_t a)
{
    clut_flush();                                   /* the previous CLUT data store has landed by now */
    process_go();
    switch (a) {
    case R_FB_ADDR:  return &r_fb_addr;
    case R_FB_SIZE:  return &r_fb_size;
    case R_FB_COLOR: return &r_fb_color;
    case R_FB_GO:    go_pending = 1; return &go_val;
    case R_BLT_IDX:  return &blt_idx;
    case R_BLT_DATA: { uint32_t *p = &blt_field[blt_idx & 7u]; blt_idx = (blt_idx + 1u) % 7u; return p; }   /* the written field */
    case R_CLUT_IDX: return &clut_idx;
    case R_CLUT_DATA: { uint32_t slot = (clut_idx + CLUT_SKEW) & 255u; clut_idx = (clut_idx + 1u) & 255u; scratch = 0; (void)slot;
                        /* the stored value lands in `scratch`; applied at the next access (below) */
                        clut_pending_slot = slot; return &scratch; }
    case R_DBUF_CPU: return &r_cpu;
    case R_DBUF_DISP: return &r_disp;
    }
    return &scratch;
}
uint32_t clut_pending_slot = 0xFFFFFFFFu;
#define REG(a) (*reg_ptr(a))
/* a CLUT data write completes when the next register is touched: keep that simple by flushing at fb_* entry points */
static void clut_flush(void) { if (clut_pending_slot != 0xFFFFFFFFu) { clut[clut_pending_slot] = (uint16_t)scratch; clut_pending_slot = 0xFFFFFFFFu; } }

#define FB_HELD() 0
#define DBUF_READY() 1
#define BLIT_READY() 1
#define cold_ready() 1
static void blit_probe_ensure(void) {}
static uint32_t fb_color_shadow;
static void fb_wait(void) { process_go(); clut_flush(); while (qn >= FIFO_DEPTH) drain(1); }
static void fb_fence(void) { process_go(); clut_flush(); drain(qn); }
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    if (!w || !h || FB_HELD()) return;
    process_go(); clut_flush();
    while (qn >= FIFO_DEPTH) drain(1);
    q[qn++] = (cmd_t){ 0, x, y, w, h, c };
}
/* ---- the real firmware code under test ---- */
#include "thumb_fw.inc"

static uint8_t thumb_flat_ready_dummy;
static int tests, fails;
static void check(int ok, const char *m, int a, int b) { tests++; if (!ok) { fails++; printf("FAIL %s (%d,%d)\n", m, a, b); } else printf("ok   %s (%d,%d)\n", m, a, b); }

int main(void)
{
    /* the CLUT-blit previews, drawn the way the Settings list draws them: one after another */
    const uint32_t vizs[] = { VIZ_WINAMP_SCOPE, VIZ_WINAMP_BARS, VIZ_CHLADNI, VIZ_BARS, VIZ_WATER, VIZ_SCOPE, VIZ_WAVE, VIZ_VU, VIZ_DOTS, VIZ_LED };
    for (uint32_t disp = 0; disp < 2u; disp++) {
        memset(mem, 0, sizeof mem); memset(clut, 0, sizeof clut); memset(blt_field, 0, sizeof blt_field);
        qn = 0; thumb_flat_ready = 0u; r_cpu = disp; r_disp = disp;
        const uint32_t base = disp ? DBUF_BASE1_W : 0u;
        int bad_prev = 0;
        for (uint32_t k = 0; k < sizeof vizs / sizeof vizs[0]; k++) {
            const uint32_t y = 8u + k * 34u;
            set_draw_thumb(60u, y, vizs[k]);                  /* hardware path */
            set_draw_thumb_soft(260u, y, vizs[k]);            /* software path: the reference */
        }
        fb_fence();
        for (uint32_t k = 0; k < sizeof vizs / sizeof vizs[0]; k++) {
            const uint32_t y = 8u + k * 34u;
            int diff = 0;
            for (uint32_t j = 0; j < METER_THUMB_H; j++) for (uint32_t i = 0; i < METER_THUMB_W; i++)
                if (mem[base + (y + j) * FB_STRIDE + 60u + i] != mem[base + (y + j) * FB_STRIDE + 260u + i]) diff++;
            if (diff) bad_prev++;
            char m[96]; snprintf(m, sizeof m, "displayed buffer %u: preview of viz %u equals the software one", disp, vizs[k]);
            check(diff == 0, m, (int)diff, 0);
        }
        char m[96]; snprintf(m, sizeof m, "displayed buffer %u: sticky bases are back to 0,0 and the CPU buffer is untouched", disp);
        check(blt_field[0] == 0 && blt_field[2] == 0 && r_cpu == disp, m, (int)blt_field[2], (int)r_cpu);
    }
    printf(fails ? "%d of %d FAILED\n" : "PASSED (%d checks, %d failures)\n", fails ? fails : tests, fails ? tests : fails);
    return fails ? 1 : 0;
}
