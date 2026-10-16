/* fw2/cores/menupages_core.h -- Player 2 Settings: the previous player's menu structure (Home > Appearance / Audio / Playback / Diagnostics / Info), as data, with the navigation as pure logic.
 * Rows come in six kinds: GROUP (opens a page), CHOICE (A or Right opens a list of its options; A picks one and goes back), TOGGLE (A, Left or Right flips it), VOL (Left/Right changes by 3),
 * ACTION (A runs it) and SOON (a feature of the previous player not ported yet: shown dimmed so the structure is the one you know, does nothing). B or Left goes back a level; on the Home page it
 * closes Settings. Up/Down wrap (Helios's wrap rule). Values live with the caller; this module only says what changed.
 * Pure: no drawing, no registers. Host-tested (sim/test_p2_menupages.py). */
#ifndef MENUPAGES_CORE_H
#define MENUPAGES_CORE_H
#include <stdint.h>
#include "helios_core.h"
#ifndef BUG
#define BUG 0
#endif
/* Value ids: 0..6 mirror PREF_* (cores/settings_core.h, asserted in ui/settings_view.c); 7 and up are view-level flags that are not persisted. */
enum { V_VOLUME = 0, V_RG, V_TEMPO, V_REPEAT, V_SHUFFLE, V_SEEKSTEP, V_SKIPSIL, V_ART, V_ACCEPT_ALL, V_COLOUR, V_THEME, V_MODE, V_METER, V_BLANK, V_COUNT };
enum { RK_GROUP = 0, RK_CHOICE, RK_TOGGLE, RK_VOL, RK_ACTION, RK_SOON };
enum { PG_HOME = 0, PG_APPEAR, PG_AUDIO, PG_PLAY, PG_DIAG, PG_CHOICE, PG_COUNT };   /* PG_CHOICE is the option list of the choice row that opened it */
enum { ACT_INFO = 1, ACT_REPORT = 2, ACT_HALCYON = 3, ACT_CONFIGURE = 4 };
typedef struct { const char *label; uint8_t kind, arg; } mg_row_t;      /* arg: the page (GROUP), the value id (CHOICE/TOGGLE/VOL), the action (ACTION) */
typedef struct { const char *title; const mg_row_t *rows; uint8_t n; } mg_page_t;
typedef struct { const char *const *opt; uint8_t n; } mg_choice_t;       /* the options of a choice value, indexed by value */

static const mg_row_t mg_home_rows[] = { { "APPEARANCE", RK_GROUP, PG_APPEAR }, { "AUDIO", RK_GROUP, PG_AUDIO }, { "PLAYBACK", RK_GROUP, PG_PLAY }, { "DIAGNOSTICS", RK_GROUP, PG_DIAG },
                                         { "INFO", RK_ACTION, ACT_INFO } };      /* always the last row, as before */
static const mg_row_t mg_appear_rows[] = { { "COLOUR", RK_CHOICE, V_COLOUR }, { "THEME", RK_CHOICE, V_THEME }, { "MODE", RK_CHOICE, V_MODE }, { "METER", RK_CHOICE, V_METER }, { "METER: CONFIGURE", RK_ACTION, ACT_CONFIGURE },
                                           { "ALBUM ART", RK_TOGGLE, V_ART }, { "SCREEN BLANK", RK_CHOICE, V_BLANK } };
static const mg_row_t mg_audio_rows[] = { { "VOLUME", RK_VOL, V_VOLUME }, { "REPLAYGAIN", RK_CHOICE, V_RG }, { "HALCYON EQ", RK_ACTION, ACT_HALCYON } };
static const mg_row_t mg_play_rows[] = { { "REPEAT", RK_CHOICE, V_REPEAT }, { "SHUFFLE", RK_TOGGLE, V_SHUFFLE }, { "TEMPO", RK_CHOICE, V_TEMPO }, { "SEEK STEP", RK_CHOICE, V_SEEKSTEP },
                                         { "SKIP SILENCE", RK_TOGGLE, V_SKIPSIL } };
static const mg_row_t mg_diag_rows[] = { { "ACCEPT ALL RATES", RK_TOGGLE, V_ACCEPT_ALL }, { "REPORT", RK_ACTION, ACT_REPORT } };
#define MG_N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
static const mg_page_t mg_pages[PG_COUNT] = {
    { "MENU", mg_home_rows, MG_N(mg_home_rows) }, { "APPEARANCE", mg_appear_rows, MG_N(mg_appear_rows) }, { "AUDIO", mg_audio_rows, MG_N(mg_audio_rows) },
    { "PLAYBACK", mg_play_rows, MG_N(mg_play_rows) }, { "DIAGNOSTICS", mg_diag_rows, MG_N(mg_diag_rows) }, { "", 0, 0 } };
static const char *const mg_rg_opt[] = { "OFF", "TRACK", "ALBUM" }, *const mg_rp_opt[] = { "OFF", "ALL", "ONE" },
                         *const mg_tp_opt[] = { "0.75X", "0.85X", "1.00X", "1.10X", "1.25X", "1.50X", "1.75X", "2.00X" }, *const mg_sk_opt[] = { "10 S", "30 S", "60 S", "120 S" };
static const char *const mg_mode_opt[] = { "DARK", "LIGHT" }, *const mg_bl_opt[] = { "OFF", "1 MIN", "5 MIN", "10 MIN", "30 MIN" };
/* The option lists of THEME, COLOUR and METER depend on the build and the files on the card: the firmware sets them (mg_set_choice) before the first draw. */
static mg_choice_t mg_choices[V_COUNT] = { [V_RG] = { mg_rg_opt, 3 }, [V_REPEAT] = { mg_rp_opt, 3 }, [V_TEMPO] = { mg_tp_opt, 8 }, [V_SEEKSTEP] = { mg_sk_opt, 4 }, [V_MODE] = { mg_mode_opt, 2 }, [V_BLANK] = { mg_bl_opt, 5 } };
static inline void mg_set_choice(uint32_t id, const char *const *opt, uint32_t n) { mg_choices[id].opt = opt; mg_choices[id].n = (uint8_t)n; }

#define MG_DEPTH 4u
enum { EF_NONE = 0, EF_SET, EF_ACTION, EF_CLOSE };            /* what the caller must do after an input */
typedef struct { uint8_t kind, id; uint32_t value; } mg_effect_t;
typedef struct { uint8_t page[MG_DEPTH], sel[MG_DEPTH], top[MG_DEPTH], choice_val[MG_DEPTH]; uint8_t depth; } mg_t;       /* depth 0 = Home; entry i is the page shown at level i */
#define MG_VIS 8u                                              /* rows visible at once */

static inline void mg_open(mg_t *m) { m->depth = 0; m->page[0] = PG_HOME; m->sel[0] = 0; m->top[0] = 0; }
static inline uint32_t mg_cur(const mg_t *m) { return m->depth; }
static inline uint32_t mg_rows(const mg_t *m)
{
    uint8_t p = m->page[m->depth];
    return p == PG_CHOICE ? mg_choices[m->choice_val[m->depth]].n : mg_pages[p].n;
}
static inline void mg_fix(mg_t *m)
{
    uint32_t n = mg_rows(m), d = m->depth, sel = m->sel[d], top = m->top[d];
    if (sel < top) top = sel;
    if (sel >= top + MG_VIS) top = sel + 1u - MG_VIS;
    uint32_t maxtop = n > MG_VIS ? n - MG_VIS : 0u;
    if (top > maxtop) top = maxtop;
    m->top[d] = (uint8_t)top;
}
static inline void mg_push(mg_t *m, uint32_t page, uint32_t choice_val, uint32_t sel)
{
    if (m->depth + 1u >= MG_DEPTH) return;
    m->depth++; m->page[m->depth] = (uint8_t)page; m->choice_val[m->depth] = (uint8_t)choice_val; m->sel[m->depth] = (uint8_t)sel; m->top[m->depth] = 0; mg_fix(m);
}
/* One key edge. vals[] holds the current value of every value id. A back at the Home page asks the caller to close Settings. */
static inline mg_effect_t mg_input(mg_t *m, uint32_t edge, const uint32_t *vals)
{
    mg_effect_t e = { EF_NONE, 0, 0 };
    enum { UP = 1u << 0, DOWN = 1u << 1, LEFT = 1u << 2, RIGHT = 1u << 3, A = 1u << 4, B = 1u << 5 };
    uint32_t d = m->depth, n = mg_rows(m);
    if (edge & (UP | DOWN)) { m->sel[d] = (uint8_t)hl_wrap(m->sel[d], n, (edge & UP) != 0); mg_fix(m); return e; }
    int back = (edge & (B | LEFT)) != 0, fwd = (edge & (A | RIGHT)) != 0;
    if (m->page[d] == PG_CHOICE) {                                       /* an option list: A picks, B or Left backs out without a change */
        if (edge & A) {
            e.kind = EF_SET; e.value = m->sel[d];
#if BUG == 2
            e.id = 0;                                                    /* the wrong value id */
#else
            e.id = m->choice_val[d];
#endif
            m->depth--; mg_fix(m); return e;
        }
        if (back) {
#if BUG == 3
            e.kind = EF_SET; e.id = m->choice_val[d]; e.value = m->sel[d];      /* backing out applies the highlighted option */
#endif
            m->depth--; mg_fix(m);
        }
        return e;
    }
    const mg_row_t *r = &mg_pages[m->page[d]].rows[m->sel[d]];
#if BUG == 1
    if (back) {                                                          /* Left always goes back, even on the volume */
#else
    if (back && !(r->kind == RK_VOL && (edge & LEFT)) && !(r->kind == RK_TOGGLE && (edge & LEFT))) {      /* Left changes a value on a toggle or the volume; elsewhere it is back */
#endif      /* Left changes a value on a toggle or the volume; elsewhere it is back */
        if (d == 0) e.kind = EF_CLOSE; else { m->depth--; mg_fix(m); }
        return e;
    }
    switch (r->kind) {
    case RK_GROUP: if (fwd) mg_push(m, r->arg, 0, 0); break;
    case RK_CHOICE: if (fwd) mg_push(m, PG_CHOICE, r->arg, vals[r->arg] < mg_choices[r->arg].n ? vals[r->arg] : 0u); break;
    case RK_TOGGLE:
        if (fwd || (edge & LEFT)) { e.kind = EF_SET; e.id = r->arg; e.value = vals[r->arg] ? 0u : 1u; }
        break;
    case RK_VOL:
        if (edge & (LEFT | RIGHT)) {
            uint32_t v = vals[r->arg], nv = (edge & RIGHT) ? (v + 3u > 100u ? 100u : v + 3u) : (v < 3u ? 0u : v - 3u);
            if (nv != v) { e.kind = EF_SET; e.id = r->arg; e.value = nv; }
        }
        break;
    case RK_ACTION: if (edge & A) { e.kind = EF_ACTION; e.id = r->arg; } break;
    default: break;                                                      /* SOON: nothing */
    }
    return e;
}
#endif
