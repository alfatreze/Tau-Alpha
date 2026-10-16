#include "appearance.h"
#include "theme.h"
#include "meter.h"
#include "../cores/settings_core.h"
void appearance_get(uint32_t *sv) { sv[SF_COLOUR] = th_pal; sv[SF_THEME] = th_theme; sv[SF_POLARITY] = th_pol; sv[SF_METER] = meters_cur(); for (uint32_t m = 0; m < 5u; m++) sv[SF_MPRE0 + m] = meters_preset_of(m); meters_tune_words(&sv[SF_TUNE0]); }
void appearance_set(const uint32_t *sv)
{
    theme_set(sv[SF_THEME], sv[SF_POLARITY], sv[SF_COLOUR]);
    for (uint32_t m = 0; m < 5u; m++) if (sv[SF_MPRE0 + m] != meters_preset_of(m)) meters_preset_select(m, sv[SF_MPRE0 + m]);
    meters_tune_apply(&sv[SF_TUNE0]);                                 /* after the presets: a tuning belongs to the preset it started from */
    if (sv[SF_METER] != meters_cur()) meters_select(sv[SF_METER]);
}
