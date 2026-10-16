/* Player 2 Now Playing view (spec section 11): draws the heard track's text, the clock and the bar from np_info_t (the transport's view of what is HEARD), only the fields that changed.
 * It owns pixels and nothing else: it never touches transport state. */
#ifndef P2_NOWPLAYING_H
#define P2_NOWPLAYING_H
#include <stdint.h>
void nowplaying_enter(void);     /* clear the screen and forget what was drawn */
void nowplaying_draw_head(void);    /* rows 0..210: the heard track's text, clock, bar and cover; cheap when nothing changed */
void nowplaying_draw_meter(void);   /* rows 212..292 */
void nowplaying_draw_foot(void);    /* rows 296..360: volume, tempo, repeat, shuffle, the key hint */
int  nowplaying_input(uint32_t edge);   /* the plain key edges; returns VW_STAY (-1) */
void nowplaying_mark(void);         /* once per loop pass: marks the three regions (their ids are views.h NP_REGIONS) */
extern uint8_t np_art_on;           /* Album art on/off (Settings > Appearance) */
#endif
