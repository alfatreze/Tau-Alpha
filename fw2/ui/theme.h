/* Player 2 themes (docs/THEME_SPEC.md): every drawn colour that a theme may change is read through th_role[]. Roles 0..11 are the spec's twelve in the spec's order, 12.. the extension roles of the previous
 * player; the numbering is append-only and is the numbering of THEM files in tau-assets.bin. A theme is a name and two role sets (dark, light); the mode picks the set; the accent is the user's pick from the 19
 * Analogue Pocket colours (scaled down on a light theme). Built-in themes are generated from themes/*.json (tools/gen_themes.py); up to four more come from tau-assets.bin (THEM). */
#ifndef P2_THEME_H
#define P2_THEME_H
#include <stdint.h>
enum {
    TR_BG_TOP = 0, TR_BG_BOTTOM, TR_SURFACE, TR_SURFACE_TRACK, TR_TEXT_PRIMARY, TR_TEXT_SECONDARY,
    TR_ACCENT, TR_ON_ACCENT, TR_ACCENT2, TR_OK, TR_WARN, TR_DANGER,
    TR_BASE, TR_CHROME, TR_PILL, TR_ERROR, TR_FAINT, TR_SPLASH_BG, TR_SPLASH_BAR, TR_FS_RED, TR_FS_TRACK,
    TR_COUNT
};
extern uint16_t th_role[TR_COUNT];
#define UI_BG_SCREEN th_role[TR_BG_BOTTOM]
#define UI_ACCENT  th_role[TR_ACCENT]
#define UI_ON_ACCENT th_role[TR_ON_ACCENT]
#define UI_PANEL   th_role[TR_SURFACE]
#define UI_TRACK   th_role[TR_SURFACE_TRACK]
#define UI_WHITE   th_role[TR_TEXT_PRIMARY]
#define UI_DIM     th_role[TR_TEXT_SECONDARY]
#define UI_BG      th_role[TR_BASE]
#define UI_FAINT   th_role[TR_FAINT]
#define UI_RED     th_role[TR_ERROR]
#define UI_FS_RED  th_role[TR_FS_RED]
#define UI_FS_TRACK th_role[TR_FS_TRACK]
#define TH_FILE_MAX 4u
extern uint8_t th_theme, th_pol, th_pal;        /* the selected theme, mode (0 dark, 1 light) and accent colour index */
extern uint8_t th_file_n, th_file_err;          /* themes read from tau-assets.bin, and why not (0 none, else an AS_E_* code) */
extern uint8_t th_changed;                      /* set whenever the colours changed: the main loop repaints the current view once */
extern const char *const th_palette_name[19];
void        theme_init(void);                   /* the built-in defaults, text mode probe */
uint32_t    theme_count(void);                  /* built-in + file themes */
const char *theme_name(uint32_t i);
void        theme_set(uint32_t theme, uint32_t pol, uint32_t pal);   /* validates, applies, sets th_changed */
void        theme_pick_step(int dir);           /* L1/R1: the next / previous accent colour */
/* the THEM section loader (theme_file.c): fills the file themes; the built-in theme 0 supplies the roles a file does not carry */
void        theme_file_load(const uint8_t *base, uint32_t total);
uint16_t    theme_file_role(uint32_t file_idx, uint32_t pol, uint32_t role);
uint32_t    theme_file_luma(uint32_t file_idx, uint32_t pol);
const char *theme_file_name(uint32_t file_idx);
uint16_t    theme_builtin_role(uint32_t pol, uint32_t role);          /* built-in theme 0 */
#endif
