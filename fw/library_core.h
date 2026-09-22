/* Media library: portable core (spec: docs/MEDIA_LIBRARY_0.4_SPEC.md).
 *
 * Pure logic, no MMIO and no firmware globals, so the SAME code is included by fw/player.c (behind TAU_LIBRARY) and
 * compiled into tools/host/library_harness.c, which runs it under tools/rv32sim.py against indexes built by
 * tools/tau_library.py. The reference reader is tools/tau_library.py `parse`; the two must agree on every E-code.
 *
 * The caller supplies (a) a read hook (index file bytes -> a small window buffer, the firmware's tag buffer),
 * (b) the index image memory (the PSRAM window in firmware, an array in the harness).
 * State kept in fast RAM: lib_t only (about 120 B). Everything else is read from the image on demand.
 * No 64-bit arithmetic, no division, no libc: it links into the firmware without helpers. */
#ifndef TAU_LIBRARY_CORE_H
#define TAU_LIBRARY_CORE_H

#include <stdint.h>

#define LIB_HDR          128u
#define LIB_WIN          4096u          /* window size the caller must provide (= one target read)      */
#define LIB_MAX_TRACKS   16384u
#define LIB_MAX_ALBUMS   2048u
#define LIB_MAX_ARTISTS  1024u
#define LIB_MAX_LISTS    64u
#define LIB_MAX_FILE     (4u << 20)
#define LIB_MAX_STRINGS  (3u << 20)
#define LIB_MAX_PATH     200u

/* Error codes: identical to the spec (section 6) and to tau_library.LibError. */
enum {
    LIB_OK = 0, LIB_E_NOFILE = 10, LIB_E_HDR = 11, LIB_E_SIZE = 12, LIB_E_CRC = 13,
    LIB_E_COUNT = 14, LIB_E_RANGE = 15, LIB_E_PSRAM = 16, LIB_E_WALK = 17
};

enum { LIB_S_ARTISTS, LIB_S_ALBUMS, LIB_S_TRACKS, LIB_S_STRINGS, LIB_S_ALBUM_ORDER, LIB_S_TRACK_ORDER,
       LIB_S_LETTERS, LIB_S_LISTS, LIB_S_COUNT };

/* Views for the A-Z jump tables. */
enum { LIB_V_ARTISTS = 0, LIB_V_ALBUMS = 1, LIB_V_TRACKS = 2 };

/* Reads `len` bytes of the index file at `off` into `dst`; returns nonzero on success. */
typedef int (*lib_read_fn)(void *ctx, uint32_t off, uint8_t *dst, uint32_t len);

typedef struct {
    const volatile uint8_t *img;        /* whole index image (header included) */
    uint32_t size, build_id, flags;
    uint32_t off[LIB_S_COUNT], len[LIB_S_COUNT];
    uint32_t root;                      /* string offset of the path root */
    uint16_t n_artists, n_albums, n_tracks, n_lists;
} lib_t;

typedef struct { uint32_t title, file; uint16_t secs, tno, album; uint8_t fmt, flags; } lib_track_t;
typedef struct { uint32_t title, dir; uint16_t artist, year, first_track, n_tracks, art, flags; } lib_album_t;
typedef struct { uint32_t name; uint16_t first_album, n_albums; } lib_artist_t;
typedef struct { uint32_t name; uint16_t first, n; } lib_list_t;          /* a playlist: n track ids from item `first` */

/* ---------------------------------------------------------------- CRC-32 (nibble table: 64 B, ~2x bitwise speed) */
static const uint32_t lib_crc_tab[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu };

static uint32_t lib_crc_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    while (n--) {
        crc ^= *p++;
        crc = lib_crc_tab[crc & 15u] ^ (crc >> 4);
        crc = lib_crc_tab[crc & 15u] ^ (crc >> 4);
    }
    return crc;
}
#define LIB_CRC_INIT 0xFFFFFFFFu
#define LIB_CRC_DONE(c) (~(c))

/* ---------------------------------------------------------------- little-endian loads from a byte buffer */
static inline uint32_t lib_ld16(const volatile uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static inline uint32_t lib_ld32(const volatile uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---------------------------------------------------------------- load and verify */
/* Reads the whole index (via `rd`, 4 KiB windows in `win`) into `mem` (`mem_cap` bytes), checking the header CRC,
 * the file size, the body CRC, counts, section ranges and a sampled record walk. Order of checks matches
 * tau_library.parse: E11, E12, E13, E14, E15, E17. The data slot's size is not known to the core, so the size is
 * checked the way the host allows: it FAILS a read that extends past the end of the file, so the header's file size
 * is right exactly when the last byte reads and one byte past it does not.
 * `win` must be 4-byte aligned and LIB_WIN bytes. Returns LIB_OK or the E-code; `l` is only valid on LIB_OK. */
static int lib_load(lib_t *l, lib_read_fn rd, void *ctx, uint8_t *win, volatile uint8_t *mem, uint32_t mem_cap)
{
    if (!rd(ctx, 0u, win, LIB_HDR))                         /* no header's worth: empty/missing, or too short */
        return rd(ctx, 0u, win, 1u) ? LIB_E_HDR : LIB_E_NOFILE;

    uint32_t magic = lib_ld32(win), hs = lib_ld32(win + 8);
    if (magic != 0x42494C54u || lib_ld16(win + 6) > 1u || hs != LIB_HDR) return LIB_E_HDR;
    if (LIB_CRC_DONE(lib_crc_update(LIB_CRC_INIT, win, 124u)) != lib_ld32(win + 124)) return LIB_E_HDR;

    uint32_t fsz = lib_ld32(win + 20), bcrc = lib_ld32(win + 24);
    if (fsz <= mem_cap) for (uint32_t i = 0; i < LIB_HDR; i++) mem[i] = win[i];   /* before the probes reuse the window */
    if (fsz < LIB_HDR || !rd(ctx, fsz - 1u, win, 1u) || rd(ctx, fsz, win, 1u)) return LIB_E_SIZE;
    if (fsz > LIB_MAX_FILE || fsz > mem_cap) return LIB_E_COUNT;      /* cannot be held: too large */

    uint32_t crc = LIB_CRC_INIT;
    for (uint32_t off = LIB_HDR; off < fsz; ) {
        uint32_t n = fsz - off;
        if (n > LIB_WIN) n = LIB_WIN;
        if (!rd(ctx, off, win, n)) return LIB_E_SIZE;                 /* short file */
        crc = lib_crc_update(crc, win, n);
        volatile uint8_t *d = mem + off;
        if (((off | n) & 3u) == 0u) {
            volatile uint32_t *dw = (volatile uint32_t *)d;
            const uint32_t *sw = (const uint32_t *)win;
            for (uint32_t i = 0; i < (n >> 2); i++) dw[i] = sw[i];
        } else {
            for (uint32_t i = 0; i < n; i++) d[i] = win[i];
        }
        off += n;
    }
    if (LIB_CRC_DONE(crc) != bcrc) return LIB_E_CRC;

    const volatile uint8_t *h = mem;
    l->img = h;
    l->size = fsz;
    l->flags = lib_ld32(h + 12);
    l->build_id = lib_ld32(h + 16);
    l->n_artists = (uint16_t)lib_ld16(h + 32);
    l->n_albums  = (uint16_t)lib_ld16(h + 34);
    l->n_tracks  = (uint16_t)lib_ld16(h + 36);
    l->n_lists   = (uint16_t)lib_ld16(h + 38);
    l->root = lib_ld32(h + 40);
    if (l->n_tracks > LIB_MAX_TRACKS || l->n_albums > LIB_MAX_ALBUMS || l->n_artists > LIB_MAX_ARTISTS ||
        l->n_lists > LIB_MAX_LISTS) return LIB_E_COUNT;

    for (uint32_t k = 0; k < LIB_S_COUNT; k++) {
        l->off[k] = lib_ld32(h + 48 + 8u * k);
        l->len[k] = lib_ld32(h + 52 + 8u * k);
        if ((l->off[k] & 15u) || l->off[k] < LIB_HDR || l->off[k] > fsz || l->len[k] > fsz - l->off[k])
            return LIB_E_RANGE;
    }
    if (l->len[LIB_S_ARTISTS] != (uint32_t)l->n_artists * 8u ||
        l->len[LIB_S_ALBUMS] != (uint32_t)l->n_albums * 20u ||
        l->len[LIB_S_TRACKS] != (uint32_t)l->n_tracks * 16u ||
        l->len[LIB_S_ALBUM_ORDER] != (uint32_t)l->n_albums * 2u ||
        l->len[LIB_S_TRACK_ORDER] != (uint32_t)l->n_tracks * 2u ||
        l->len[LIB_S_LETTERS] != 162u) return LIB_E_RANGE;
    if (l->len[LIB_S_STRINGS] > LIB_MAX_STRINGS) return LIB_E_COUNT;

    uint32_t sl = l->len[LIB_S_STRINGS];
    if (l->root >= sl) return LIB_E_WALK;
    const volatile uint8_t *t = h + l->off[LIB_S_TRACKS], *a = h + l->off[LIB_S_ALBUMS];
    for (uint32_t i = 0; i < l->n_tracks; i += (i < 256u ? 1u : 64u)) {
        const volatile uint8_t *r = t + 16u * i;
        if (lib_ld32(r) >= sl || lib_ld32(r + 4) >= sl || lib_ld16(r + 12) >= l->n_albums) return LIB_E_WALK;
    }
    for (uint32_t i = 0; i < l->n_albums; i++) {
        const volatile uint8_t *r = a + 20u * i;
        if (lib_ld32(r) >= sl || lib_ld32(r + 4) >= sl || lib_ld16(r + 8) >= l->n_artists ||
            lib_ld16(r + 12) + lib_ld16(r + 14) > l->n_tracks) return LIB_E_WALK;
    }
    /* Playlists: 8-byte records followed by u16 track ids. Records are all checked (at most 64), items are sampled
     * like the track records. */
    uint32_t pl = l->len[LIB_S_LISTS];
    if (pl < 8u * l->n_lists || ((pl - 8u * l->n_lists) & 1u)) return LIB_E_RANGE;
    uint32_t items = (pl - 8u * l->n_lists) >> 1;
    const volatile uint8_t *lr = h + l->off[LIB_S_LISTS];
    for (uint32_t i = 0; i < l->n_lists; i++) {
        const volatile uint8_t *r = lr + 8u * i;
        if (lib_ld32(r) >= sl || lib_ld16(r + 4) + lib_ld16(r + 6) > items || lib_ld16(r + 6) > LIB_MAX_TRACKS)
            return LIB_E_WALK;
    }
    const volatile uint8_t *it = lr + 8u * l->n_lists;
    for (uint32_t i = 0; i < items; i += (i < 256u ? 1u : 64u))
        if (lib_ld16(it + 2u * i) >= l->n_tracks) return LIB_E_WALK;
    return LIB_OK;
}

/* ---------------------------------------------------------------- record and string access */
static void lib_track(const lib_t *l, uint32_t id, lib_track_t *o)
{
    const volatile uint8_t *r = l->img + l->off[LIB_S_TRACKS] + 16u * id;
    o->title = lib_ld32(r); o->file = lib_ld32(r + 4);
    o->secs = (uint16_t)lib_ld16(r + 8); o->tno = (uint16_t)lib_ld16(r + 10); o->album = (uint16_t)lib_ld16(r + 12);
    o->fmt = r[14]; o->flags = r[15];
}

static void lib_album(const lib_t *l, uint32_t id, lib_album_t *o)
{
    const volatile uint8_t *r = l->img + l->off[LIB_S_ALBUMS] + 20u * id;
    o->title = lib_ld32(r); o->dir = lib_ld32(r + 4);
    o->artist = (uint16_t)lib_ld16(r + 8); o->year = (uint16_t)lib_ld16(r + 10);
    o->first_track = (uint16_t)lib_ld16(r + 12); o->n_tracks = (uint16_t)lib_ld16(r + 14);
    o->art = (uint16_t)lib_ld16(r + 16); o->flags = (uint16_t)lib_ld16(r + 18);
}

static void lib_artist(const lib_t *l, uint32_t id, lib_artist_t *o)
{
    const volatile uint8_t *r = l->img + l->off[LIB_S_ARTISTS] + 8u * id;
    o->name = lib_ld32(r); o->first_album = (uint16_t)lib_ld16(r + 4); o->n_albums = (uint16_t)lib_ld16(r + 6);
}

static void lib_list(const lib_t *l, uint32_t id, lib_list_t *o)
{
    const volatile uint8_t *r = l->img + l->off[LIB_S_LISTS] + 8u * id;
    o->name = lib_ld32(r); o->first = (uint16_t)lib_ld16(r + 4); o->n = (uint16_t)lib_ld16(r + 6);
}

/* Track id of entry `k` of playlist `id` (k < its n). */
static uint32_t lib_list_item(const lib_t *l, uint32_t id, uint32_t k)
{
    lib_list_t p;
    lib_list(l, id, &p);
    return lib_ld16(l->img + l->off[LIB_S_LISTS] + 8u * l->n_lists + 2u * ((uint32_t)p.first + k));
}

/* Copies the string at pool offset `so` into dst (at most cap-1 bytes, always terminated). Returns its length. */
static uint32_t lib_str(const lib_t *l, uint32_t so, char *dst, uint32_t cap)
{
    const volatile uint8_t *p = l->img + l->off[LIB_S_STRINGS];
    uint32_t sl = l->len[LIB_S_STRINGS], n = 0;
    if (so >= sl || cap == 0u) { if (cap) dst[0] = 0; return 0; }
    while (so + n < sl && n + 1u < cap && p[so + n]) { dst[n] = (char)p[so + n]; n++; }
    dst[n] = 0;
    return n;
}

/* Builds root + dir + '/' + file into out (cap bytes). Returns the length, or 0 when it does not fit. */
static uint32_t lib_path(const lib_t *l, uint32_t id, char *out, uint32_t cap)
{
    lib_track_t t;
    lib_album_t a;
    lib_track(l, id, &t);
    lib_album(l, t.album, &a);
    uint32_t n = lib_str(l, l->root, out, cap);
    if (n + 1u >= cap) return 0;
    uint32_t d = lib_str(l, a.dir, out + n, cap - n);
    n += d;
    if (d) {
        if (n + 1u >= cap) return 0;
        out[n++] = '/';
    }
    uint32_t f = lib_str(l, t.file, out + n, cap - n);
    if (!f || n + f + 1u > cap) return 0;
    return n + f;
}

/* ---------------------------------------------------------------- ordering and jump tables */
static uint32_t lib_album_by_title(const lib_t *l, uint32_t pos)
{
    return lib_ld16(l->img + l->off[LIB_S_ALBUM_ORDER] + 2u * pos);
}
static uint32_t lib_track_by_title(const lib_t *l, uint32_t pos)
{
    return lib_ld16(l->img + l->off[LIB_S_TRACK_ORDER] + 2u * pos);
}

/* First position in `view` whose entry starts with `ch` (A-Z, case-insensitive) or, if none does, the next letter
 * that has entries; anything that is not a letter is the '#' group. Returns the count when past the last entry. */
static uint32_t lib_letter(const lib_t *l, uint32_t view, char ch)
{
    uint32_t c = 0;
    if (ch >= 'a' && ch <= 'z') c = (uint32_t)(ch - 'a') + 1u;
    else if (ch >= 'A' && ch <= 'Z') c = (uint32_t)(ch - 'A') + 1u;
    return lib_ld16(l->img + l->off[LIB_S_LETTERS] + 2u * (27u * view + c));
}

/* Letter jump from position `pos` of a view with `count` entries, using only the jump table (classes come from the
 * tool's sort keys, so "The Beta" is a B, exactly as it is sorted).
 * dir > 0: the first entry of the next non-empty letter group (unchanged if there is none).
 * dir < 0: the start of this group, or, if already there, the start of the previous non-empty group. */
static uint32_t lib_jump(const lib_t *l, uint32_t view, uint32_t count, uint32_t pos, int dir)
{
    const volatile uint8_t *t = l->img + l->off[LIB_S_LETTERS] + 2u * 27u * view;
    uint32_t cls = 26u;
    while (cls > 0u && lib_ld16(t + 2u * cls) > pos) cls--;         /* the group that holds pos */
    if (dir > 0) {
        for (uint32_t c = cls + 1u; c < 27u; c++) {
            uint32_t p = lib_ld16(t + 2u * c);
            if (p > pos && p < count) return p;
        }
        return pos;
    }
    uint32_t p0 = lib_ld16(t + 2u * cls);
    if (p0 < pos) return p0;
    for (uint32_t c = cls; c-- > 0; ) {
        uint32_t p = lib_ld16(t + 2u * c);
        if (p < pos) return p;
    }
    return pos;
}

/* ---------------------------------------------------------------- queue helpers (u16 track ids in `q`) */
static uint32_t lib_rng_next(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

/* The tracks of one album in play order. Returns the count. */
static uint32_t lib_queue_album(const lib_t *l, uint32_t album, volatile uint16_t *q)
{
    lib_album_t a;
    lib_album(l, album, &a);
    for (uint32_t i = 0; i < a.n_tracks; i++) q[i] = (uint16_t)(a.first_track + i);
    return a.n_tracks;
}

/* The tracks of one playlist in list order. Returns the count. */
static uint32_t lib_queue_list(const lib_t *l, uint32_t id, volatile uint16_t *q)
{
    lib_list_t p;
    lib_list(l, id, &p);
    for (uint32_t i = 0; i < p.n; i++) q[i] = (uint16_t)lib_list_item(l, id, i);
    return p.n;
}

/* Every track exactly once in a random order: Fisher-Yates (i from n-1 down, j = rand % (i+1)) with xorshift32.
 * tau_library tests reproduce this bit for bit. Returns n. */
static uint32_t lib_queue_shuffle_all(const lib_t *l, volatile uint16_t *q, uint32_t seed)
{
    uint32_t n = l->n_tracks;
    for (uint32_t i = 0; i < n; i++) q[i] = (uint16_t)i;
    for (uint32_t i = n; i > 1u; i--) {
        uint32_t j = lib_rng_next(&seed) % i;           /* 32-bit remainder: __umodsi3 is not needed on rv32im */
        uint16_t t = q[i - 1u]; q[i - 1u] = q[j]; q[j] = t;
    }
    return n;
}

#endif
