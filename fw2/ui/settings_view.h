/* Player 2 Settings view (Home > Appearance / Audio / Playback / Diagnostics / Info, the previous player's structure): the pages and navigation are cores/menupages_core.h (host-tested); this file reads the values,
 * applies what the core says changed (a transport preference through mp3play_pref_set, so every change takes effect at once and the persisted ones are saved by the settings service) and draws. */
#ifndef P2_SETTINGS_VIEW_H
#define P2_SETTINGS_VIEW_H
#include <stdint.h>
extern uint8_t blank_sel;                 /* the screen-blank choice (0 off, then 1, 5, 10, 30 minutes) */
void settings_view_enter(void);
int  settings_view_input(uint32_t edge);    /* VW_STAY, or the view to switch to (Now Playing on close, Info, Report) */
void settings_view_mark(void);              /* once per loop pass: marks the region when a value changed elsewhere */
void settings_view_draw(void);              /* the region's redraw */
void settings_view_init_choices(void);      /* the colour, theme and meter option lists (once, after tau-assets.bin is read) */
void settings_view_redraw(void);            /* repaint in place (the colours changed): the page you are on stays */
#endif
