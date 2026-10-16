/* The persisted look: accent colour, theme, mode and meter, in the settings words (cores/settings_core.h). */
#ifndef P2_APPEARANCE_H
#define P2_APPEARANCE_H
#include <stdint.h>
void appearance_get(uint32_t *sv);           /* the current values into the settings vector */
void appearance_set(const uint32_t *sv);     /* adopt the saved values (or a change made in Core Settings) */
#endif
