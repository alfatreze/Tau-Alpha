/* Player 2 meter Configure page (Settings > Appearance > Meter: configure): the live meter on top, the meter's parameters below (cores/configure_core.h). Edits are for this session; the selected preset is what is saved. */
#ifndef P2_CONFIGURE_VIEW_H
#define P2_CONFIGURE_VIEW_H
#include <stdint.h>
void configure_view_enter(void);
void configure_view_redraw(void);          /* the colours changed: repaint in place, keep the selection */
int  configure_view_input(uint32_t edge);  /* VW_STAY or VW_BACK */
void configure_view_mark(void);            /* once per pass: the preview animates every pass, the list when something changed */
void configure_view_draw_preview(void);
void configure_view_draw_list(void);
#endif
