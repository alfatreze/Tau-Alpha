/* Player 2 Settings view: the menu of cores/menu_core.h over the transport's preferences. It edits through mp3play_pref_set (so every change takes effect at once and the persisted ones are saved by the settings service). */
#ifndef P2_SETTINGS_VIEW_H
#define P2_SETTINGS_VIEW_H
#include <stdint.h>
void settings_view_enter(void);
void settings_view_key(uint32_t edge);      /* Up/Down select, Left/Right change */
void settings_view_draw(void);
#endif
