#include "appearance.h"
#include "theme.h"
#include "meter.h"
#include "../cores/settings_core.h"
#include "../gfx/font.h"
#include "../generated/meter_registry.h"
/* the persisted preset field of each meter in table order: a meter without a field would lose its preset at every restart (the sixth meter did) */
static const uint8_t pre_field[] = { SF_MPRE0, SF_MPRE1, SF_MPRE2, SF_MPRE3, SF_MPRE4, SF_MPRE5, SF_MPRE6 };
_Static_assert(METER_SLOTS <= sizeof pre_field, "every meter needs a persisted preset field (cores/settings_core.h SF_MPRE*)");
void appearance_get(uint32_t *sv) { sv[SF_COLOUR] = th_pal; sv[SF_THEME] = th_theme; sv[SF_POLARITY] = th_pol; sv[SF_METER] = meters_cur(); for (uint32_t m = 0; m < METER_SLOTS; m++) sv[pre_field[m]] = meters_preset_of(m); meters_tune_words(&sv[SF_TUNE0]); sv[SF_FONT] = font_cur; }
void appearance_set(const uint32_t *sv)
{
    theme_set(sv[SF_THEME], sv[SF_POLARITY], sv[SF_COLOUR]);
    for (uint32_t m = 0; m < METER_SLOTS; m++) if (sv[pre_field[m]] != meters_preset_of(m)) meters_preset_select(m, sv[pre_field[m]]);
    font_select(sv[SF_FONT]);                                          /* the saved font (an unknown index is font 0); a failure leaves the font in the memory as it is */
    meters_tune_apply(&sv[SF_TUNE0]);                                 /* after the presets: a tuning belongs to the preset it started from */
    if (sv[SF_METER] != meters_cur()) meters_select(sv[SF_METER]);
}
