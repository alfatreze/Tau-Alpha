/* Player 2 minimal graphics layer (S2): rectangles and 1x text through the Talos draw engine. Owns the draw engine's command registers (spec P7). */
#ifndef P2_GFX_H
#define P2_GFX_H
#include <stdint.h>
#define GFX_W 400u
#define GFX_H 360u
#define GFX_STRIDE 512u
#define GFX_CELL_H 16u
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);   /* the same without the clip to the visible frame: for the off-screen stash (rows 360.., columns 400..511) */
/* Draws the string with its full 16 px cells (foreground and background, so nothing underneath needs erasing). Stops before a cell would cross the right edge. Returns the x after the last glyph. */
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg);
uint32_t gfx_text_width(const char *s);
/* Mailbox access to the framebuffer's SDRAM (two 16-bit pixels per 32-bit word) and the draw engine's palette blit. All of these are off the hot path.
 * The columns 400..511 of every row are never displayed: the cells there are scratch. */
#define GFX_CELL_X 480u
int  gfx_mb_write(uint32_t pixel_addr, uint32_t two_pixels);       /* 0 ok, -1 timeout */
int  gfx_mb_read(uint32_t pixel_addr, uint32_t *two_pixels);
void gfx_fence(void);                                                /* every queued draw command has executed (bounded wait) */
void gfx_clut_begin(uint32_t start_index);                           /* waits for the engine to drain, then points the palette at `start_index` */
void gfx_clut_put(uint16_t rgb565);                                  /* next palette entry (auto-increments) */
void gfx_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);   /* indices at (sx,sy) through the palette to (dx,dy); w <= 127 */
void gfx_cblit_abs(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);   /* self-tests: stash to stash, buffer 0 addresses */
void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);    /* copy a block inside the buffer being drawn into; w <= 127, source and destination must not overlap */
void gfx_blit_stash(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);   /* from the shared stash rows (360 and below) into the buffer being drawn into */
void gfx_blit_abs(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);    /* stash to stash (self-tests): buffer 0 addresses, independent of the drawing target */
void gfx_sblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t scale);   /* copy w x h SOURCE pixels (from the stash) scaled by 1, 2 or 3 into the drawing target (the engine clamps the output at 127) */
/* Helios H2 double buffering: which buffer is drawn into, a full redraw into the idle one flipped in at vblank, and an off-screen compose copied onto the displayed buffer. */
uint32_t gfx_cur_buf(void);
uint32_t gfx_front_buf(void);
void gfx_set_target(uint32_t buf);
int  gfx_frame_begin(void);                    /* 1 = drawing into the idle buffer; 0 = no double buffer here (draw directly) */
void gfx_frame_end(int active);                /* fence, flip, wait for the flip, draw into the displayed buffer again */
int  gfx_compose_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* 1 = drawing into the idle buffer for a compose of this box (which starts as a copy of the shown one) */
void gfx_compose_present(uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* copy the box onto the displayed buffer and draw there again */
/* Crossfade: hold_next() keeps the next frame's picture in the idle buffer (no flip); xfade_step(a) moves the shown buffer toward it by a/256 (>= 256: an exact copy); xfade_end() releases the hold. */
void gfx_hold_next(void);
int  gfx_held(void);
void gfx_xfade_step(uint32_t a);
void gfx_xfade_end(void);
/* Alpha blend (OP_BLIT only): on(a), blend_blit(...) any number of times, off() (fences). */
int  gfx_blend_ready(void);
void gfx_blend_on(uint32_t a);
void gfx_blend_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);
void gfx_blend_off(void);
#endif
