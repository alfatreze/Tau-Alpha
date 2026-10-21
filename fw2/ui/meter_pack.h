/* Meter pack ABI for Player 2 (docs/features/meters/METER_PACKS.md): what a loadable meter and the firmware agree on.
 *
 * A pack is one meter's drawing code, compiled freestanding and linked at a fixed address (its slot in the PSRAM code window). It reaches the firmware ONLY through the table below and never names a firmware
 * symbol, so a pack built against ABI N works with every firmware that speaks ABI N however the ROM changes around it. The table is a thin layer over gfx_* (fw2/gfx/gfx.h); a function is added to it only
 * when a meter that is a pack needs it, and every change to the table or to mtr_in_t bumps MTR_PACK_ABI (sim/test_p2_meter_pack.py fails until the fingerprint is updated).
 * Registers are not in the table: a pack includes platform/mmio.h and reads or writes them directly (they are hardware addresses, not firmware symbols).
 * The entry point is  uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api): the same contract as a built-in meter's tick; the return value is the number of draw commands issued. */
#ifndef P2_METER_PACK_H
#define P2_METER_PACK_H
#include <stdint.h>
#include "meter.h"
#include "../cores/meter_pack_core.h"      /* MTR_PACK_ABI, the slot and scratch addresses */

typedef struct {
    uint32_t         abi;                                                   /* MTR_PACK_ABI */
    void           (*rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour);   /* gfx_rect: the draw engine's rectangle fill (clipped around the fullscreen exclusions by the host) */
    const uint16_t  *role;                                                  /* the theme roles, TR_COUNT entries (ui/theme.h); the pack reads them through its own UI_* macros */
    const uint16_t  *params;                                                /* this meter's live setting values, in its manifest's order */
    uint32_t        *stats;                                                 /* MTR_PACK_STATS words the pack may publish: [0] draw commands of the last tick (the host fills the rest); may be NULL */
    uint32_t       (*text)(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg);   /* gfx_text: 1x text with full cells */
    void           (*rect_stash)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour);   /* gfx_rect_stash: the same without the clip to the visible frame (the off-screen stash) */
    int            (*blend_ready)(void);                                    /* gfx_blend_ready: the bitstream has the pipelined blend */
    void           (*blend_on)(uint32_t a);                                 /* gfx_blend_on: source weight a/256 for the blits that follow */
    void           (*blend_blit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);   /* gfx_blend_blit */
    void           (*blend_off)(void);                                      /* gfx_blend_off */
    int            (*mb_write)(uint32_t pixel_addr, uint32_t two_pixels);   /* gfx_mb_write: the SDRAM mailbox, two pixels a word; 0 ok, -1 timeout */
    int            (*mb_read)(uint32_t pixel_addr, uint32_t *two_pixels);   /* gfx_mb_read */
    void           (*fence)(void);                                          /* gfx_fence: every queued draw command has executed */
    void           (*blit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);       /* gfx_blit: a copy inside the buffer being drawn into */
    void           (*blit_abs)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);   /* gfx_blit_abs: stash to stash (self-tests) */
    void           (*sblit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t scale);   /* gfx_sblit: a scaled copy from the stash */
    void           (*bar)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg);   /* gfx_bar: a bar column in one command (the bottom `lit` rows in fg, the rest in bg) */
    void           (*scroll_down)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d);                      /* gfx_scroll_down: the box moves down d rows (the framework's scroll: a few copies, not a redraw) */
} mtr_host_api_t;

typedef uint32_t (*mtr_pack_entry_fn)(const mtr_in_t *in, const mtr_host_api_t *api);
#endif
