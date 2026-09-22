/* Runs fw/library_core.h (the real firmware code, real toolchain) under tools/rv32sim.py against an index file given
 * as the simulator's data file. Prints the load result and hashes of everything the core can read back; sim/test_library_fw.py
 * computes the same values from tools/tau_library.py and compares. */
#include "hostio.h"
#include "../../fw/library_core.h"

#ifndef HMEM
#define HMEM (3u << 19)                          /* 1.5 MiB index memory (PSRAM stand-in); -DHMEM=... to shrink */
#endif
static uint8_t win[LIB_WIN] __attribute__((aligned(16)));
static uint8_t img[HMEM] __attribute__((aligned(16)));
static uint16_t queue[LIB_MAX_TRACKS];
static uint8_t seen[LIB_MAX_TRACKS / 8u];

static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx;
    if (off + len > hfilesize()) return 0;          /* the host fails a read that extends past the end */
    return hread(off, dst, len) == len;
}

static uint32_t fnv;
static void h_reset(void) { fnv = 2166136261u; }
static void h_byte(uint8_t b) { fnv ^= b; fnv *= 16777619u; }
static void h_str(const char *s) { while (*s) h_byte((uint8_t)*s++); h_byte('\n'); }
static void out(const char *tag) { hputs(tag); hputc(' '); hputx(fnv); hnl(); }

int main(void)
{
    lib_t l;
    int e = lib_load(&l, rd, 0, win, img, sizeof(img));
    if (e) { hputs("LOAD E"); hputu((uint32_t)e); hnl(); return 0; }
#ifdef LOAD_ONLY                                 /* instruction count of the load alone (simulator prints it) */
    return 0;
#endif
    hputs("LOAD OK "); hputu(l.n_tracks); hputc(' '); hputu(l.n_albums); hputc(' ');
    hputu(l.n_artists); hputc(' '); hputu(l.n_lists); hputc(' '); hputx(l.build_id); hnl();

    char buf[256];
    uint32_t bad = 0;
    h_reset();                                       /* the stored image must equal the file, header included */
    for (uint32_t i = 0; i < LIB_HDR; i++) h_byte(img[i]);
    out("HEADER");
    h_reset();
    for (uint32_t i = 0; i < l.n_tracks; i++) {
        uint32_t n = lib_path(&l, i, buf, sizeof(buf));
        if (!n) { bad++; continue; }
        h_str(buf);
    }
    out("PATHS"); hputs("PATHFAIL "); hputu(bad); hnl();

    h_reset();
    for (uint32_t i = 0; i < l.n_tracks; i++) {
        lib_track_t t; lib_track(&l, i, &t);
        lib_str(&l, t.title, buf, sizeof(buf)); h_str(buf);
        h_byte((uint8_t)t.secs); h_byte((uint8_t)(t.secs >> 8)); h_byte((uint8_t)t.tno); h_byte((uint8_t)(t.tno >> 8));
        h_byte((uint8_t)t.album); h_byte((uint8_t)(t.album >> 8)); h_byte(t.fmt);
    }
    out("TRACKS");

    h_reset();
    for (uint32_t i = 0; i < l.n_albums; i++) {
        lib_album_t a; lib_album(&l, i, &a);
        lib_str(&l, a.title, buf, sizeof(buf)); h_str(buf);
        lib_str(&l, a.dir, buf, sizeof(buf)); h_str(buf);
        h_byte((uint8_t)a.artist); h_byte((uint8_t)(a.artist >> 8)); h_byte((uint8_t)a.year); h_byte((uint8_t)(a.year >> 8));
        h_byte((uint8_t)a.first_track); h_byte((uint8_t)(a.first_track >> 8));
        h_byte((uint8_t)a.n_tracks); h_byte((uint8_t)(a.n_tracks >> 8));
    }
    out("ALBUMS");

    h_reset();
    for (uint32_t i = 0; i < l.n_artists; i++) {
        lib_artist_t a; lib_artist(&l, i, &a);
        lib_str(&l, a.name, buf, sizeof(buf)); h_str(buf);
        h_byte((uint8_t)a.first_album); h_byte((uint8_t)(a.first_album >> 8));
        h_byte((uint8_t)a.n_albums); h_byte((uint8_t)(a.n_albums >> 8));
    }
    out("ARTISTS");

    h_reset();
    for (uint32_t i = 0; i < l.n_albums; i++) { uint32_t v = lib_album_by_title(&l, i); h_byte((uint8_t)v); h_byte((uint8_t)(v >> 8)); }
    for (uint32_t i = 0; i < l.n_tracks; i++) { uint32_t v = lib_track_by_title(&l, i); h_byte((uint8_t)v); h_byte((uint8_t)(v >> 8)); }
    out("ORDER");

    h_reset();
    for (uint32_t v = 0; v < 3u; v++) {
        for (uint32_t c = 0; c < 27u; c++) {
            uint32_t p = lib_letter(&l, v, c ? (char)('a' + c - 1u) : '5');
            h_byte((uint8_t)p); h_byte((uint8_t)(p >> 8));
        }
    }
    out("LETTERS");

    h_reset();                                       /* playlists: names, lengths and every item */
    for (uint32_t i = 0; i < l.n_lists; i++) {
        lib_list_t p; lib_list(&l, i, &p);
        lib_str(&l, p.name, buf, sizeof(buf)); h_str(buf);
        h_byte((uint8_t)p.n); h_byte((uint8_t)(p.n >> 8));
        for (uint32_t k = 0; k < p.n; k++) { uint32_t v = lib_list_item(&l, i, k); h_byte((uint8_t)v); h_byte((uint8_t)(v >> 8)); }
    }
    out("LISTS");
    if (l.n_lists) {
        uint32_t n0 = lib_queue_list(&l, 0, queue), n1;
        h_reset();
        for (uint32_t i = 0; i < n0; i++) { h_byte((uint8_t)queue[i]); h_byte((uint8_t)(queue[i] >> 8)); }
        n1 = lib_queue_list(&l, l.n_lists - 1u, queue);
        for (uint32_t i = 0; i < n1; i++) { h_byte((uint8_t)queue[i]); h_byte((uint8_t)(queue[i] >> 8)); }
        hputs("QLIST "); hputu(n0); hputc(' '); hputu(n1); hputc(' '); out("hash");
    }

    h_reset();                                       /* letter jumps from (sampled) positions of every view, both ways */
    for (uint32_t v = 0; v < 3u; v++) {
        uint32_t cnt = v == 0 ? l.n_artists : v == 1 ? l.n_albums : l.n_tracks;
        for (uint32_t p = 0; p < cnt; p += (cnt > 300u ? 7u : 1u)) {
            for (int d = -1; d <= 1; d += 2) {
                uint32_t j = lib_jump(&l, v, cnt, p, d);
                h_byte((uint8_t)j); h_byte((uint8_t)(j >> 8));
            }
        }
    }
    out("JUMPS");

    static const uint32_t seeds[2] = { 1u, 12345u };
    for (uint32_t k = 0; k < 2u; k++) {
        uint32_t n = lib_queue_shuffle_all(&l, queue, seeds[k]);
        for (uint32_t i = 0; i < sizeof(seen); i++) seen[i] = 0;
        uint32_t dup = 0;
        h_reset();
        for (uint32_t i = 0; i < n; i++) {
            uint32_t v = queue[i];
            if (v >= n || (seen[v >> 3] & (1u << (v & 7u)))) dup++;
            else seen[v >> 3] |= (uint8_t)(1u << (v & 7u));
            h_byte((uint8_t)v); h_byte((uint8_t)(v >> 8));
        }
        hputs("SHUF "); hputu(seeds[k]); hputs(" dup "); hputu(dup); hputc(' '); out("hash");
    }

    if (l.n_albums) {
        uint32_t last = l.n_albums - 1u;
        uint32_t n0 = lib_queue_album(&l, 0, queue), n1;
        h_reset();
        for (uint32_t i = 0; i < n0; i++) { h_byte((uint8_t)queue[i]); h_byte((uint8_t)(queue[i] >> 8)); }
        n1 = lib_queue_album(&l, last, queue);
        for (uint32_t i = 0; i < n1; i++) { h_byte((uint8_t)queue[i]); h_byte((uint8_t)(queue[i] >> 8)); }
        hputs("QALB "); hputu(n0); hputc(' '); hputu(n1); hputc(' '); out("hash");
    }
    return 0;
}
