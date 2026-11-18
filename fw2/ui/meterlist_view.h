/* The page of Settings > METERS (fw2/ui/meterlist_view.c; its own row of the main menu below APPEARANCE since 0.7.5): the scrolling list of the TOC's visible rows (up to 64), seven at a time, with previews, a position indicator and a settings icon on every row that can be configured. */
#ifndef P2_METERLIST_VIEW_H
#define P2_METERLIST_VIEW_H
#include <stdint.h>
enum { MLG_A = 1, MLG_X = 2 };                                   /* the key that asks */
enum { MLG_REFUSED = 1, MLG_PICKED = 2, MLG_CONFIGURE = 3 };     /* the answer of meterlist_guard */
/* The whole page: title line with the position "12 / 64", the seven rows from list position `top` (`sel` highlighted, `cur` is the meter on screen: its star), the hints. EVERY pixel of the 400 x 360 page is drawn or cleared. */
void     meterlist_draw(const char *title, uint32_t top, uint32_t sel, uint32_t n, uint32_t cur);
/* A (set) or X (Configure) on list position i. A row this firmware cannot run is refused for both (nothing changes, one short line says why: msg 1 for A, 2 for X). A SETS the meter (meters_select: the same path as X on Now Playing; the
 * saved word follows after the commit window) and answers MLG_PICKED. X does NOT select: a meter with no parameters is refused (msg 3), otherwise its pack is loaded for the Configure page (meters_cfg_begin: the set meter is remembered and
 * comes back when the page is left; a pack that cannot be opened is refused, msg 4, and the set meter is still there) and the answer is MLG_CONFIGURE. */
uint32_t meterlist_guard(uint32_t i, uint32_t what);
uint32_t meterlist_msg(void);                                    /* 1 to 4 while a refusal line is shown (see above) */
void     meterlist_msg_clear(void);                              /* any key */
const char *meterlist_cur_name(char *o, uint32_t n);             /* the Appearance row's value: the meter on screen by its TOC name, cut to fit (n >= 24) */
#endif
