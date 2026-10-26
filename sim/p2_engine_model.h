/* A host model of the draw engine as the RTL has it (mp3_fb.sv), enough to test the Player 2 graphics layer and everything above it: two display buffers of 512 x 360 pixels with the stash rows below shared by both
 * (dbuf_addr: only addresses below 184320 follow R_DBUF_CPU), RECT / BLIT / SBLIT / CBLIT, the sticky SRC_BASE / DST_BASE / BLEND fields read WHEN A COMMAND EXECUTES, a command queue that drains only as time passes
 * (one command per read of the cycle counter, so a missing fence shows), the SDRAM mailbox (two pixels a word, no ordering with the queue), and a flip that is applied at a vblank some time after it is requested.
 * Times: reading R_CYCLES advances 3000 cycles; one command executes per read; a vblank passes every VBLANK_READS reads. */
#ifndef P2_ENGINE_MODEL_H
#define P2_ENGINE_MODEL_H
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define EM_BUF_PX 1048576u
#define EM_VISIBLE 184320u
#define EM_STRIDE 512u
#define VBLANK_READS 9u
typedef struct { uint32_t op, addr, size, color, go; } em_cmd_t;
static uint16_t em_mem[2u * EM_BUF_PX];
static em_cmd_t em_q[4096]; static uint32_t em_qh, em_qt;
static uint32_t em_fb_addr, em_fb_size, em_fb_color, em_cycles, em_reads;
static uint32_t em_blt_idx, em_src_base, em_dst_base, em_blend_on, em_blend_a;
static uint32_t em_cpu, em_disp, em_flip_pending, em_has_dbuf = 1, em_has_blend = 1;
static uint32_t em_sdr_addr, em_sdr_data, em_sdr_rdata, em_clut_idx; static uint16_t em_clut[256];
static uint32_t em_nflips, em_executed, em_torn;
static uint32_t (*em_ext_rd)(uint32_t a); static void (*em_ext_wr)(uint32_t a, uint32_t v);      /* registers outside the draw engine (a test supplies the ones its module reads) */
static uint32_t em_dbuf_addr(uint32_t a, uint32_t sel) { return (em_has_dbuf && sel && a < EM_VISIBLE) ? a + EM_BUF_PX : a; }
static uint16_t em_blend_px(uint16_t s, uint16_t d, uint32_t a)
{
    uint32_t sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31, dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    return (uint16_t)((((sr * a + dr * (256 - a)) >> 8) << 11) | (((sg * a + dg * (256 - a)) >> 8) << 5) | ((sb * a + db * (256 - a)) >> 8));
}
static void em_exec(const em_cmd_t *c)
{
    const uint32_t w = c->size & 0x1FFu, h = (c->size >> 9) & 0x1FFu, op = c->go & 7u;
    em_executed++;
    if (op == 1u) {                                                  /* RECT */
        for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) em_mem[em_dbuf_addr(c->addr + j * EM_STRIDE + i, em_cpu)] = (uint16_t)c->color;
    } else if (op == 5u) {                                           /* BAR: lit = cmd_glyph (go[9:3]) with cmd_glyph_hi (go[16:15]) above it, clamped to h; unlit rows on top in the background colour, lit rows below in the foreground (mp3_fb.sv OP_BAR) */
        uint32_t lit = ((c->go >> 3) & 0x7Fu) | (((c->go >> 15) & 3u) << 7); if (lit > h) lit = h;
        for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) em_mem[em_dbuf_addr(c->addr + j * EM_STRIDE + i, em_cpu)] = (uint16_t)(j < h - lit ? (c->color >> 16) : (c->color & 0xFFFFu));
    } else if (op == 4u || op == 6u || op == 7u) {                   /* BLIT / SBLIT / CBLIT: addressed through the sticky bases */
        const uint32_t src = ((c->color & 7u) << 16) | (c->color >> 16);
        const uint32_t sx = (op == 6u) ? (((c->go >> 10) & 3u) ? ((c->go >> 10) & 3u) : 1u) : 1u, sy = (op == 6u) ? (((c->go >> 12) & 3u) ? ((c->go >> 12) & 3u) : 1u) : 1u;
        for (uint32_t j = 0; j < h * sy; j++) for (uint32_t i = 0; i < w * sx; i++) {
            const uint32_t s = em_src_base + src + (j / sy) * EM_STRIDE + (i / sx), d = em_dst_base + c->addr + j * EM_STRIDE + i;
            uint16_t v = em_mem[s];
            if (op == 7u) v = em_clut[v & 255u];
            else if (op == 4u && em_blend_on && em_has_blend) v = em_blend_px(v, em_mem[d], em_blend_a);
            em_mem[d] = v;
        }
    }
}
static void em_step(void)                                           /* time passes: one queued command executes, a vblank may come */
{
    if (em_qh != em_qt) em_exec(&em_q[em_qh++ & 4095u]);
    if (++em_reads % VBLANK_READS == 0u && em_flip_pending) { if (em_qh != em_qt) em_torn++; em_disp ^= 1u; em_flip_pending = 0; em_nflips++; }
}
static void em_drain(void) { while (em_qh != em_qt) em_exec(&em_q[em_qh++ & 4095u]); }
uint32_t host_rd(uint32_t a)
{
    switch (a) {
    case 0x8000000Cu: em_cycles += 3000u; em_step(); return em_cycles;
    case 0x80000054u: return 0u;                                      /* R_FB_GO: the FIFO is never full */
    case 0x80000084u: return 0u;                                      /* mailbox status: never busy */
    case 0x80000080u: return em_sdr_rdata;
    case 0x8000011Cu: return (em_has_dbuf ? 0x80000000u : 0u) | (em_flip_pending << 1) | em_disp;
    case 0x80000118u: return (em_has_dbuf ? 0x80000000u : 0u) | em_cpu;
    default: return em_ext_rd ? em_ext_rd(a) : 0u;
    }
}
void host_wr(uint32_t a, uint32_t v)
{
    switch (a) {
    case 0x80000048u: em_fb_addr = v; break;
    case 0x8000004Cu: em_fb_size = v; break;
    case 0x80000050u: em_fb_color = v; break;
    case 0x80000054u: em_q[em_qt++ & 4095u] = (em_cmd_t){ 0, em_fb_addr, em_fb_size, em_fb_color, v }; break;
    case 0x800000C0u: em_blt_idx = v; break;
    case 0x800000C4u:
        if (em_blt_idx == 0u) em_src_base = v; else if (em_blt_idx == 2u) em_dst_base = v;
        else if (em_blt_idx == 5u) { em_blend_on = v & 1u; em_blend_a = (v >> 8) & 0xFFu; }
        em_blt_idx = (em_blt_idx + 1u) % 7u; break;
    case 0x80000118u: if (em_has_dbuf) em_cpu = v & 1u; break;
    case 0x8000011Cu: if (em_has_dbuf && (v & 1u)) em_flip_pending = 1u; break;
    case 0x80000074u: em_sdr_addr = v; break;
    case 0x80000078u: em_sdr_data = v; break;
    case 0x8000007Cu:
        if (v == 0x3Fu) { em_mem[em_sdr_addr] = (uint16_t)em_sdr_data; em_mem[em_sdr_addr + 1u] = (uint16_t)(em_sdr_data >> 16); }
        else if (v == 0x3Du) em_sdr_rdata = (uint32_t)em_mem[em_sdr_addr] | ((uint32_t)em_mem[em_sdr_addr + 1u] << 16);
        break;
    case 0x800000C8u: em_clut_idx = v & 255u; break;
    case 0x800000CCu: em_clut[em_clut_idx++ & 255u] = (uint16_t)v; break;
    default: if (em_ext_wr) em_ext_wr(a, v); break;
    }
}
static uint16_t em_px(uint32_t buf, uint32_t x, uint32_t y) { return em_mem[(buf ? EM_BUF_PX : 0u) + y * EM_STRIDE + x]; }
static void em_reset(void)
{
    memset(em_mem, 0, sizeof em_mem); em_qh = em_qt = 0; em_cpu = em_disp = em_flip_pending = 0; em_src_base = em_dst_base = 0; em_blend_on = 0; em_nflips = 0; em_executed = 0; em_torn = 0;
    for (uint32_t i = 0; i < 256u; i++) em_clut[i] = (uint16_t)i;
}
#endif
