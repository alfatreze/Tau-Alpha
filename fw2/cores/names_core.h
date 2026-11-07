/* Name sidecar: portable core of the firmware reader (spec: docs/features/NAME_SIDECAR_SPEC.md; reference: tools/name_sidecar.py).
 *
 * Pure logic like library_core.h (no MMIO, no firmware globals), so the SAME code is compiled into the Player 2 firmware (fw2/stream/library.c)
 * and into sim/p2_names_harness.c, which loads files built by tools/name_sidecar.py. State: names_t (about 48 B); everything else is read
 * from the image on demand (PSRAM in the firmware, an array in the harness). No 64-bit arithmetic, no division, no libc.
 *
 * What it does: loads and validates tau-names.bin (any failed check = "no sidecar", never a reason to refuse the library), then answers
 * "the original text of this track / album / artist field, as the screen can draw it". Today the screen font is Latin-1 (docs/features/FONT_SPEC.md),
 * so the original is used only when EVERY character is in U+0020..U+007E or U+00A1..U+00FF (except U+00AD); then it is converted from UTF-8 to
 * one byte per character. A larger font changes names_cov() and the output conversion, nothing else here. A path is never taken from here: the
 * index owns paths. */
#ifndef P2_NAMES_CORE_H
#define P2_NAMES_CORE_H

#include <stdint.h>
#include "library_core.h"               /* lib_read_fn, lib_crc_update (the same CRC-32), lib_ld32 */

#define NAMES_HDR        48u
#define NAMES_MAX_FILE   (1u << 20)    /* spec 5: hard cap 1 MiB */

enum {
    NAMES_OK = 0, NAMES_E_NOFILE = 20, NAMES_E_HDR = 21, NAMES_E_BIND = 22, NAMES_E_COUNT = 23,
    NAMES_E_LAYOUT = 24, NAMES_E_SIZE = 25, NAMES_E_STR = 26, NAMES_E_TOOBIG = 27
};

enum { NAMES_TRACK = 0, NAMES_ALBUM = 1, NAMES_ARTIST = 2 };
/* fields: track 0 file, 1 title, 2 artist, 3 album; album 0 title, 1 artist; artist 0 name */
enum { NF_FILE = 0, NF_TITLE = 1, NF_ARTIST = 2, NF_ALBUM = 3 };      /* track records */
enum { NF_ALBUM_TITLE = 0, NF_ALBUM_ARTIST = 1 };                      /* album records (an artist record has the one field 0) */

typedef struct {
    const volatile uint8_t *img;       /* whole file image */
    uint32_t size, o_trk, o_alb, o_art, o_pool, n_pool;
    uint16_t n_tracks, n_albums, n_artists;
} names_t;

static inline uint32_t names_align16(uint32_t n) { return (n + 15u) & ~15u; }

/* Reads tau-names.bin via `rd` (4 KiB windows in `win`, 4-byte aligned, LIB_WIN bytes) into `mem` (`mem_cap` bytes) and checks, in this order:
 * file present (NOFILE), magic / version / header CRC (HDR), `build_id` = the loaded index's build_id (BIND), the three counts (COUNT), the layout
 * and the file size (LAYOUT, SIZE, TOOBIG), the pool's NUL framing and every table offset (STR). `n` is only valid on NAMES_OK. */
static int names_load(names_t *n, lib_read_fn rd, void *ctx, uint8_t *win, volatile uint8_t *mem, uint32_t mem_cap,
                      uint32_t build_id, uint32_t n_tracks, uint32_t n_albums, uint32_t n_artists)
{
    if (!rd(ctx, 0u, win, NAMES_HDR))
        return rd(ctx, 0u, win, 1u) ? NAMES_E_HDR : NAMES_E_NOFILE;
    if (win[0] != 'T' || win[1] != 'N' || win[2] != 'M' || win[3] != 'S' || lib_ld16(win + 4) > 1u) return NAMES_E_HDR;
    if (~lib_crc_update(0xFFFFFFFFu, win, 32u) != lib_ld32(win + 32)) return NAMES_E_HDR;
    if (lib_ld32(win + 8) != build_id) return NAMES_E_BIND;
    uint32_t nt = lib_ld32(win + 12), na = lib_ld32(win + 16), nr = lib_ld32(win + 20);
    if (nt != n_tracks || na != n_albums || nr != n_artists) return NAMES_E_COUNT;
    uint32_t o_trk = NAMES_HDR, o_alb = names_align16(o_trk + nt * 16u), o_art = names_align16(o_alb + na * 8u);
    uint32_t o_pool = lib_ld32(win + 24), n_pool = lib_ld32(win + 28);
    if (o_pool != names_align16(o_art + nr * 4u) || n_pool < 1u) return NAMES_E_LAYOUT;
    if (o_pool > NAMES_MAX_FILE || n_pool > NAMES_MAX_FILE - o_pool) return NAMES_E_TOOBIG;
    uint32_t fsz = o_pool + n_pool;
    if (fsz > mem_cap) return NAMES_E_TOOBIG;
    for (uint32_t i = 0; i < NAMES_HDR; i++) mem[i] = win[i];
    if (!rd(ctx, fsz - 1u, win, 1u) || rd(ctx, fsz, win, 1u)) return NAMES_E_SIZE;       /* the file is exactly as long as the header says */
    for (uint32_t off = NAMES_HDR; off < fsz; ) {
        uint32_t c = fsz - off;
        if (c > LIB_WIN) c = LIB_WIN;
        if (!rd(ctx, off, win, c)) return NAMES_E_SIZE;
        volatile uint8_t *d = mem + off;
        if (((off | c) & 3u) == 0u) {
            volatile uint32_t *dw = (volatile uint32_t *)d;
            const uint32_t *sw = (const uint32_t *)win;
            for (uint32_t i = 0; i < (c >> 2); i++) dw[i] = sw[i];
        } else {
            for (uint32_t i = 0; i < c; i++) d[i] = win[i];
        }
        off += c;
    }
    const volatile uint8_t *h = mem, *pool = mem + o_pool;
    if (pool[0] != 0u || pool[n_pool - 1u] != 0u) return NAMES_E_STR;
    /* every offset: 0 (absent) or the start of a string (the byte before it is the NUL that ended the last one) */
    uint32_t nwords = nt * 4u + na * 2u;
    for (uint32_t i = 0; i < nwords; i++) {
        uint32_t off = lib_ld32(h + (i < nt * 4u ? o_trk + 4u * i : o_alb + 4u * (i - nt * 4u)));
        if (off && (off >= n_pool || pool[off - 1u] != 0u)) return NAMES_E_STR;
    }
    for (uint32_t i = 0; i < nr; i++) {
        uint32_t off = lib_ld32(h + o_art + 4u * i);
        if (off && (off >= n_pool || pool[off - 1u] != 0u)) return NAMES_E_STR;
    }
    n->img = h; n->size = fsz; n->o_trk = o_trk; n->o_alb = o_alb; n->o_art = o_art; n->o_pool = o_pool; n->n_pool = n_pool;
    n->n_tracks = (uint16_t)nt; n->n_albums = (uint16_t)na; n->n_artists = (uint16_t)nr;
    return NAMES_OK;
}

/* The NUL-terminated UTF-8 string of a field, or 0 when absent / the id or field is out of range. */
static const volatile uint8_t *names_ptr(const names_t *n, uint8_t kind, uint32_t id, uint8_t field)
{
    uint32_t off;
    switch (kind) {
    case NAMES_TRACK:  if (id >= n->n_tracks || field > 3u) return 0;  off = lib_ld32(n->img + n->o_trk + 16u * id + 4u * field); break;
    case NAMES_ALBUM:  if (id >= n->n_albums || field > 1u) return 0;  off = lib_ld32(n->img + n->o_alb + 8u * id + 4u * field); break;
    case NAMES_ARTIST: if (id >= n->n_artists || field) return 0;      off = lib_ld32(n->img + n->o_art + 4u * id); break;
    default: return 0;
    }
    return off ? n->img + n->o_pool + off : 0;
}

/* Can the screen font draw code point c? (Latin-1: FONT_SPEC.md; U+00AD soft hyphen and every control are not drawable.) */
static inline int names_cov(uint32_t c)
{
    return (c >= 0x20u && c <= 0x7Eu) || (c >= 0xA1u && c <= 0xFFu && c != 0xADu);
}

/* One strict UTF-8 character from a volatile string, advancing *p. Anything malformed (stray continuation, truncated, overlong, a surrogate,
 * above U+10FFFF) returns 0xFFFFFFFF. */
static uint32_t names_u8(const volatile uint8_t **p)
{
    const volatile uint8_t *s = *p;
    uint32_t c = s[0], k, min;
    if (c < 0x80u) { *p = s + 1; return c; }
    if      (c >= 0xC2u && c <= 0xDFu) { k = 1u; c &= 0x1Fu; min = 0x80u; }
    else if (c >= 0xE0u && c <= 0xEFu) { k = 2u; c &= 0x0Fu; min = 0x800u; }
    else if (c >= 0xF0u && c <= 0xF4u) { k = 3u; c &= 0x07u; min = 0x10000u; }
    else return 0xFFFFFFFFu;
    for (uint32_t i = 1; i <= k; i++) {
        if ((s[i] & 0xC0u) != 0x80u) return 0xFFFFFFFFu;
        c = (c << 6) | (s[i] & 0x3Fu);
    }
    if (c < min || c > 0x10FFFFu || (c >= 0xD800u && c <= 0xDFFFu)) return 0xFFFFFFFFu;
    *p = s + k + 1u;
    return c;
}

/* Spec 6 for one field: if the original exists and EVERY character is drawable, writes it to `out` as Latin-1 (one byte a character, NUL-terminated,
 * cut at cap-1) and returns its length (at least 1). Otherwise returns 0 and leaves `out` untouched, so the caller keeps the index text it already
 * put there. One undrawable or malformed character sends the whole string to the fallback: no row ever shows a half-drawn title. */
static uint32_t names_display(const names_t *n, uint8_t kind, uint32_t id, uint8_t field, char *out, uint32_t cap)
{
    const volatile uint8_t *p = names_ptr(n, kind, id, field);
    if (!p || cap < 2u || !*p) return 0;
    for (const volatile uint8_t *q = p; *q; ) {              /* first pass: drawable and well-formed, whole string */
        uint32_t c = names_u8(&q);
        if (c == 0xFFFFFFFFu || !names_cov(c)) return 0;
    }
    uint32_t o = 0;
    while (*p && o + 1u < cap) out[o++] = (char)names_u8(&p);
    out[o] = 0;
    return o;
}

#endif
