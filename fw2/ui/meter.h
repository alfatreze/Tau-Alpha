/* Player 2 spectrum meter (spec section 11): 16 bars from the hardware filter bank, a falling peak cap per bar. Draws only what changed; the bank is read when a new 1024-sample window is complete (zero CPU for the analysis itself). */
#ifndef P2_METER_H
#define P2_METER_H
#include <stdint.h>
void meter_enter(uint32_t y, uint32_t h);       /* the box: clear it, forget the bars */
void meter_draw(int allowed);                   /* allowed = 0 while the governor has shed the UI: the bars are left as they are */
#endif
