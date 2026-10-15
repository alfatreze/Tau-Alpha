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
#endif
