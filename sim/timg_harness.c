/* Host harness for fw/timg.inc (B-285): emulates the tag-buffer slot read, the SDRAM mailbox, the draw engine's BLIT/CBLIT and the
 * CLUT on a 512-wide word memory, runs the real firmware code on a real .timg file, and dumps what reached the art stash so
 * sim/test_tau_timg.py can compare it with the Python decoder. Usage: harness <file.timg> <out.bin> <hw_swap 0|1> <mailbox_ok 0|1> */
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
#define REG(a) (*reg_ptr(a))
static uint32_t clut_sink;
static uint32_t *reg_ptr(uint32_t a) {
    if (a == R_CLUT_IDX) { clut_idx = 0u; return &clut_sink; }                 /* write index 0 */
    if (a == R_CLUT_DATA) return &clut32[clut_idx++ & 255u];                   /* each store lands in the next entry (auto-increment) */
    uint32_t i = (a - R_SDR_ADDR) / 4u;
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

static void fb_wait(void) {}
static int  BLIT_READY(void) { return 1; }
static void blit_probe_ensure(void) {}
static void fb_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) sdram[(dy + y) * FB_STRIDE + dx + x] = sdram[(sy + y) * FB_STRIDE + sx + x];
}
static uint32_t cblit_calls, max_cblit_w;
static void fb_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
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
    FILE *f = fopen(argv[1], "rb");
    file_ok = f != NULL;
    if (f) { fseek(f, 0, SEEK_END); file_len = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET); file = malloc(file_len); fread(file, 1, file_len, f); fclose(f); }
    char path[LIB_MAX_PATH + 40u];
    uint32_t sig = timg_cover_path(path, sizeof(path));
    printf("path=%s sig=%u\n", path, sig);
    int ok = 0;
    int e = timg_load(path);
    printf("load=%d ok=%u swap=%u w=%u h=%u\n", e, timg_ok, timg_swap, timg_w, timg_h);
    if (e == TIMG_OK) { timg_draw_art(); ok = 1; }
    printf("cblit=%u maxw=%u mounts=%d\n", cblit_calls, max_cblit_w, ui_mounts);
    if (ok) {
        FILE *o = fopen(argv[2], "wb");
        for (uint32_t y = 0; y < ART_IMG; y++) fwrite(&sdram[(ART_STASH_Y + ART_PAD + y) * FB_STRIDE + ART_PAD], 2, ART_IMG, o);
        fclose(o);
    }
    return e;
}
