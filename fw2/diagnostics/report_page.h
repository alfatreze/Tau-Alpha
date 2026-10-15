/* Player 2 report page: the counter registry as a pixel grid (spec P9), so ONE screenshot carries every counter: `python3 tools/decode_tau_suite.py --grid shot.png --table`. */
#ifndef P2_REPORT_PAGE_H
#define P2_REPORT_PAGE_H
void report_page_draw(void);        /* snapshots the counters now and draws the whole screen (about 0.3 s) */
#endif
