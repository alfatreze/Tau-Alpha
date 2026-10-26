/* Player 2 Check (Settings > Diagnostics > CHECK): the audio tests that come back from the old player, run while a track plays. The verdicts also feed two rows of the counter registry (Info and the report grid). */
#ifndef P2_CHECK_PAGE_H
#define P2_CHECK_PAGE_H
#include <stdint.h>
void check_page_enter(void);
void check_page_repaint(void);            /* the colours changed: draw again, keep the results */
int  check_page_key(uint32_t edge);       /* 1 = leave the page */
void check_page_tick(void);               /* once per loop pass while the page is current: advances the running test */
void check_page_draw(void);
void check_page_exit(void);               /* restores Normal rate and stops a running check */
uint32_t check_page_dirty(void);
extern uint32_t check_mask, check_under;  /* 2 bits per test (0 not run, 1 pass, 2 fail, 3 skipped); FIFO stalls seen while testing */
#endif
