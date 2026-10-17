/* Player 2 Settings > CONTROLS page (fw2/cores/controls_core.h): every button and combination, with icons. */
#ifndef P2_CONTROLS_VIEW_H
#define P2_CONTROLS_VIEW_H
#include <stdint.h>
void controls_view_enter(void);
void controls_view_redraw(void);              /* repaint in place */
int  controls_view_input(uint32_t edge);      /* VW_STAY or VW_BACK */
void controls_view_mark(void);                /* once per loop pass */
void controls_view_draw(void);                /* the region's redraw */
#endif
