#include "views.h"
#include "regions.h"
#include "helios.h"
#include "../cores/view_core.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../transport/browser.h"
#include "nowplaying.h"
#include "browser_view.h"
#include "settings_view.h"
#include "halcyon_view.h"
#include "fullscreen_view.h"
#include "configure_view.h"
#include "controls_view.h"
#include "help_view.h"
#include "meter.h"

static vw_t vw;
/* A view change draws inside one frame: begin (the idle buffer, a copy of what is shown), the new view's enter(), its regions marked, all drawn at once, then the flip. The nesting count keeps a switch asked from inside a
 * switch (applied right after, never nested) in the same frame discipline. */
static int frame_active;
static void view_frame_begin(void) { frame_active = gfx_frame_begin(); }
static void view_frame_end(void) { if (frame_active) { helios_flush_now(); gfx_frame_end(frame_active); frame_active = 0; } }

/* Library */
static void lib_enter(void) { browser_enter(); browser_view_enter(); }
static int  lib_input(uint32_t edge) { return browser_key(edge) ? V_NOWPLAYING : VW_STAY; }
static void lib_mark(void) { helios_dirty(R_LIST); }
/* Info: the page redraws every quarter second */
static const views_diag_t *dg;                               /* the diagnostics pages, registered by the app (ui does not include diagnostics) */
static uint32_t info_last;
static void info_enter(void) { if (dg) dg->info_init(); info_last = 0; }
static int  info_input(uint32_t edge)
{
    if (edge & (KEY_B | KEY_LEFT)) return VW_BACK;
    if (dg) dg->info_key(edge & (KEY_UP | KEY_DOWN));
    helios_dirty(R_INFO);
    return VW_STAY;
}
static void info_mark(void) { uint32_t t = cycles(); if ((uint32_t)(t - info_last) >= CLK_HZ / 4u) { info_last = t; helios_dirty(R_INFO); } }
static void info_redraw(void) { if (dg) dg->info_draw(); }
/* About: the player in words (docs/features/ABOUT_PAGE_SPEC.md): scrolls like Info, redraws every quarter second (the running time) */
static void about_enter(void) { if (dg) dg->about_init(); info_last = 0; }
static int  about_input(uint32_t edge)
{
    if (edge & (KEY_B | KEY_LEFT)) return VW_BACK;
    if (dg) dg->about_key(edge & (KEY_UP | KEY_DOWN));
    helios_dirty(R_ABOUT);
    return VW_STAY;
}
static void about_mark(void) { uint32_t t = cycles(); if ((uint32_t)(t - info_last) >= CLK_HZ / 4u) { info_last = t; helios_dirty(R_ABOUT); } }
static void about_redraw(void) { if (dg) dg->about_draw(); }
/* Report: a snapshot of every counter as a pixel grid; any of these keys returns */
static void report_enter(void) { if (dg) dg->report_draw(); }
static int  report_input(uint32_t edge) { return (edge & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_UP | KEY_DOWN)) ? VW_BACK : VW_STAY; }

/* Check: the audio tests of Settings > Diagnostics > CHECK */
static void check_enter(void) { if (dg) dg->check_enter(); }
static void check_repaint(void) { if (dg) dg->check_repaint(); }
static void check_exit(void) { if (dg) dg->check_exit(); }
static int  check_input(uint32_t edge) { return (dg && dg->check_key(edge)) ? VW_BACK : VW_STAY; }
static void check_mark(void) { if (!dg) return; dg->check_tick(); if (dg->check_dirty()) helios_dirty(R_CHECK); }
static void check_redraw(void) { if (dg) dg->check_draw(); }

static const view_t tab[VIEW_COUNT] = {
    [V_NOWPLAYING] = { nowplaying_enter, 0, nowplaying_input, nowplaying_mark, NP_REGIONS, nowplaying_enter },
    [V_LIBRARY]    = { lib_enter, 0, lib_input, lib_mark, RM(R_LIST), browser_view_enter },
    [V_SETTINGS]   = { settings_view_enter, settings_view_exit, settings_view_input, settings_view_mark, RM(R_MENU), settings_view_redraw },
    [V_INFO]       = { info_enter, 0, info_input, info_mark, RM(R_INFO), info_enter },
    [V_REPORT]     = { report_enter, 0, report_input, 0, 0, 0 },
    [V_HALCYON]    = { halcyon_view_enter, 0, halcyon_view_input, halcyon_view_mark, RM(R_HAL), halcyon_view_redraw },
    [V_CONFIGURE]  = { configure_view_enter, 0, configure_view_input, configure_view_mark, RM(R_CONF_PREV) | RM(R_CONF_LIST), configure_view_redraw },
    [V_CONTROLS]   = { controls_view_enter, 0, controls_view_input, controls_view_mark, RM(R_CONTROLS), controls_view_redraw },
    [V_HELP]       = { help_view_enter, 0, help_view_input, help_view_mark, RM(R_HELP), help_view_redraw },
    [V_ABOUT]      = { about_enter, 0, about_input, about_mark, RM(R_ABOUT), about_enter },
    [V_CHECK]      = { check_enter, check_exit, check_input, check_mark, RM(R_CHECK), check_repaint },
    [V_FULLSCREEN] = { fullscreen_view_enter, 0, nowplaying_input, fullscreen_view_mark, RM(R_FS_FIG) | RM(R_FS_FOOT), fullscreen_view_enter },
};

void views_init(void)
{
    helios_init();
    helios_region(nowplaying_draw_head, NP_HEAD_Y0, NP_HEAD_Y1);        /* R_NP_HEAD */
    helios_region(nowplaying_draw_meter, NP_METER_Y0, NP_METER_Y1);     /* R_NP_METER */
    helios_region(nowplaying_draw_foot, NP_FOOT_Y0, NP_FOOT_Y1);        /* R_NP_FOOT */
    helios_region(browser_view_draw, 0, HL_WHOLE);                      /* R_LIST */
    helios_region(settings_view_draw, 0, HL_WHOLE);                     /* R_MENU */
    helios_region(info_redraw, 0, HL_WHOLE);                            /* R_INFO */
    helios_region(halcyon_view_draw, 0, HL_WHOLE);                      /* R_HAL */
    helios_region(fullscreen_draw_fig, 0, 323);                         /* R_FS_FIG */
    helios_region(fullscreen_draw_foot, 323, 360);                      /* R_FS_FOOT */
    helios_region(configure_view_draw_preview, 26, 126);                /* R_CONF_PREV */
    helios_region(configure_view_draw_list, 134, 360);                  /* R_CONF_LIST */
    helios_region(check_redraw, 0, HL_WHOLE);                           /* R_CHECK */
    helios_region(controls_view_draw, 0, HL_WHOLE);                     /* R_CONTROLS */
    helios_region(help_view_draw, 0, HL_WHOLE);                         /* R_HELP */
    helios_region(about_redraw, 0, HL_WHOLE);                           /* R_ABOUT */
    vw_init(&vw, tab, VIEW_COUNT, V_NOWPLAYING);
    vw.overlay = V_REPORT;                                              /* the Report is looked at from Info or Configure: Back from them must not lead into it again */
    vw.frame_begin = view_frame_begin; vw.frame_end = view_frame_end;
    vw_start(&vw, &helios);
}
void views_refresh(void) { vw_refresh(&vw, &helios); }
void views_switch(int to) { vw_switch(&vw, &helios, to); }
void views_input(uint32_t edge) { vw_input(&vw, &helios, edge); }
void views_mark(void) { vw_draw(&vw); }
void views_flush(void) { helios_flush(); }
uint32_t views_cur(void) { return vw.cur; }
void views_report_payload(const uint8_t *p, uint32_t n) { if (dg) dg->report_payload(p, n); }
void views_report_refresh(void) { if (dg) dg->report_draw(); }
void views_set_diag(const views_diag_t *d) { dg = d; }
