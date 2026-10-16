/* Player 2 page crossfade (the old player's Settings transition): a page change is drawn into the idle buffer as usual, but instead of flipping to it the picture is held there and the shown buffer is blended toward it in
 * five steps of about 20 ms (source weights 1/5, 1/4, 1/3, 1/2 and then an exact copy, which telescope to the new picture). The steps run from the main loop (xfade_tick), so the audio is never starved by a blocking fade. */
#ifndef P2_XFADE_H
#define P2_XFADE_H
void xfade_request(void);     /* call inside a full-redraw frame, before drawing the new page: the frame will be held for a fade (no-op without a blend or a second buffer) */
void xfade_tick(void);        /* once per loop pass: the next step when it is time */
void xfade_finish(void);      /* the rest of the fade at once (the user pressed a key, or the view is being left) */
int  xfade_active(void);
#endif
