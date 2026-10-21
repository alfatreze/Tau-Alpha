/* The previews of the METER list (fw2/ui/meterlist_thumb.c). */
#ifndef P2_METER_THUMB_H
#define P2_METER_THUMB_H
#include <stdint.h>
#define METER_THUMB_W 56u
#define METER_THUMB_H 32u
void meter_thumb_draw(uint32_t m, uint32_t x, uint32_t y);      /* meter m's preview with its top-left at (x, y), in the buffer being drawn into */
#endif
