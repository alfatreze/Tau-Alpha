#include "sdram_soak.h"
/* The one hot byte of the soak: not in the cold list of fw2/platform/link.ld, so the main loop can test it without touching the cold image. */
volatile uint8_t soak_run;
