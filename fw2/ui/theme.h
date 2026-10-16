/* Player 2 colour roles (docs/THEME_SPEC.md): every drawn colour that a theme may change is read through th_role[] instead of being a literal. Roles 0..11 are the spec's twelve in the spec's order, 12.. the
 * extension roles of the previous player; the numbering is append-only and is the numbering of THEM files in tau-assets.bin. The defaults are the colours the firmware always had; the theme loader (next) replaces
 * the whole set, and the accent follows the user's pick. */
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
void theme_init(void);                         /* the built-in defaults */
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
#endif
