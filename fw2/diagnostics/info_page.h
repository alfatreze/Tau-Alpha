/* Player 2 Info page: draws the counter registry as rows (name left, value right), scrolls with Up/Down (wrapping), refreshed a few times a second. */
#ifndef P2_INFO_PAGE_H
#define P2_INFO_PAGE_H
#include <stdint.h>
void info_page_init(void);
void info_page_key(uint32_t edge);        /* rising edges */
void info_page_draw(void);                /* rows only; the frame is drawn once */
#endif
