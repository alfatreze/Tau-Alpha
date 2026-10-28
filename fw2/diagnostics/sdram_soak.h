/* The SDRAM soak driver (Diagnostic Build): toggled from the Check page (X), run from the main loop through soak_step() while soak_run is set. See cores/sdram_soak_core.h. */
#ifndef P2_SDRAM_SOAK_H
#define P2_SDRAM_SOAK_H
#include <stdint.h>
#include "../cores/sdram_soak_core.h"
extern volatile uint8_t soak_run;      /* hot: the main loop reads it, the cold driver sets it */
void soak_step(void);                  /* cold: a slice of work sized to the elapsed time and the level */
void soak_cycle(void);                 /* off, level 1 (16 k ops/s), 2 (64 k), 3 (128 k), off */
uint32_t soak_level(void);
extern sk_t soak_st;                   /* the counters (Info rows) */
#endif
