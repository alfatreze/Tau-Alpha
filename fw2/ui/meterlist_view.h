/* The page of Settings > Appearance > METER (fw2/ui/meterlist_view.c): the scrolling list of the TOC's visible rows (up to 64), seven at a time, with previews and a position indicator. */
#ifndef P2_METERLIST_VIEW_H
#define P2_METERLIST_VIEW_H
#include <stdint.h>
enum { MLG_A = 1, MLG_X = 2 };                                   /* the key that asks */
enum { MLG_REFUSED = 1, MLG_PICKED = 2, MLG_CONFIGURE = 3 };     /* the answer of meterlist_guard */
/* The whole page: title line with the position "12 / 64", the seven rows from list position `top` (`sel` highlighted, `cur` is the meter on screen: its star), the hints. EVERY pixel of the 400 x 360 page is drawn or cleared. */
void     meterlist_draw(const char *title, uint32_t top, uint32_t sel, uint32_t n, uint32_t cur);
/* A (pick) or X (Configure) on list position i. A row this firmware cannot run is refused (nothing changes, one short line says why). Otherwise the row is selected here (the same path as X on Now Playing:
 * the pack loads, the saved word follows after the commit window) and the answer says what the caller does next. Selecting first is what makes X open Configure for THIS row: the registry loads only the active meter. */
uint32_t meterlist_guard(uint32_t i, uint32_t what);
uint32_t meterlist_msg(void);                                    /* 1 while the refusal line is shown */
void     meterlist_msg_clear(void);                              /* any key */
const char *meterlist_cur_name(char *o, uint32_t n);             /* the Appearance row's value: the meter on screen by its TOC name, cut to fit (n >= 24) */
#endif
