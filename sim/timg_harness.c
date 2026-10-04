/* Host harness for fw/timg.inc (B-285): emulates the tag-buffer slot read, the SDRAM mailbox, the draw engine's BLIT/CBLIT and the
 * CLUT on a 512-wide word memory, runs the real firmware code on a real .timg file, and dumps what reached the art stash so
 * sim/test_tau_timg.py can compare it with the Python decoder. Usage: harness <file.timg> <out.bin> <hw_swap 0|1> <mailbox_ok 0|1>
 * [overlay 0|1] [engine_delay_reads N] [retry 0|1]
 *   overlay: a menu is up (FB_HELD() is true unless ov_draw is set): every engine command outside the exemption is dropped, as on the Pocket
 *   engine_delay_reads: a queued engine copy only becomes visible after N mailbox reads (the draw engine still working through a burst)
 *   retry: after a failed attempt fix the mailbox and load again (a probe failure must not be remembered) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLK_HZ 60000000u
#define COLD_FN3
#define FB_STRIDE 512u
#define ART_STASH_Y 360u
#define ART_PAD 0u
#define ART_IMG 128u
#define ART_H (ART_IMG + 2u * ART_PAD)
#define THUMB_LIVE_SLOTS 11u
#define LIB_MAX_PATH 200u
#define R_SDR_ADDR   0x80000074u
#define R_SDR_DATA   0x80000078u
#define R_SDR_CTRL   0x8000007Cu
#define R_SDR_RDATA  0x80000080u
#define R_SDR_STATUS 0x80000084u
#ifndef CLUT_START_IDX
#define CLUT_START_IDX 255u      /* must equal fw/player.c's (sim/test_clut_contract.py checks that) */
#endif
#define R_CLUT_IDX   0x800000C8u
#define R_CLUT_DATA  0x800000CCu

static int hw_swap, mailbox_ok;
static uint16_t sdram[1024u * 512u];
static uint32_t regs[8], clut_idx;
static uint32_t clut32[256];
static uint32_t cyc;
static uint32_t cycles(void) { cyc += 100u; return cyc; }
static uint32_t mb[5];                                        /* addr, data, ctrl, rdata, status */
static uint32_t *reg_ptr(uint32_t a);
static int engine_delay, pend_left = -1;
static uint32_t pb[6];
static void do_copy(void) { for (uint32_t y = 0; y < pb[5]; y++) for (uint32_t x = 0; x < pb[4]; x++) sdram[(pb[3] + y) * FB_STRIDE + pb[2] + x] = sdram[(pb[1] + y) * FB_STRIDE + pb[0] + x]; }
#define REG(a) (*reg_ptr(a))
#ifndef CLUT_SKEW
#define CLUT_SKEW 1u
#endif
static uint32_t *reg_ptr(uint32_t a) {
    /* B-570: models the RTL as it is, not as documented. mp3_soc.v raises a one-cycle write pulse and advances clut_idx on the same
     * edge, while clut_waddr follows clut_idx, so a DATA write lands one slot ABOVE the index it was issued at (CLUT_SKEW 1). Build with
     * -DCLUT_SKEW=0 for a bitstream whose write address is registered with the pulse. The old model (index forced to 0, each store in
     * the next entry) is what let this ship. */
    if (a == R_CLUT_IDX) return &clut_idx;                                     /* the written value becomes the index */
    if (a == R_CLUT_DATA) { uint32_t slot = (clut_idx + CLUT_SKEW) & 255u; clut_idx = (clut_idx + 1u) & 255u; return &clut32[slot]; }
    uint32_t i = (a - R_SDR_ADDR) / 4u;
    if (a == R_SDR_RDATA && pend_left >= 0) { if (pend_left == 0) { do_copy(); pend_left = -1; } else pend_left--; }
    if ((a == R_SDR_STATUS || a == R_SDR_RDATA) && mb[2] != 0u) {
        uint32_t c = mb[2], addr = mb[0], data = mb[1];
        if (mailbox_ok) {
            if (c & 2u) { uint16_t lo = (uint16_t)data, hi = (uint16_t)(data >> 16);
                          sdram[addr] = hw_swap ? hi : lo; sdram[addr + 1u] = hw_swap ? lo : hi; }
            else { uint32_t lo = sdram[addr], hi = sdram[addr + 1u]; mb[3] = hw_swap ? ((lo << 16) | hi) : ((hi << 16) | lo); }
        } else mb[3] = 0xDEADBEEFu;
        mb[4] = 0; mb[2] = 0u;
    }
    return &mb[i];
}

static uint8_t ov_draw, ov_up;                                 /* B-325: FB_HELD() semantics */
#define FB_HELD() (ov_up && !ov_draw)
static void fb_wait(void) {}
static int  BLIT_READY(void) { return 1; }
static void blit_probe_ensure(void) {}
static void fb_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    if (!w || !h || FB_HELD()) return;
    pb[0] = sx; pb[1] = sy; pb[2] = dx; pb[3] = dy; pb[4] = w; pb[5] = h;
    if (engine_delay <= 0) do_copy(); else pend_left = engine_delay;       /* lands after that many mailbox reads */
}
static uint32_t cblit_calls, max_cblit_w;
static void fb_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    if (!w || !h || FB_HELD()) return;
    cblit_calls++; if (w > max_cblit_w) max_cblit_w = w;
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++)
        sdram[(dy + y) * FB_STRIDE + dx + x] = (uint16_t)clut32[sdram[(sy + y) * FB_STRIDE + sx + x] & 0xFFu];
}

/* the slot: the file, read like the host does (a read that extends past the end fails) */
static uint8_t *file; static uint32_t file_len; static int file_ok;
static uint8_t tagbuf[4096];
#define TAG_OFF 0u
#define TAG_SIZE 4096u
static int pl_open_into(uint32_t slot, const char *name) { (void)slot; (void)name; return file_ok; }
static int target_read_slot(uint32_t slot, uint32_t off, uint32_t dst, uint32_t len) {
    (void)slot; (void)dst;
    if (off + len > file_len) return 0;
    memcpy(tagbuf, file + off, len); return 1;
}
static int ui_mounts;
static void ui_art_mount(void) { ui_mounts++; }

/* library stubs for timg_cover_path */
static uint8_t lib_src = 1; static uint32_t lib_qn = 1, lib_qpos, lib; static uint16_t LIB_Q[1] = { 7 };
static const char *test_track = "/Assets/tau_test/common/Album One/01 Track.mp3";
static int lib_path(const void *l, uint32_t id, char *out, uint32_t cap) { (void)l; (void)id; strncpy(out, test_track, cap); return 1; }
#define lib_path(l, id, o, c) lib_path(&lib, id, o, c)

#include "../fw/timg.inc"

int main(int argc, char **argv) {
    if (argc < 5) return 2;
    hw_swap = atoi(argv[3]); mailbox_ok = atoi(argv[4]);
    ov_up = argc > 5 ? (uint8_t)atoi(argv[5]) : 0u;
    engine_delay = argc > 6 ? atoi(argv[6]) : 0;
    const int retry = argc > 7 ? atoi(argv[7]) : 0;
    const int repeat = argc > 8 ? atoi(argv[8]) : 0;
    FILE *f = fopen(argv[1], "rb");
    file_ok = f != NULL;
    if (f) { fseek(f, 0, SEEK_END); file_len = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET); file = malloc(file_len); fread(file, 1, file_len, f); fclose(f); }
    char path[LIB_MAX_PATH + 40u];
    uint32_t sig = timg_cover_path(path, sizeof(path));
    printf("path=%s sig=%u\n", path, sig);
    int shown = timg_cover(sig, path);                         /* the real entry point, including the FB_HELD exemption */
    printf("load=%d shown=%d err=%u fail=%u miss=%u ok=%u swap=%u w=%u h=%u\n", timg_last_err, shown, timg_last_err, timg_fail_n, timg_miss_sig == sig,
           timg_ok, timg_swap, timg_w, timg_h);
    if (!shown && retry) {                                     /* a failed probe must not be remembered: fix the cause and try again */
        mailbox_ok = 1;
        shown = timg_cover(sig, path);
        printf("retry shown=%d err=%u fail=%u\n", shown, timg_last_err, timg_fail_n);
    }
    for (int i = 0; i < repeat; i++) timg_cover(sig, path);      /* the failure cap: repeated probe failures stop probing */
    if (repeat) printf("repeat fails=%u probe_fails=%u ok=%u\n", timg_fail_n, timg_probe_fails, timg_ok);
    printf("cblit=%u maxw=%u mounts=%d ovdraw_after=%u\n", cblit_calls, max_cblit_w, ui_mounts, ov_draw);
    if (shown) {
        FILE *o = fopen(argv[2], "wb");
        for (uint32_t y = 0; y < ART_IMG; y++) fwrite(&sdram[(ART_STASH_Y + ART_PAD + y) * FB_STRIDE + ART_PAD], 2, ART_IMG, o);
        fclose(o);
    }
    return shown ? 0 : (timg_last_err ? timg_last_err : 1);
}
