#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "library.h"
#include "../platform/mmio.h"
#include "slot.h"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../cores/library_core.h"

#define PS_BASE   0xA4000000u
#define LIB_IMG   ((volatile uint8_t *)(uintptr_t)(PS_BASE + 0x010000u))
#define LIB_CAP   0x400000u
#define LIB_Q     ((uint16_t *)(uintptr_t)(PS_BASE + 0x510000u))

extern char _tag_start;
uint8_t lib_state, lib_err; uint32_t lib_ms, lib_n_tracks;
static lib_t lib;

static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx; (void)dst;
    return slot_read_blocking(SLOT_LIB, off, (uint32_t)(uintptr_t)&_tag_start, len, CLK_HZ * 5u);
}

static int psram_ok(void)           /* the window does not exist on an older bitstream: prove it before the first store */
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG_RD(R_PS_ID) != 0x50535231u || !((REG_RD(R_PS_CFG) >> 16) & 1u)) return 0;
    volatile uint32_t *w[2] = { (volatile uint32_t *)LIB_IMG, (volatile uint32_t *)(LIB_IMG + LIB_CAP - 4u) };
    for (uint32_t k = 0; k < 2u; k++) for (uint32_t i = 0; i < 2u; i++) { w[i][0] = pat[k] ^ i; if (w[i][0] != (pat[k] ^ i)) return 0; }
    return 1;
}

void library_load(void)
{
    uint32_t t0 = cycles();
    lib_state = 0; lib_err = 0;
    if (!slot_read_blocking(SLOT_LIB, 0u, (uint32_t)(uintptr_t)&_tag_start, 1u, CLK_HZ * 3u)) { lib_state = 2; lib_err = LIB_E_NOFILE; return; }
    if (!psram_ok()) { lib_state = 2; lib_err = 0x80; return; }
    uint8_t *win = (uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start);      /* the tag zone through the uncached alias */
    int e = lib_load(&lib, rd, 0, win, LIB_IMG, LIB_CAP);
    lib_ms = (cycles() - t0) / (CLK_HZ / 1000u);
    if (e) { lib_state = 2; lib_err = (uint8_t)e; return; }
    lib_state = 1; lib_n_tracks = lib.n_tracks;
}
uint32_t library_path(uint32_t id, char *out, uint32_t cap) { return lib_state == 1 && id < lib.n_tracks ? lib_path(&lib, id, out, cap) : 0; }
uint32_t library_size(uint32_t id) { lib_det_t d; return lib_state == 1 && lib_details(&lib, id, &d) ? d.size : 0; }
uint16_t *library_queue_buf(void) { return LIB_Q; }
uint16_t *library_orig_buf(void) { return (uint16_t *)(uintptr_t)(PS_BASE + 0x518000u); }
uint32_t library_rate(uint32_t id) { lib_det_t d; return lib_state == 1 && lib_details(&lib, id, &d) ? d.rate : 0; }
uint32_t library_codec(uint32_t id) { lib_det_t d; if (lib_state != 1) return 0; if (lib_details(&lib, id, &d)) return d.codec; lib_track_t t; if (id >= lib.n_tracks) return 0; lib_track(&lib, id, &t); return t.fmt; }
uint32_t library_album_jump(uint32_t id, int dir)
{
    lib_track_t t; lib_album_t a;
    if (lib_state != 1 || id >= lib.n_tracks) return 0xFFFFFFFFu;
    lib_track(&lib, id, &t); lib_album(&lib, t.album, &a);
    if (dir > 0) { uint32_t n = (uint32_t)a.first_track + a.n_tracks; return n < lib.n_tracks ? n : 0xFFFFFFFFu; }
    if (id > a.first_track) return a.first_track;                       /* inside an album: back to its first track */
    if (!a.first_track) return 0xFFFFFFFFu;
    lib_track(&lib, a.first_track - 1u, &t); lib_album(&lib, t.album, &a); return a.first_track;
}
int library_rg(uint32_t id, uint32_t *flags, int32_t *tg, int32_t *ag, uint32_t *tp, uint32_t *ap) { lib_det_t d; if (lib_state != 1 || !lib_details(&lib, id, &d)) return 0; *flags = d.flags; *tg = d.tg; *ag = d.ag; *tp = d.tp; *ap = d.ap; return 1; }
void library_meta(uint32_t id, char *title, uint32_t tcap, char *artist, uint32_t acap, char *album, uint32_t bcap)
{
    title[0] = artist[0] = album[0] = 0;
    if (lib_state != 1 || id >= lib.n_tracks) return;
    lib_track_t t; lib_album_t a; lib_artist_t r;
    lib_track(&lib, id, &t); title[lib_str(&lib, t.title, title, tcap - 1u)] = 0;
    lib_album(&lib, t.album, &a); album[lib_str(&lib, a.title, album, bcap - 1u)] = 0;
    lib_artist(&lib, a.artist, &r); artist[lib_str(&lib, r.name, artist, acap - 1u)] = 0;
}
int library_facts(uint32_t id, uint32_t *codec, uint32_t *ch, uint32_t *kbps, uint32_t *rate)
{
    lib_det_t d; if (lib_state != 1 || !lib_details(&lib, id, &d)) return 0;
    *codec = d.codec; *ch = d.ch; *kbps = d.kbps; *rate = d.rate; return 1;
}

#include "../cores/browse_core.h"
uint32_t library_br_count(uint8_t kind, uint32_t parent)
{
    if (lib_state != 1) return 0;
    switch (kind) {
    case BK_HOME: return 4u + (lib.n_lists ? 1u : 0u);
    case BK_ARTISTS: return lib.n_artists;
    case BK_ALBUMS: return lib.n_albums;
    case BK_ALBUMS_OF: { lib_artist_t r; lib_artist(&lib, parent, &r); return r.n_albums; }
    case BK_TRACKS_OF: { lib_album_t a; lib_album(&lib, parent, &a); return a.n_tracks; }
    case BK_TRACKS: return lib.n_tracks;
    case BK_LISTS: return lib.n_lists;
    case BK_TRACKS_LIST: { lib_list_t l; lib_list(&lib, parent, &l); return l.n; }
    }
    return 0;
}
uint32_t library_br_child(uint8_t kind, uint32_t parent, uint32_t pos)
{
    switch (kind) {
    case BK_ARTISTS: case BK_LISTS: return pos;
    case BK_ALBUMS: return lib_album_by_title(&lib, pos);
    case BK_ALBUMS_OF: { lib_artist_t r; lib_artist(&lib, parent, &r); return (uint32_t)r.first_album + pos; }
    case BK_TRACKS_OF: { lib_album_t a; lib_album(&lib, parent, &a); return (uint32_t)a.first_track + pos; }
    case BK_TRACKS: return lib_track_by_title(&lib, pos);
    case BK_TRACKS_LIST: return lib_list_item(&lib, parent, pos);
    }
    return 0;
}
void library_br_text(uint8_t kind, uint32_t parent, uint32_t pos, char *out, uint32_t cap)
{
    static const char *const home[5] = { "ARTISTS", "ALBUMS", "ALL TRACKS", "SHUFFLE ALL", "PLAYLISTS" };
    out[0] = 0;
    if (lib_state != 1) return;
    switch (kind) {
    case BK_HOME: { const char *s = home[pos < 5u ? pos : 4u]; uint32_t i = 0; for (; s[i] && i + 1u < cap; i++) out[i] = s[i]; out[i] = 0; break; }
    case BK_ARTISTS: { lib_artist_t r; lib_artist(&lib, pos, &r); lib_str(&lib, r.name, out, cap); break; }
    case BK_ALBUMS: case BK_ALBUMS_OF: { lib_album_t a; lib_album(&lib, library_br_child(kind, parent, pos), &a); lib_str(&lib, a.title, out, cap); break; }
    case BK_LISTS: { lib_list_t l; lib_list(&lib, pos, &l); lib_str(&lib, l.name, out, cap); break; }
    default: { lib_track_t t; lib_track(&lib, library_br_child(kind, parent, pos), &t); lib_str(&lib, t.title, out, cap); break; }
    }
}
uint32_t library_build_queue(uint8_t kind, uint32_t parent, uint32_t pos, uint16_t *q, uint32_t cap, uint32_t *start, uint32_t seed)
{
    uint32_t n = 0; *start = 0;
    if (lib_state != 1) return 0;
    switch (kind) {
    case BK_TRACKS_OF: { lib_album_t a; lib_album(&lib, parent, &a); n = a.n_tracks; if (n > cap) n = cap; for (uint32_t i = 0; i < n; i++) q[i] = (uint16_t)(a.first_track + i); *start = pos; break; }
    case BK_TRACKS: n = lib.n_tracks < cap ? lib.n_tracks : cap; for (uint32_t i = 0; i < n; i++) q[i] = (uint16_t)lib_track_by_title(&lib, i); *start = pos; break;
    case BK_TRACKS_LIST: { lib_list_t l; lib_list(&lib, parent, &l); n = l.n < cap ? l.n : cap; for (uint32_t i = 0; i < n; i++) q[i] = (uint16_t)lib_list_item(&lib, parent, i); *start = pos; break; }
    case LQ_ALL_NATURAL: n = lib.n_tracks < cap ? lib.n_tracks : cap; for (uint32_t i = 0; i < n; i++) q[i] = (uint16_t)i; break;
    case LQ_ALL_SHUFFLED: n = lib_queue_shuffle_all(&lib, q, seed); break;
    }
    return n;
}
uint32_t library_cover_path(uint32_t id, char *out, uint32_t cap)
{
    lib_det_t d; char p[LIBP_MAX_PATH];
    if (lib_state != 1 || !lib_details(&lib, id, &d) || !(d.flags & 4u)) return 0;          /* flag bit 2: a tau-art sidecar exists */
    uint32_t n = lib_path(&lib, id, p, sizeof p), last = 0;
    if (!n) return 0;
    for (uint32_t i = 0; i < n; i++) if (p[i] == '/') last = i + 1u;
    static const char tail[] = "tau-art/cover_128.pal256.timg";
    if (!last || last + sizeof tail > cap) return 0;
    for (uint32_t i = 0; i < last; i++) out[i] = p[i];
    for (uint32_t i = 0; i < sizeof tail; i++) out[last + i] = tail[i];
    return last + (uint32_t)sizeof tail - 1u;
}
