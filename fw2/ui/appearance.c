#include "appearance.h"
#include "theme.h"
#include "meter.h"
#include "../cores/settings_core.h"
#include "../gfx/font.h"
#include "../generated/meter_registry.h"
/* the persisted preset field of each meter in table order: a meter without a field would lose its preset at every restart (the sixth meter did) */
_Static_assert(METER_SLOTS <= SF_METERS_MAX, "every meter needs a persisted preset position and preset id field (cores/settings_core.h SF_MPRE*, SF_MPID*: seven)");
void appearance_get(uint32_t *sv) { sv[SF_COLOUR] = th_pal; sv[SF_THEME] = th_theme; sv[SF_POLARITY] = th_pol; sv[SF_METER] = meters_cur(); for (uint32_t m = 0; m < METER_SLOTS; m++) { sv[SF_MPRE0 + m] = meters_preset_pos_saved(m); sv[SF_MPID0 + m] = meters_preset_id_saved(m); } meters_tune_words(&sv[SF_TUNE0]); sv[SF_TUNE_NEW] = meters_tune_newer(); sv[SF_FONT] = font_cur; }
void appearance_set(const uint32_t *sv)
{
    theme_set(sv[SF_THEME], sv[SF_POLARITY], sv[SF_COLOUR]);
    for (uint32_t m = 0; m < METER_SLOTS; m++) meters_preset_restore(m, sv[SF_MPID0 + m], sv[SF_MPRE0 + m]);          /* a preset file by its id when one was chosen, else the position in the pack's own list */
    font_select(sv[SF_FONT]);                                          /* the saved font (an unknown index is font 0); a failure leaves the font in the memory as it is */
    meters_tune_apply(&sv[SF_TUNE0], sv[SF_TUNE_NEW]);                                 /* after the presets: a tuning belongs to the preset it started from */
    if (sv[SF_METER] != meters_cur()) meters_select(sv[SF_METER]);
}
