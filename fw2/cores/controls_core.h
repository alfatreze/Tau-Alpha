/* fw2/cores/controls_core.h -- Player 2 Settings > CONTROLS: a list of every button and combination, with the player icons where one says it better than words (play/pause, replay, repeat modes,
 * shuffle, album, and the volume speaker that matches the current volume). A read-only list: Up/Down move the highlight (wrapping, Helios's rule), B or Left goes back. The text is the one place the key
 * map is written down for the user; it follows fw2/app/main_player.c and ui/nowplaying.c. Pure: no drawing, no registers. Host-tested (sim/test_p2_controls.py). */
#ifndef CONTROLS_CORE_H
#define CONTROLS_CORE_H
#include <stdint.h>
#include "helios_core.h"
#include "../generated/icon_data.h"
#ifndef BUG
#define BUG 0
#endif
enum { CI_NONE = 0xFF, CI_VOLUME = 0xFE };                    /* an icon slot: an ICON_* id, nothing, or the speaker for the current volume */
typedef struct { const char *key, *what; uint8_t ic[3]; } ctl_row_t;
#define CTL_VIS 8u
static const ctl_row_t ctl_rows[] = {
    { "A",                "PLAY / PAUSE",      { ICON_PLAY, ICON_PAUSE, CI_NONE } },
    { "B",                "RESTART TRACK",     { ICON_BACK, CI_NONE, CI_NONE } },
    { "LEFT / RIGHT",     "PREV / NEXT",       { CI_NONE, CI_NONE, CI_NONE } },
    { "HOLD LEFT/RIGHT",  "SEEK 5-30 S",       { CI_NONE, CI_NONE, CI_NONE } },
    { "UP / DOWN",        "VOLUME",            { CI_VOLUME, CI_NONE, CI_NONE } },
    { "X",                "NEXT METER",        { CI_NONE, CI_NONE, CI_NONE } },
    { "Y",                "EQ PRESET",         { CI_NONE, CI_NONE, CI_NONE } },
    { "L / R",            "ACCENT COLOUR",     { CI_NONE, CI_NONE, CI_NONE } },
    { "SELECT",           "LIBRARY",           { ICON_NOTE, CI_NONE, CI_NONE } },
    { "SEL + UP/DOWN",    "PREV/NEXT ALBUM",   { ICON_NOTE, CI_NONE, CI_NONE } },
    { "SEL + LEFT/RIGHT", "SEEK 1 S",          { CI_NONE, CI_NONE, CI_NONE } },
    { "SEL + L",          "REPEAT MODE",       { ICON_PLAY_BOX, ICON_REPEAT, ICON_REPEAT_ONE } },
    { "SEL + X",          "METER PRESET",      { CI_NONE, CI_NONE, CI_NONE } },
    { "SEL + Y",          "FULLSCREEN METER",  { CI_NONE, CI_NONE, CI_NONE } },
    { "SEL + B",          "INFO PAGE",         { CI_NONE, CI_NONE, CI_NONE } },
    { "START",            "SETTINGS",          { CI_NONE, CI_NONE, CI_NONE } },
    { "SEL + START",      "REPORT GRID",       { CI_NONE, CI_NONE, CI_NONE } },
    { "MENU > SHUFFLE",   "OFF / ON",          { ICON_SHUFFLE_OFF, ICON_SHUFFLE, CI_NONE } },
    { "A / RIGHT",        "MENU: OPEN",        { CI_NONE, CI_NONE, CI_NONE } },
    { "B / LEFT",         "MENU: BACK",        { CI_NONE, CI_NONE, CI_NONE } },
    { "START",            "MENU: CLOSE",       { CI_NONE, CI_NONE, CI_NONE } },
};
#define CTL_N ((uint8_t)(sizeof(ctl_rows) / sizeof(ctl_rows[0])))
/* The speaker for a volume 0..100: the five ranges of the icon sheet (0 / 1-33 / 34-66 / 67-99 / 100). Names follow the strip order: ICON_MUTE is the crossed speaker, ICON_VOL0 the plain one. */
static inline uint32_t ctl_vol_icon(uint32_t vol)
{
#if BUG == 2
    return vol == 0u ? ICON_MUTE : vol < 33u ? ICON_VOL0 : vol < 66u ? ICON_VOL1 : vol < 99u ? ICON_VOL2 : ICON_VOL3;     /* off-by-one at the range ends */
#else
    return vol == 0u ? ICON_MUTE : vol <= 33u ? ICON_VOL0 : vol <= 66u ? ICON_VOL1 : vol <= 99u ? ICON_VOL2 : ICON_VOL3;
#endif
}
typedef struct { uint8_t sel, top; } ctl_t;
static inline void ctl_open(ctl_t *c) { c->sel = 0; c->top = 0; }
static inline void ctl_fix(ctl_t *c)
{
    uint32_t sel = c->sel, top = c->top;
    if (sel < top) top = sel;
    if (sel >= top + CTL_VIS) top = sel + 1u - CTL_VIS;
    uint32_t maxtop = CTL_N > CTL_VIS ? CTL_N - CTL_VIS : 0u;
    if (top > maxtop) top = maxtop;
    c->top = (uint8_t)top;
}
/* One key edge (bit 0 Up, 1 Down, 2 Left, 5 B): returns 1 when the page should close (back). */
static inline int ctl_input(ctl_t *c, uint32_t edge)
{
    if (edge & ((1u << 0) | (1u << 1))) {
#if BUG == 1
        c->sel = (uint8_t)(((edge & 1u) ? (c->sel ? c->sel - 1u : 0u) : (c->sel + 1u < CTL_N ? c->sel + 1u : CTL_N - 1u)));       /* clamps instead of wrapping */
#else
        c->sel = (uint8_t)hl_wrap(c->sel, CTL_N, (edge & 1u) != 0);
#endif
        ctl_fix(c); return 0;
    }
    return (edge & ((1u << 2) | (1u << 5))) != 0;
}
#endif
