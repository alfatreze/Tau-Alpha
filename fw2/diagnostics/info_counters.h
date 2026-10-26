/* Player 2 counter registry instance (spec P9, section 12.2): the table that generates the Info page and the report rows. A module adds rows in its own file via
 * CNT_REGISTER-style entries in counters.c (S2 keeps one table; modules contribute fragments from S3 on). */
#ifndef P2_INFO_COUNTERS_H
#define P2_INFO_COUNTERS_H
#include "../cores/counters_core.h"   /* the generic registry logic (cores); this file is the Player 2 INSTANCE of the table, owned by diagnostics */
extern const cnt_t counters_table[];
extern const uint32_t counters_count;
void counters_init(void);
extern uint32_t counters_loops, counters_loop_max_cyc, counters_keys, counters_aout_init;   /* the main loop publishes its own measurements here (the only writer) */
void counters_refresh(void);           /* updates the shadow values derived from cycles and status words (called a few times a second) */
#endif
