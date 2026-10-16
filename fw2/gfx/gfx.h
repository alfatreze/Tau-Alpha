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
void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);    /* copy a block of pixels inside the framebuffer; w <= 127 words, source and destination must not overlap */
void gfx_sblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t scale);   /* copy w x h SOURCE pixels scaled by 1, 2 or 3 (the engine clamps the output at 127) */
#endif
