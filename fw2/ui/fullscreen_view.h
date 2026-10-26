/* Player 2 fullscreen meter (Select+Y, as before): the meter takes the whole figure area (400 x 323) and the foot shows "title . artist . album" and a thin progress line with a tick (accent while playing,
 * red while paused or stopped). The player keys stay as on Now Playing (the input is Now Playing's); Select+Y again or Start leaves. */
#ifndef P2_FULLSCREEN_VIEW_H
#define P2_FULLSCREEN_VIEW_H
#include <stdint.h>
void fullscreen_view_enter(void);
void fullscreen_view_mark(void);            /* once per loop pass: marks the figure and the foot */
void fullscreen_draw_fig(void);             /* the figure region's redraw: the meter */
void fullscreen_draw_foot(void);            /* the foot region's redraw: the text line and the progress line */
#endif
