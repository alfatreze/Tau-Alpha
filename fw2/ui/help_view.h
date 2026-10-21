/* Player 2 Menu > HELP (fw2/cores/help_core.h): three pages on adding music with Tau Omega, where things are on the card, and the Python alternative. */
#ifndef P2_HELP_VIEW_H
#define P2_HELP_VIEW_H
#include <stdint.h>
void help_view_enter(void);
void help_view_redraw(void);              /* repaint in place */
int  help_view_input(uint32_t edge);      /* VW_STAY or VW_BACK */
void help_view_mark(void);                /* once per loop pass */
void help_view_draw(void);                /* the region's redraw */
#endif
