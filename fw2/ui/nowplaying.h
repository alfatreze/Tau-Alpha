/* Player 2 Now Playing view (spec section 11): draws the heard track's text, the clock and the bar from np_info_t (the transport's view of what is HEARD), only the fields that changed.
 * It owns pixels and nothing else: it never touches transport state. */
#ifndef P2_NOWPLAYING_H
#define P2_NOWPLAYING_H
void nowplaying_enter(void);     /* clear the screen and forget what was drawn */
void nowplaying_draw(void);      /* cheap when nothing changed; call every loop */
#endif
