/* Player 2 counter registry instance (spec P9, section 12.2): the table that generates the Info page and the report rows. A module adds rows in its own file via
 * CNT_REGISTER-style entries in counters.c (S2 keeps one table; modules contribute fragments from S3 on). */
#ifndef P2_INFO_COUNTERS_H
#define P2_INFO_COUNTERS_H
#include "../cores/counters_core.h"   /* the generic registry logic (cores); this file is the Player 2 INSTANCE of the table, owned by diagnostics */
extern const cnt_t counters_table[];
extern const uint32_t counters_count;
void counters_init(void);
#include "../cores/monoclock_core.h"
extern mc_t counters_up_clk;           /* the uptime clock: the main loop advances it every pass (the only ways it stays exact: mc_update needs a call at least every 64 s, and the Info and Report pages are not open that often) */
extern uint32_t counters_uptime_s;     /* seconds since the player started, written by the main loop from counters_up_clk (read by the Info page, the Report and About) */
extern uint32_t counters_loops, counters_loop_max_cyc, counters_keys, counters_aout_init;   /* the main loop publishes its own measurements here (the only writer) */
void counters_refresh(void);           /* updates the shadow values derived from cycles and status words (called a few times a second) */
#endif
