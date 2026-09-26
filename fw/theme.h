/* Theme roles (docs/THEME_SPEC.md, step 0a of docs/ROADMAP.md item 0).
 *
 * Every named UI colour is read through th_role[] instead of being a literal, so a theme can replace the whole set.
 * In step 0a the table holds today's colours and nothing writes to it: the screen is pixel-identical to before.
 *
 * Roles 0..11 are the spec's twelve, in the spec's order (append-only). Four of them are still driven by their existing
 * runtime variables in 0a and their table slots are unused until 0b: bg/top (ui_grad_top_c, derived from the accent),
 * accent (ui_accent, the user's pick), on-accent (chosen by luma where needed) and accent-2 (no user yet).
 * Roles 12.. are named colours the code already used that the twelve do not cover; the survey (B-310) found them, and
 * whether they stay as roles, merge into the twelve or become derived is an owner decision recorded in docs/ROADMAP.md. */
#ifndef TAU_THEME_H
#define TAU_THEME_H

enum {
    TR_BG_TOP = 0, TR_BG_BOTTOM, TR_SURFACE, TR_SURFACE_TRACK, TR_TEXT_PRIMARY, TR_TEXT_SECONDARY,
    TR_ACCENT, TR_ON_ACCENT, TR_ACCENT2, TR_OK, TR_WARN, TR_DANGER,
    /* extension roles (append-only) */
    TR_BASE,         /* UI_BG: near-black navy under panels and overlays */
    TR_CHROME,       /* overlay header and action bar */
    TR_PILL,         /* now-playing preset pill */
    TR_ERROR,        /* UI_RED: warnings and failure text (differs from TR_DANGER, the ladder's top) */
    TR_FAINT,        /* filename line; the spec would derive it, kept as a colour so 0a changes nothing */
    TR_SPLASH_BG, TR_SPLASH_BAR,
    TR_FS_RED,       /* fullscreen progress colour while stopped or paused */
    TR_FS_TRACK,     /* fullscreen unplayed part of the line */
    TR_COUNT
};

/* Built-in default theme = the colours the firmware always had. Named separately so tools/ui_snapshot_renderer.py reads them. */
#define TH_DEF_BG_BOTTOM     0x0000u
#define TH_DEF_SURFACE       0x2945u
#define TH_DEF_SURFACE_TRACK 0x18E3u
#define TH_DEF_TEXT_PRIMARY  0xFFFFu
#define TH_DEF_TEXT_SECONDARY 0x94B2u
#define TH_DEF_OK            0x0600u
#define TH_DEF_WARN          0xFE60u
#define TH_DEF_DANGER        0xF9C0u
#define TH_DEF_BASE          0x0862u
#define TH_DEF_CHROME        0x10E5u
#define TH_DEF_PILL          0x0320u
#define TH_DEF_ERROR         0xF800u
#define TH_DEF_FAINT         0x6B4Du
#define TH_DEF_SPLASH_BG     0x0841u
#define TH_DEF_SPLASH_BAR    0x27ECu
#define TH_DEF_FS_RED        0xF249u
#define TH_DEF_FS_TRACK      0x1905u

static uint16_t th_role[TR_COUNT] = {
    [TR_BG_BOTTOM] = TH_DEF_BG_BOTTOM, [TR_SURFACE] = TH_DEF_SURFACE, [TR_SURFACE_TRACK] = TH_DEF_SURFACE_TRACK,
    [TR_TEXT_PRIMARY] = TH_DEF_TEXT_PRIMARY, [TR_TEXT_SECONDARY] = TH_DEF_TEXT_SECONDARY,
    [TR_OK] = TH_DEF_OK, [TR_WARN] = TH_DEF_WARN, [TR_DANGER] = TH_DEF_DANGER,
    [TR_BASE] = TH_DEF_BASE, [TR_CHROME] = TH_DEF_CHROME, [TR_PILL] = TH_DEF_PILL, [TR_ERROR] = TH_DEF_ERROR,
    [TR_FAINT] = TH_DEF_FAINT, [TR_SPLASH_BG] = TH_DEF_SPLASH_BG, [TR_SPLASH_BAR] = TH_DEF_SPLASH_BAR,
    [TR_FS_RED] = TH_DEF_FS_RED, [TR_FS_TRACK] = TH_DEF_FS_TRACK,
};

#define UI_PANEL   th_role[TR_SURFACE]
#define UI_TRACK   th_role[TR_SURFACE_TRACK]
#define UI_WHITE   th_role[TR_TEXT_PRIMARY]
#define UI_DIM     th_role[TR_TEXT_SECONDARY]
#define UI_BG      th_role[TR_BASE]
#define UI_PILL_BG th_role[TR_PILL]
#define UI_RED     th_role[TR_ERROR]
#define UI_FAINT   th_role[TR_FAINT]
#define LED_LO     th_role[TR_OK]
#define LED_MIDC   th_role[TR_WARN]
#define LED_HI     th_role[TR_DANGER]
#define OV_CHROME_BG   th_role[TR_CHROME]
#define TAU_SPLASH_BG  th_role[TR_SPLASH_BG]
#define TAU_SPLASH_BAR_C th_role[TR_SPLASH_BAR]
#define FS_RED     th_role[TR_FS_RED]
#define FS_TRACK   th_role[TR_FS_TRACK]

#endif
