/* Player 2 icons (docs/features/FONT_SPEC.md 13): the 14 player icons drawn with the palette blit. Cold code. */
#ifndef P2_ICON_H
#define P2_ICON_H
#include <stdint.h>
#include "../generated/icon_data.h"
#define ICON_H 28u
#define ICON_PLANE_Y 560u            /* the stash rows the planes sit in (28 rows, the icons side by side from x = 0: 263 columns) */
uint32_t icon_width(uint32_t id);    /* 0 for an unknown id */
/* Draws icon `id` with its top-left at (x, y) in the buffer being drawn into: the 16-entry colour table is the background blended toward the foreground, loaded now (the table is shared with the art and the meter
 * previews, which load their own); the first call builds the planes (one rectangle per run of equal pixels, once). Returns 0, or 1 when the icon record is unusable or the id is unknown. */
int icon_draw(uint32_t id, uint32_t x, uint32_t y, uint16_t fg, uint16_t bg);
int icon_draw_stash(uint32_t id, uint32_t x, uint32_t y, uint16_t fg, uint16_t bg);   /* the same into the off-screen stash (rows 360 and below, the same in both buffers): the Check page test reads it back */
#endif
