/* Player 2 persistence (spec section 10): the register file APF reads back every frame and saves to its own interact_persist.json. The table is cores/settings_core.h. APF saving
 * disturbs the card stream (it drops the slot's fragment cache), so values are published only when nothing is streaming; the Core Settings menu is watched and a changed word adopted.
 * NEVER replace this with a file write (fw/settings.inc has the history: two such mechanisms destroyed libraries). */
#ifndef P2_SETTINGS_H
#define P2_SETTINGS_H
#include <stdint.h>
#include "../cores/settings_core.h"
typedef struct { uint32_t loads, stores, menu_changes, wide, dirty, migrated, layout; } settings_stats_t;      /* migrated: 1 when this boot converted a legacy (layout version 0) card; layout: the layout version the register file holds now (0 legacy, 1 by area) */
extern settings_stats_t settings_stats;
void settings_init(uint32_t *vals);                       /* probe the wide file, adopt what APF holds (a legacy layout is migrated and written first), defaults if nothing valid */
void settings_seed(uint32_t *vals);                       /* the values as last published: the caller overwrites the fields it owns, so a field nobody sets (layout version, presets of meters that are not there) keeps its saved value */
/* Once per loop. `vals` is the live state (in/out): a change made with the buttons is published when `streaming` is 0; a change made in Core Settings is adopted into `vals`.
 * Returns 1 when vals changed from the menu (the caller applies them). */
int  settings_service(uint32_t *vals, int streaming);
#endif
