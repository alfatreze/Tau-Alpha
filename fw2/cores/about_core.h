/* fw2/cores/about_core.h -- the About page: what a person wants to know about the player, in words (docs/features/ABOUT_PAGE_SPEC.md, decisions D-U01/D-U02). Pure: the caller fills an `about_in_t` from the player's state;
 * `about_get` returns the i-th visible row (sections that do not apply, such as PLAYING NOW with no track, are skipped). Host-tested by sim/test_p2_about.py with a fixture for every state of every row. */
#ifndef ABOUT_CORE_H
#define ABOUT_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
enum { AB_HEAD = 0, AB_ROW, AB_TEXT };                         /* a section heading (label), a label and a value, a line of text (value) */
typedef struct { uint8_t kind; char label[24]; char value[44]; } about_row_t;
typedef struct {
    char     version[44];                                      /* the full version, "0.7.0" or "0.6.1-dev.191+f2c7350c" */
    uint32_t core_version;                                     /* the bitstream's CORE_VERSION */
    uint8_t  lib_state, lib_err;                               /* 0 not loaded, 1 loaded, 2 failed, and why */
    uint32_t tracks, albums, artists;
    uint8_t  np_valid; char quality[32]; uint8_t rg_mode;      /* a track is loaded; "MP3 128K 44.1K STEREO"; 0 off 1 track 2 album */
    const char *eq;                                            /* the Halcyon selection by name ("OFF", a preset...) */
    uint32_t tempo_q8; uint8_t varispeed; uint32_t volume;     /* 256 = 1.00x; varispeed: pitch follows the speed; volume 0..100 */
    const char *theme, *colour, *font, *meter; uint8_t light, art_on;
    uint32_t themes, eq_user, packs_seen, packs_run;
    uint8_t  migrated;                                         /* this boot carried the settings over from an older version */
    uint32_t dropouts, read_errors, uptime_s;
} about_in_t;

static inline uint32_t ab_str(char *d, uint32_t n, uint32_t cap, const char *s) { while (*s && n + 1u < cap) d[n++] = *s++; d[n] = 0; return n; }
static inline uint32_t ab_num(char *d, uint32_t n, uint32_t cap, uint32_t v)
{
    char t[11]; uint32_t k = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 10u);
    while (k && n + 1u < cap) d[n++] = t[--k];
    d[n] = 0; return n;
}
/* "1.25": v is hundredths */
static inline uint32_t ab_hund(char *d, uint32_t n, uint32_t cap, uint32_t v) { n = ab_num(d, n, cap, v / 100u); n = ab_str(d, n, cap, "."); n = ab_num(d, n, cap, (v / 10u) % 10u); return ab_num(d, n, cap, v % 10u); }
static inline uint32_t ab_has(const char *s, const char *needle) { for (; *s; s++) { const char *a = s, *b = needle; while (*a && *b && *a == *b) { a++; b++; } if (!*b) return 1u; } return 0u; }

enum { AR_TAU_H, AR_VERSION, AR_BUILD, AR_CHANNEL, AR_HW, AR_MADE, AR_WEB, AR_LICENCE,
       AR_NOW_H, AR_FORMAT, AR_GAIN, AR_EQ, AR_SPEED, AR_VOLUME,
       AR_LIB_H, AR_LIBRARY, AR_ALBUMS, AR_ARTISTS,
       AR_LOOK_H, AR_THEME, AR_MODE, AR_COLOUR, AR_FONT, AR_METER, AR_ART,
       AR_CARD_H, AR_THEMES, AR_EQPRE, AR_METERS, AR_SETTINGS,
       AR_HEALTH_H, AR_AUDIO, AR_READS, AR_UPTIME,
       AR_CRED_H, AR_C1, AR_C2, AR_C3, AR_C4,
       AR_HELP_H, AR_H1, AR_H2, AR_H3,
       AR_COUNT };
static inline int ab_visible(const about_in_t *in, uint32_t r)
{
    if (r >= AR_NOW_H && r <= AR_VOLUME) return in->np_valid;
    if (r == AR_ALBUMS || r == AR_ARTISTS) return in->lib_state == 1u;
    return 1;
}
static inline void ab_fill(const about_in_t *in, uint32_t r, about_row_t *o)
{
    uint32_t n = 0; char *v = o->value; const uint32_t C = sizeof o->value;
    o->kind = AB_ROW; o->label[0] = 0; v[0] = 0;
#define LBL(s) ab_str(o->label, 0, sizeof o->label, s)
#define HEAD(s) do { o->kind = AB_HEAD; LBL(s); } while (0)
#define TEXT(s) do { o->kind = AB_TEXT; ab_str(v, 0, C, s); } while (0)
    switch (r) {
    case AR_TAU_H: HEAD("TAU"); break;
    case AR_VERSION: LBL("VERSION"); { uint32_t i = 0; while (in->version[i] && in->version[i] != '+' && i + 1u < C) { v[i] = in->version[i]; i++; } v[i] = 0; } break;
    case AR_BUILD: LBL("BUILD"); { const char *p = in->version; while (*p && *p != '+') p++; if (*p == '+') ab_str(v, 0, C, p + 1); else ab_str(v, 0, C, "RELEASE"); } break;
    case AR_CHANNEL: LBL("CHANNEL"); ab_str(v, 0, C, ab_has(in->version, "-dev") ? "DEV" : (ab_has(in->version, "-preview") || ab_has(in->version, "-alpha") || ab_has(in->version, "-beta")) ? "PREVIEW" : "STABLE"); break;
    case AR_HW: LBL("HARDWARE"); { static const char hx[] = "0123456789ABCDEF"; for (int s = 28; s >= 0; s -= 4) v[n++] = hx[(in->core_version >> s) & 15u]; v[n] = 0; } break;
    case AR_MADE: LBL("MADE BY"); ab_str(v, 0, C, "ALFATREZE"); break;
    case AR_WEB: LBL("WEBSITE"); ab_str(v, 0, C, "GITHUB.COM/ALFATREZE/TAU-ALPHA"); break;
    case AR_LICENCE: LBL("LICENCE"); ab_str(v, 0, C, "MIT, PARTS KEEP THEIR OWN"); break;
    case AR_NOW_H: HEAD("PLAYING NOW"); break;
    case AR_FORMAT: LBL("FORMAT"); ab_str(v, 0, C, in->quality[0] ? in->quality : "-"); break;
    case AR_GAIN: LBL("REPLAYGAIN"); ab_str(v, 0, C, in->rg_mode == 1u ? "TRACK" : in->rg_mode == 2u ? "ALBUM" : "OFF"); break;
    case AR_EQ: LBL("EQUALISER"); ab_str(v, 0, C, in->eq ? in->eq : "OFF"); break;
    case AR_SPEED: LBL("SPEED"); {
        const uint32_t h = (in->tempo_q8 * 100u + 128u) / 256u; n = ab_hund(v, 0, C, h); n = ab_str(v, n, C, "X");
        if (h != 100u) ab_str(v, n, C, in->varispeed ? " PITCH CHANGES" : " PITCH KEPT"); } break;
    case AR_VOLUME: LBL("VOLUME"); n = ab_num(v, 0, C, in->volume); ab_str(v, n, C, "%"); break;
    case AR_LIB_H: HEAD("LIBRARY"); break;
    case AR_LIBRARY: LBL("LIBRARY");
        if (in->lib_state == 1u) { n = ab_num(v, 0, C, in->tracks); ab_str(v, n, C, in->tracks == 1u ? " TRACK" : " TRACKS"); }
        else if (in->lib_state == 2u) { n = ab_str(v, 0, C, "CANNOT BE READ (CODE "); n = ab_num(v, n, C, in->lib_err); ab_str(v, n, C, ")"); }
        else ab_str(v, 0, C, "NOT LOADED: SYNC WITH TAU OMEGA");
        break;
    case AR_ALBUMS: LBL("ALBUMS"); ab_num(v, 0, C, in->albums); break;
    case AR_ARTISTS: LBL("ARTISTS"); ab_num(v, 0, C, in->artists); break;
    case AR_LOOK_H: HEAD("LOOK AND FEEL"); break;
    case AR_THEME: LBL("THEME"); ab_str(v, 0, C, in->theme ? in->theme : "-"); break;
    case AR_MODE: LBL("MODE"); ab_str(v, 0, C, in->light ? "LIGHT" : "DARK"); break;
    case AR_COLOUR: LBL("COLOUR"); ab_str(v, 0, C, in->colour ? in->colour : "-"); break;
    case AR_FONT: LBL("FONT"); ab_str(v, 0, C, in->font ? in->font : "-"); break;
    case AR_METER: LBL("METER"); ab_str(v, 0, C, in->meter ? in->meter : "-"); break;
    case AR_ART: LBL("ALBUM ART"); ab_str(v, 0, C, in->art_on ? "ON" : "OFF"); break;
    case AR_CARD_H: HEAD("ON THE CARD"); break;
    case AR_THEMES: LBL("THEMES"); n = ab_num(v, 0, C, in->themes); ab_str(v, n, C, " CUSTOM"); break;
    case AR_EQPRE: LBL("EQ PRESETS"); n = ab_str(v, 0, C, "8 BUILT IN, "); n = ab_num(v, n, C, in->eq_user); ab_str(v, n, C, " YOURS"); break;
    case AR_METERS: LBL("METERS"); n = ab_num(v, 0, C, in->packs_run); n = ab_str(v, n, C, " OF "); n = ab_num(v, n, C, in->packs_seen); ab_str(v, n, C, in->packs_run == in->packs_seen ? " LOADED" : " LOADED: SOME FAILED"); break;
    case AR_SETTINGS: LBL("SETTINGS"); ab_str(v, 0, C, in->migrated ? "CARRIED OVER FROM AN OLDER VERSION" : "OK"); break;
    case AR_HEALTH_H: HEAD("HEALTH"); break;
    case AR_AUDIO: LBL("AUDIO");
        if (in->dropouts == 0u) ab_str(v, 0, C, "NO DROPOUTS");
        else { n = ab_num(v, 0, C, in->dropouts); ab_str(v, n, C, in->dropouts == 1u ? " DROPOUT SINCE START" : " DROPOUTS SINCE START"); }
        break;
    case AR_READS: LBL("CARD READS");
        if (in->read_errors == 0u) ab_str(v, 0, C, "NO ERRORS");
        else { n = ab_num(v, 0, C, in->read_errors); ab_str(v, n, C, in->read_errors == 1u ? " ERROR (RETRIED)" : " ERRORS (RETRIED)"); }
        break;
    case AR_UPTIME: LBL("RUNNING FOR");
        if (in->uptime_s < 60u) { n = ab_num(v, 0, C, in->uptime_s); ab_str(v, n, C, " SEC"); }
        else if (in->uptime_s < 3600u) { n = ab_num(v, 0, C, in->uptime_s / 60u); ab_str(v, n, C, " MIN"); }
        else { n = ab_num(v, 0, C, in->uptime_s / 3600u); n = ab_str(v, n, C, " H "); n = ab_num(v, n, C, (in->uptime_s / 60u) % 60u); ab_str(v, n, C, " MIN"); }
        break;
    case AR_CRED_H: HEAD("CREDITS AND LICENCES"); break;
    case AR_C1: TEXT("MP3 DECODER: HELIX (RPSL 1.0)"); break;
    case AR_C2: TEXT("FONTS: TAMZEN, SPACE GROTESK (OPEN LICENCES)"); break;
    case AR_C3: TEXT("PLAYER BASE: HARPMUDD (MIT)"); break;
    case AR_C4: TEXT("PLATFORM: ANALOGUE POCKET"); break;
    case AR_HELP_H: HEAD("HELP"); break;
    case AR_H1: TEXT("SETTINGS > HELP: THE BUTTONS AND HOW TO USE THEM"); break;
    case AR_H2: TEXT("SELECT+START ON THIS PAGE: A SUPPORT REPORT"); break;
    case AR_H3: TEXT("SEND IT WITH A BUG REPORT TO THE WEBSITE ABOVE"); break;
    default: break;
    }
#undef LBL
#undef HEAD
#undef TEXT
}
static inline uint32_t about_count(const about_in_t *in) { uint32_t n = 0; for (uint32_t r = 0; r < AR_COUNT; r++) n += (uint32_t)ab_visible(in, r); return n; }
static inline int about_get(const about_in_t *in, uint32_t i, about_row_t *o)
{
    uint32_t k = 0;
    for (uint32_t r = 0; r < AR_COUNT; r++) {
        if (!ab_visible(in, r)) continue;
        if (k++ == i) { ab_fill(in, r, o);
#if BUG == 1
            if (r == AR_AUDIO) ab_str(o->value, 0, sizeof o->value, "NO DROPOUTS");          /* the dropout count is never shown */
#endif
            return 1; }
    }
    return 0;
}
#endif
