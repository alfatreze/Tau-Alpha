#include "appearance.h"
#include "theme.h"
#include "meter.h"
#include "../cores/settings_core.h"
void appearance_get(uint32_t *sv) { sv[SF_COLOUR] = th_pal; sv[SF_THEME] = th_theme; sv[SF_POLARITY] = th_pol; sv[SF_METER] = meters_cur(); }
void appearance_set(const uint32_t *sv)
{
    theme_set(sv[SF_THEME], sv[SF_POLARITY], sv[SF_COLOUR]);
    if (sv[SF_METER] != meters_cur()) meters_select(sv[SF_METER]);
}
