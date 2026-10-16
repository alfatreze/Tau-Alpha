#include "library.h"
#include "../platform/mmio.h"
#include "slot.h"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../cores/library_core.h"

#define PS_BASE   0xA4000000u
#define LIB_IMG   ((volatile uint8_t *)(uintptr_t)(PS_BASE + 0x010000u))
#define LIB_CAP   0x400000u
#define LIB_Q     ((uint16_t *)(uintptr_t)(PS_BASE + 0x510000u))

extern char _tag_start;
uint8_t lib_state, lib_err; uint32_t lib_ms, lib_n_tracks;
static lib_t lib;

static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx; (void)dst;
    return slot_read_blocking(SLOT_LIB, off, (uint32_t)(uintptr_t)&_tag_start, len, CLK_HZ * 5u);
}

static int psram_ok(void)           /* the window does not exist on an older bitstream: prove it before the first store */
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG_RD(R_PS_ID) != 0x50535231u || !((REG_RD(R_PS_CFG) >> 16) & 1u)) return 0;
    volatile uint32_t *w[2] = { (volatile uint32_t *)LIB_IMG, (volatile uint32_t *)(LIB_IMG + LIB_CAP - 4u) };
    for (uint32_t k = 0; k < 2u; k++) for (uint32_t i = 0; i < 2u; i++) { w[i][0] = pat[k] ^ i; if (w[i][0] != (pat[k] ^ i)) return 0; }
    return 1;
}

void library_load(void)
{
    uint32_t t0 = cycles();
    lib_state = 0; lib_err = 0;
    if (!slot_read_blocking(SLOT_LIB, 0u, (uint32_t)(uintptr_t)&_tag_start, 1u, CLK_HZ * 3u)) { lib_state = 2; lib_err = LIB_E_NOFILE; return; }
    if (!psram_ok()) { lib_state = 2; lib_err = 0x80; return; }
    uint8_t *win = (uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start);      /* the tag zone through the uncached alias */
    int e = lib_load(&lib, rd, 0, win, LIB_IMG, LIB_CAP);
    lib_ms = (cycles() - t0) / (CLK_HZ / 1000u);
    if (e) { lib_state = 2; lib_err = (uint8_t)e; return; }
    lib_state = 1; lib_n_tracks = lib.n_tracks;
}
uint32_t library_path(uint32_t id, char *out, uint32_t cap) { return lib_state == 1 && id < lib.n_tracks ? lib_path(&lib, id, out, cap) : 0; }
uint32_t library_size(uint32_t id) { lib_det_t d; return lib_state == 1 && lib_details(&lib, id, &d) ? d.size : 0; }
uint32_t library_title(uint32_t id, char *out, uint32_t cap) { lib_track_t t; if (lib_state != 1 || id >= lib.n_tracks) return 0; lib_track(&lib, id, &t); return lib_str(&lib, t.title, out, cap); }
uint16_t *library_queue_buf(void) { return LIB_Q; }
uint16_t *library_orig_buf(void) { return (uint16_t *)(uintptr_t)(PS_BASE + 0x518000u); }
