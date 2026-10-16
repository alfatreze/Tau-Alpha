/* Player 2 report page: the counter registry as a pixel grid (spec P9), so ONE screenshot carries every counter: `python3 tools/decode_tau_suite.py --grid shot.png --table`. */
#ifndef P2_REPORT_PAGE_H
#define P2_REPORT_PAGE_H
#include <stdint.h>
void report_page_set_payload(const uint8_t *p, uint32_t n);   /* the next draw shows this meter config (SR_T_METERCFG) instead of the counters, once */
void report_page_draw(void);        /* snapshots the counters now and draws the whole screen (about 0.3 s) */
#endif
