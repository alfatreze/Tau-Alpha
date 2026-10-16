/* Player 2 library browser state (transport side): the navigation core over the real library, and the rows the view draws. A Play on a track row builds the queue and starts it. */
#ifndef P2_BROWSER_H
#define P2_BROWSER_H
#include <stdint.h>
#define BROWSER_ROWS 9u
typedef struct {
    char head[28];                         /* the level's name */
    char rows[BROWSER_ROWS][44]; uint8_t is_track[BROWSER_ROWS];
    uint32_t sel, top, count, depth, n;    /* n = rows filled */
    uint32_t gen;                          /* changes whenever the content changed */
} browser_view_t;
void browser_enter(void);                  /* open at Home (or where it was left) */
int  browser_key(uint32_t edge);           /* 1 = leave the browser (Back at Home, or a track was started) */
const browser_view_t *browser_view(void);
#endif
