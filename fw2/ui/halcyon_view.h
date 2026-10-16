/* Player 2 Halcyon EQ page (Settings > Audio > HALCYON EQ; the design of the previous player's fw/halcyon_page.inc): the live response curve of the cascade you are setting on a log frequency axis with the selected
 * control's band lit, and below it the six controls as centre-zero sliders. Row 0 is the preset area (Left/Right change the preset), rows 1..6 the controls (Left/Right adjust, applied LIVE). L1/R1 step
 * the presets inside a gain dip, X compares against the EQ off, Y resets the six controls, B goes back. The controls of CUSTOM are not saved; the preset selection is (persist id 16). */
#ifndef P2_HALCYON_VIEW_H
#define P2_HALCYON_VIEW_H
#include <stdint.h>
void halcyon_view_enter(void);
void halcyon_view_redraw(void);              /* repaint in place */
int  halcyon_view_input(uint32_t edge);      /* VW_STAY or VW_BACK */
void halcyon_view_mark(void);                /* once per loop pass */
void halcyon_view_draw(void);                /* the region's redraw */
#endif
