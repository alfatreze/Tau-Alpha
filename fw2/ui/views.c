#include "views.h"
#include "regions.h"
#include "helios.h"
#include "../cores/view_core.h"
#include "../platform/mmio.h"
#include "../diagnostics/info_counters.h"
#include "../diagnostics/info_page.h"
#include "../diagnostics/report_page.h"
#include "../transport/browser.h"
#include "nowplaying.h"
#include "browser_view.h"
#include "settings_view.h"

static vw_t vw;

/* Library */
static void lib_enter(void) { browser_enter(); browser_view_enter(); }
static int  lib_input(uint32_t edge) { return browser_key(edge) ? V_NOWPLAYING : VW_STAY; }
static void lib_mark(void) { helios_dirty(R_LIST); }
/* Info: the page redraws every quarter second */
static uint32_t info_last;
static void info_enter(void) { info_page_init(); info_last = 0; }
static int  info_input(uint32_t edge)
{
    if (edge & (KEY_B | KEY_LEFT)) return VW_BACK;
    info_page_key(edge & (KEY_UP | KEY_DOWN));
    helios_dirty(R_INFO);
    return VW_STAY;
}
static void info_mark(void) { uint32_t t = cycles(); if ((uint32_t)(t - info_last) >= CLK_HZ / 4u) { info_last = t; helios_dirty(R_INFO); } }
static void info_redraw(void) { counters_refresh(); info_page_draw(); }
/* Report: a snapshot of every counter as a pixel grid; any of these keys returns */
static void report_enter(void) { report_page_draw(); }
static int  report_input(uint32_t edge) { return (edge & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_UP | KEY_DOWN)) ? VW_BACK : VW_STAY; }

static const view_t tab[VIEW_COUNT] = {
    [V_NOWPLAYING] = { nowplaying_enter, 0, nowplaying_input, nowplaying_mark, NP_REGIONS },
    [V_LIBRARY]    = { lib_enter, 0, lib_input, lib_mark, RM(R_LIST) },
    [V_SETTINGS]   = { settings_view_enter, 0, settings_view_input, settings_view_mark, RM(R_MENU) },
    [V_INFO]       = { info_enter, 0, info_input, info_mark, RM(R_INFO) },
    [V_REPORT]     = { report_enter, 0, report_input, 0, 0 },
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
    vw_init(&vw, tab, VIEW_COUNT, V_NOWPLAYING);
    vw_start(&vw, &helios);
}
void views_switch(int to) { vw_switch(&vw, &helios, to); }
void views_input(uint32_t edge) { vw_input(&vw, &helios, edge); }
void views_mark(void) { vw_draw(&vw); }
void views_flush(void) { helios_flush(); }
uint32_t views_cur(void) { return vw.cur; }
void views_report_refresh(void) { report_page_draw(); }
