#include "appearance.h"
#include "theme.h"
#include "meter.h"
#include "../cores/settings_core.h"
#include "../gfx/font.h"
#include "meters_toc.h"
/* Persistence layout 2 (docs/features/PERSISTENCE_LAYOUT.md): the selected meter by its id hash (31 bits), its preset (position and preset file id), and the two tunings. What is written is the COMMITTED meter (the commit rule in fw2/ui/meters.c: a newly chosen meter reaches
 * the saved word only after it has run cleanly for a few seconds). The declarations are in meters_toc.h, not meter.h (the WebAssembly twins of the meters hash that header). */
void appearance_get(uint32_t *sv)
{
    sv[SF_COLOUR] = th_pal; sv[SF_THEME] = th_theme; sv[SF_POLARITY] = th_pol; sv[SF_FONT] = font_cur;
    sv[SF_METER_HASH] = meters_saved_hash();                          /* the committed meter, or the saved one that could not be honoured at boot (kept until the user picks another) */
    sv[SF_METER_PRE_POS] = meters_saved_pos();                        /* its preset: live while it is the one on screen, else the snapshot taken when it left */
    sv[SF_METER_PRE_ID] = meters_saved_id();
    meters_tune_words(&sv[SF_TUNE0]); sv[SF_TUNE_NEW] = meters_tune_newer();
}
void appearance_set(const uint32_t *sv)
{
    theme_set(sv[SF_THEME], sv[SF_POLARITY], sv[SF_COLOUR]);
    font_select(sv[SF_FONT]);                                          /* the saved font (an unknown index is font 0); a failure leaves the font in the memory as it is */
    const int ok = meters_select_hash(sv[SF_METER_HASH]);
    meters_keep_preset(sv[SF_METER_PRE_POS], sv[SF_METER_PRE_ID]);    /* a saved meter that is gone or refused: its preset is written back with it */
    if (ok) meters_preset_restore(meters_cur(), sv[SF_METER_PRE_ID], sv[SF_METER_PRE_POS]);      /* the meter, then its preset (a file by its id when one was chosen, else the position in the pack's own list); a meter that is gone leaves Winamp Bars on its defaults */
    meters_tune_apply(&sv[SF_TUNE0], sv[SF_TUNE_NEW]);                                 /* last: a tuning belongs to the preset it started from; the meter on screen takes its tuning now, the other slot's meter when it is selected */
}
