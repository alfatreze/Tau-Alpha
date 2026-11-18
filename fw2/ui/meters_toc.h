/* The rows of the meter registry as the UI sees them (external meters P2.4). Declared here, NOT in meter.h: the WebAssembly twins of the meters hash that header (D-M15), and none of this belongs to them. */
#ifndef P2_METERS_TOC_H
#define P2_METERS_TOC_H
#include <stdint.h>
#define MROW_VISIBLE      1u   /* the TOC row is flagged visible (the built-in fallback row always is) */
#define MROW_UNAVAILABLE  2u   /* this firmware cannot run it: another ABI major, an ABI minor above its own, or a newer TAU than this one (X and the list skip it) */
#define MROW_REFUSED      4u   /* its last load was refused or it was struck out: "crashed last time" (cleared by the next good selection) */
#define MROW_NO_TUNING    8u   /* another row shares the low 16 bits of its id_hash: it runs from its defaults and saves no tuning */
#define MROW_SELECTABLE  16u   /* X and the METER list reach it: visible and runnable */
#define MROW_BINARY_ONLY 32u   /* the package has no source (flag bit 2 of the TOC row) */
uint32_t    meters_row_flags(uint32_t row);                          /* the MROW_ bits of a TOC row (0 past the end) */
const char *meters_row_name(uint32_t row, char *buf, uint32_t n);    /* the name of a row, into buf (always terminated); returns buf */
int         meters_is_builtin(uint32_t row);                         /* 1 for the built-in Winamp Bars row (its preview and parameters are in the firmware) */
uint32_t    meters_ui_count(void);                                   /* the ONE list the Settings METER list shows and X walks: the visible rows in TOC order (a row this firmware cannot run is in it, greyed; a hidden row is not) */
uint32_t    meters_ui_row(uint32_t i);                               /* list position -> TOC row */
uint32_t    meters_ui_index(uint32_t row);                           /* TOC row -> list position */
uint32_t    meters_ui_step(uint32_t row, int dir);                   /* X: the next / previous row of the list that can be picked (wraps) */
uint32_t    meters_saved_hash(void);                                 /* the 31-bit id_hash the register file holds: the COMMITTED meter (see meters.c: the commit rule) */
uint32_t    meters_hash_is_shown(void);                              /* 1 when the saved meter is the one on screen (its preset is live), else the kept snapshot is saved */
uint32_t    meters_saved_pos(void);                                  /* the preset position / preset file id + 1 to save with it */
uint32_t    meters_saved_id(void);
void        meters_keep_preset(uint32_t pos, uint32_t id1);          /* the boot: the saved preset of a saved meter that is not on screen is written back unchanged */
int         meters_select_hash(uint32_t h31);                        /* the boot restore: 1 = that meter runs */
const uint16_t *meters_shipped_max_of(uint32_t table_index, uint32_t *np);   /* parameter maxima of a shipped meter by its layout-1 index (settings_init_t) */
/* Configure of ANY meter (0.7.5): the page works on the meter on screen, so another row is loaded for the page and the set meter comes back when it is left (never touching the saved selection, see meters.c). */
int         meters_cfg_begin(uint32_t row);                          /* 0 = the page's meter (cur) is `row`; else the refusal code (the set meter is back at once) */
void        meters_cfg_end(void);                                    /* the page was left: the set meter, its preset and its tuning come back (no effect when nothing is previewed) */
void        meters_cfg_remember(void);                               /* before the page is left for the Report: the row it showed ... */
uint32_t    meters_cfg_reopen(void);                                 /* ... returned once when the page comes back (0xFFFFFFFF: none) */
uint32_t    meters_cfg_active(void);                                 /* 1 while the page shows a meter that is not the set one */
uint32_t    meters_cfg_set(void);                                    /* the set meter's row (the one the commit rule and the saved word follow) */
uint32_t    meters_row_configurable(uint32_t row);                   /* selectable and the TOC row has parameters: the rows that get the settings icon */
#endif
