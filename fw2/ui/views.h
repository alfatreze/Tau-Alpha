/* Player 2 views: the table of view_t (cores/view_core.h) and its Helios regions. One place a view is left (views_switch); each view's regions are marked dirty when it is entered and as its content changes;
 * views_flush() draws the dirty regions the beam is not in. */
#ifndef P2_VIEWS_H
#define P2_VIEWS_H
#include <stdint.h>
enum { V_NOWPLAYING = 0, V_LIBRARY, V_SETTINGS, V_INFO, V_REPORT, V_HALCYON, V_FULLSCREEN, VIEW_COUNT };
typedef struct { void (*info_init)(void); void (*info_key)(uint32_t keys); void (*info_draw)(void); void (*report_draw)(void); } views_diag_t;
void     views_set_diag(const views_diag_t *d);   /* before views_init: the Info and Report pages (diagnostics) */
void     views_init(void);                  /* registers the regions, enters Now Playing */
void     views_refresh(void);              /* the colours changed: the current view repaints itself in place */
void     views_switch(int to);              /* a view id, or VW_BACK */
void     views_input(uint32_t edge);        /* a key edge to the current view */
void     views_mark(void);                  /* once per loop pass: the current view marks what needs a redraw */
void     views_flush(void);                 /* once per loop pass: draws the dirty regions whose rows are safe */
uint32_t views_cur(void);
void     views_report_refresh(void);        /* the report is on screen: draw it again */
#endif
