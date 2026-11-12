#include "appearance.h"
#include "theme.h"
#include "meter.h"
#include "../cores/settings_core.h"
#include "../gfx/font.h"
/* Persistence layout 2 (docs/features/PERSISTENCE_LAYOUT.md): the selected meter by its id hash (31 bits), its preset (position and preset file id), and the two tunings. Declared here, not in meter.h (the WebAssembly twins of the meters hash it). */
extern uint32_t meters_saved_hash(void);                       /* fw2/ui/meters.c: the hash a card remembers: the meter on screen, or the one that could not be honoured at boot (kept until the user picks another) */
extern uint32_t meters_hash_is_shown(void);                    /* 1 when the saved hash is the meter on screen (0 while an unhonoured selection is kept)  */
extern int meters_select_hash(uint32_t h31);                   /* the boot restore: 1 = that meter runs */
void appearance_get(uint32_t *sv)
{
    sv[SF_COLOUR] = th_pal; sv[SF_THEME] = th_theme; sv[SF_POLARITY] = th_pol; sv[SF_FONT] = font_cur;
    const uint32_t h = meters_saved_hash(), shown = meters_cur();
    sv[SF_METER_HASH] = h;
    sv[SF_METER_PRE_POS] = h && meters_hash_is_shown() ? meters_preset_pos_saved(shown) : 0u;
    sv[SF_METER_PRE_ID] = h && meters_hash_is_shown() ? meters_preset_id_saved(shown) : 0u;
    meters_tune_words(&sv[SF_TUNE0]); sv[SF_TUNE_NEW] = meters_tune_newer();
}
void appearance_set(const uint32_t *sv)
{
    theme_set(sv[SF_THEME], sv[SF_POLARITY], sv[SF_COLOUR]);
    font_select(sv[SF_FONT]);                                          /* the saved font (an unknown index is font 0); a failure leaves the font in the memory as it is */
    if (meters_select_hash(sv[SF_METER_HASH])) meters_preset_restore(meters_cur(), sv[SF_METER_PRE_ID], sv[SF_METER_PRE_POS]);      /* the meter, then its preset (a file by its id when one was chosen, else the position in the pack's own list); a meter that is gone leaves Winamp Bars on its defaults */
    meters_tune_apply(&sv[SF_TUNE0], sv[SF_TUNE_NEW]);                                 /* last: a tuning belongs to the preset it started from; the meter on screen takes its tuning now, the other slot's meter when it is loaded */
}
