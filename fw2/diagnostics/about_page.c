#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "about_page.h"
#include "../gfx/gfx.h"
#include "../gfx/font.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../transport/mp3play.h"
#include "../transport/player_state.h"
#include "../stream/library.h"
#include "../aout/aout.h"
#include "../aout/halcyon.h"
#include "../cores/about_core.h"
#include "../cores/helios_core.h"
#include "../cores/monoclock_core.h"
#include "../ui/theme.h"
#include "../ui/meter.h"
#include "../ui/nowplaying.h"
#include "../settings/settings.h"

#define BG   UI_BG_SCREEN
#define DIM  UI_DIM
#define FG   UI_WHITE
#define ACC  UI_ACCENT
#define ROW_H 19u
#define TOP_Y 30u
#define VIS 16u

void meters_pack_info(uint32_t *running, uint32_t *first_err, uint32_t *file_err, uint32_t *seen);   /* fw2/ui/meters.c (not in meter.h: the wasm meter preview hashes that header) */
static uint32_t top;
static mc_t clk;                                             /* seconds since boot from the wrapping cycle counter (cores/monoclock_core.h): the page keeps its own clock, it does not depend on the counters */
static const char *const font_names[4] = { "TAMZEN", "TAMZEN BOLD", "GROTESK SEMIBOLD", "GROTESK BOLD" };      /* the Appearance order (cores/menupages_core.h mg_ft_opt, fw2/generated/font_data.h) */

static void gather(about_in_t *in)
{
    uint32_t i = 0;
    for (; i < 43u && p2_ver_field[7u + i]; i++) in->version[i] = p2_ver_field[7u + i];
    in->version[i] = 0;
    in->core_version = caps.core_version;
    in->lib_state = lib_state; in->lib_err = lib_err; in->tracks = lib_n_tracks; in->albums = library_albums(); in->artists = library_artists();
    const np_info_t *p = mp3play_np();
    in->np_valid = (uint8_t)p->valid;
    for (i = 0; i < 31u && p->quality[i]; i++) in->quality[i] = p->quality[i];
    in->quality[i] = 0;
    in->rg_mode = (uint8_t)p->rg_mode; in->eq = halcyon_name(); in->tempo_q8 = p->tempo_q8; in->varispeed = (uint8_t)p->varispeed; in->volume = p->volume;
    in->theme = theme_name(th_theme); in->light = th_pol; in->colour = th_palette_name[th_pal < 19u ? th_pal : 0u];
    in->font = font_names[font_cur < 4u ? font_cur : 0u]; in->meter = meters_def(meters_cur())->name; in->art_on = np_art_on;
    uint32_t run = 0, err = 0, file = 0, seen = 0;
#ifdef P2_PACKS
    meters_pack_info(&run, &err, &file, &seen);
#else
    (void)err; (void)file;                                              /* a build without loadable meters has none to report */
#endif
    in->themes = th_file_n; in->eq_user = halcyon_user_n; in->packs_seen = seen;
    uint32_t n = 0; for (uint32_t b = 0; b < 32u; b++) n += (run >> b) & 1u;
    in->packs_run = n > seen ? seen : n;
    in->migrated = (uint8_t)settings_stats.migrated;
    in->dropouts = aout_stats.dry; in->read_errors = lp_stream.fails_total + lp.open_err;
    in->uptime_s = mc_update(&clk, cycles(), CLK_HZ);
}

void about_page_init(void)
{
    top = 0;
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    gfx_text(10, 6, "ABOUT", ACC, BG);
    gfx_text(10, GFX_H - 20, "UP DOWN  B BACK  SEL+START REPORT", DIM, BG);
}
void about_page_key(uint32_t edge)
{
    about_in_t in; gather(&in);
    const uint32_t n = about_count(&in), maxtop = n > VIS ? n - VIS : 0u;
    if (edge & KEY_DOWN) top = hl_wrap(top, maxtop + 1u, 0);
    if (edge & KEY_UP)   top = hl_wrap(top, maxtop + 1u, 1);
}
void about_page_draw(void)
{
    about_in_t in; gather(&in);
    const uint32_t n = about_count(&in), maxtop = n > VIS ? n - VIS : 0u;
    if (top > maxtop) top = maxtop;                                      /* a section vanished (the track ended): stay inside the list */
    about_row_t r;
    for (uint32_t k = 0; k < VIS; k++) {
        const uint32_t y = TOP_Y + k * ROW_H;
        gfx_rect(0, y, GFX_W, ROW_H, BG);
        if (top + k >= n || !about_get(&in, top + k, &r)) continue;
        if (r.kind == AB_HEAD) gfx_text(10, y, r.label, ACC, BG);
        else if (r.kind == AB_TEXT) gfx_text(10, y, r.value, DIM, BG);
        else { gfx_text(10, y, r.label, DIM, BG); gfx_text(GFX_W - 10 - gfx_text_width(r.value), y, r.value, FG, BG); }
    }
}
