/* fw2/cores/help_core.h -- Player 2 Help (Menu > HELP) and the fallback screens of Now Playing when there is no usable library.
 * Help is three pages of text (add music with Tau Omega, where things are on the card, the Python alternative from the repo); Up and Down turn the page (wrapping, Helios's rule), B or Left goes back.
 * The fallback classification says which of four screens Now Playing shows instead of the empty player: no library file, an unreadable one (the LIB_E_* code is shown), an empty one, or a library whose track
 * files are not on the card. Every fallback screen shows the Tau Omega address. Lines are at most HP_MAXCH characters so they fit the widest font of Settings > Appearance.
 * Pure: no drawing, no registers. Host-tested (sim/test_p2_help.py). */
#ifndef HELP_CORE_H
#define HELP_CORE_H
#include <stdint.h>
#include "helios_core.h"
#ifndef BUG
#define BUG 0
#endif
#define HP_URL_OMEGA "github.com/alfatreze/Tau-Omega"
#define HP_URL_REPO  "github.com/alfatreze/Tau-Alpha"
#define HP_MAXCH 36u
#define FB_META_MAXCH 24u                                    /* the lines beside the cover (240 px) in the widest font */
enum { HS_TEXT = 0, HS_ACCENT, HS_DIM, HS_GAP };               /* a line's style; HS_GAP is an empty line */
typedef struct { uint8_t style; const char *s; } hp_line_t;
typedef struct { const char *title; const hp_line_t *lines; uint8_t n; } hp_page_t;

static const hp_line_t hp_p1[] = {
    { HS_TEXT, "Tau Omega puts music on the card and" }, { HS_TEXT, "builds the library TAU needs." }, { HS_GAP, "" },
    { HS_TEXT, "1  Get it (Mac, Windows, Linux):" }, { HS_ACCENT, "   " HP_URL_OMEGA },
    { HS_TEXT, "2  Put the SD card in your computer" }, { HS_TEXT, "3  Open Tau Omega, pick your music" }, { HS_TEXT, "   folder and press Sync" },
    { HS_TEXT, "4  Eject the card and start TAU" }, { HS_GAP, "" },
    { HS_DIM, "Details: the Help tab of Tau Omega," }, { HS_DIM, "or docs/guide/MEDIA_AND_TOOLS.md" } };
static const hp_line_t hp_p2[] = {
    { HS_TEXT, "On the card:" }, { HS_GAP, "" },
    { HS_ACCENT, "Assets/tau/common/" }, { HS_TEXT, "  music, covers, tau-library.tdb" },
    { HS_ACCENT, "Settings/alfatreze.TAU/" }, { HS_TEXT, "  what TAU saves (delete to reset)" },
    { HS_ACCENT, "Memories/Screenshots/" }, { HS_TEXT, "  Menu + Start screenshots" }, { HS_GAP, "" },
    { HS_TEXT, "TAU Preview reads the same music." }, { HS_GAP, "" },
    { HS_DIM, "A library error shows its code; the" }, { HS_DIM, "Tau Omega help explains each one." } };
static const hp_line_t hp_p3[] = {
    { HS_TEXT, "Python 3 and the project repo work:" }, { HS_ACCENT, HP_URL_REPO }, { HS_GAP, "" },
    { HS_TEXT, "python3 tools/sync_media.py" }, { HS_TEXT, "  \"My Music\" --core alfatreze.TAU" }, { HS_TEXT, "  --library" }, { HS_GAP, "" },
    { HS_TEXT, "It copies, converts, verifies and" }, { HS_TEXT, "writes the library." }, { HS_GAP, "" },
    { HS_DIM, "Tau Omega is the easy way. Details:" }, { HS_DIM, "docs/guide/MEDIA_AND_TOOLS.md" } };
#define HP_N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
static const hp_page_t hp_pages[] = { { "ADD MUSIC", hp_p1, HP_N(hp_p1) }, { "WHERE THINGS ARE", hp_p2, HP_N(hp_p2) }, { "WITHOUT TAU OMEGA", hp_p3, HP_N(hp_p3) } };
#define HP_PAGES ((uint8_t)(sizeof(hp_pages) / sizeof(hp_pages[0])))

typedef struct { uint8_t page; } hp_t;
static inline void hp_open(hp_t *h) { h->page = 0; }
/* One key edge (bit 0 Up, 1 Down, 2 Left, 5 B): returns 1 when Help should close. */
static inline int hp_input(hp_t *h, uint32_t edge)
{
    if (edge & ((1u << 0) | (1u << 1))) {
#if BUG == 1
        h->page = (uint8_t)((edge & 1u) ? (h->page ? h->page - 1u : 0u) : (h->page + 1u < HP_PAGES ? h->page + 1u : HP_PAGES - 1u));       /* clamps instead of wrapping */
#else
        h->page = (uint8_t)hl_wrap(h->page, HP_PAGES, (edge & 1u) != 0);
#endif
        return 0;
    }
    return (edge & ((1u << 2) | (1u << 5))) != 0;
}

/* ---- the fallback screens ---- */
enum { FB_NONE = 0, FB_NOLIB, FB_UNREADABLE, FB_EMPTY, FB_NOFILES };
#define FB_LIB_NOFILE 10u                                     /* LIB_E_NOFILE (stream/library.h) */
/* lib_state: 0 not loaded yet, 1 loaded, 2 failed; lib_err the LIB_E_* code; n_tracks the library's tracks; open_failed: tracks that could not be opened and nothing is playing. */
static inline uint32_t fb_kind(uint32_t lib_state, uint32_t lib_err, uint32_t n_tracks, uint32_t open_failed)
{
    if (lib_state == 2u) {
#if BUG == 2
        (void)lib_err; return FB_UNREADABLE;                  /* a missing file shown as damaged */
#else
        return lib_err == FB_LIB_NOFILE ? FB_NOLIB : FB_UNREADABLE;
#endif
    }
    if (lib_state != 1u) return FB_NONE;
    if (!n_tracks) return FB_EMPTY;
    return open_failed ? FB_NOFILES : FB_NONE;
}
typedef struct { const char *pill, *title, *l1, *l2, *step[3], *small[2]; uint8_t red; } fb_text_t;       /* red: the pill is an error (red) */
static const fb_text_t fb_texts[] = {
    [FB_NOLIB] = { "NO LIBRARY", "NO LIBRARY FOUND", "tau-library.tdb missing", "The card has no index.",
                   { "1  Get Tau Omega for your computer", "2  Sync your music to this card", "3  Put the card back, start TAU" }, { "Press A for the step by step help.", "" }, 0 },
    [FB_UNREADABLE] = { "ERROR", "CAN'T READ LIBRARY", "The index is damaged", "or from another version.",
                   { "Sync again with Tau Omega to", "rebuild the library on this card.", "" }, { "Your music files are not touched.", "Press A for the step by step help." }, 1 },
    [FB_EMPTY] = { "0 TRACKS", "LIBRARY IS EMPTY", "No MP3 or FLAC found", "when it was built.",
                   { "Add music, then sync again with", "Tau Omega to rebuild the library.", "" }, { "Supported: MP3 and FLAC.", "Press A for the step by step help." }, 0 },
    [FB_NOFILES] = { "CAN'T OPEN", "FILES NOT FOUND", "Tracks are missing", "from the card.",
                   { "The library may belong to another", "core or card. Sync again with", "Tau Omega to rebuild it here." }, { "Press A for the step by step help.", "" }, 1 },
};
#endif
