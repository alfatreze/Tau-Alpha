/* fw2/cores/meterbin_core.h -- the meter bin, v2 (docs/features/meters/EXTERNAL_METERS_SPEC.md sections 2, 3, 7; plan P2.1). Pure and host-tested (sim/test_p2_meterbin.py against the independent Python reference tools/meter_bin.py):
 * nothing is allocated, every byte comes through a reader callback and every read is inside the file or fails. NOT linked into the firmware yet (P2.2 and later do that): no firmware behaviour and no hot RAM change. v2 ONLY: the firmware
 * never meets a v1 bundle (tau-packs.bin ships with tau.rom); the Python tool parses v1 for inspection.
 *
 * Layout (little endian). Header 40 B { 'TPKB', u16 version 2, u16 count <= 64, u32 toc_off = 40, toc_len = 256 x count, pv_off (the preview blocks), pv_len, packs_off, total = the file's size, toc_crc (CRC-32 of bytes 40..packs_off: the TOC and
 * the previews), 0 } | TOC rows 256 B in display order | previews | packs (4-byte aligned) | u32 whole-file CRC-32 of bytes 40.. (TOOLS only: the firmware never reads it, it is up to 4.25 MiB). A row: id_hash u32 (the first 8 hex digits of the
 * id as a number) | id[40] (32 lower-case hex digits, NUL padded) | name[24] (1..23 printable ASCII, NUL padded) | version 3 x u16 | abi | tau_min | flags (bit0 visible, bit1 built-in fallback, bit2 binary only) | params_rev | nparams (<= 48)
 * | cmds | reserved u16 | max_cycles | pack_off | pack_size (the whole TMPK, header included) | pack_crc (the pack header's image CRC) | pstate_bytes | bss_bytes | preview_off | preview_len | reserved to 256 (zero). A preview block is the
 * 64-byte palette plus the 56 x 32 run-length stream (65..1024 bytes).
 *
 * Three levels, from cheap to dear:
 *   mb_open_toc   the boot path. Reads ONLY bytes 0..packs_off (so it works on the PSRAM copy of the TOC and previews: P2.4), in one sequential pass computes toc_crc, then validates every row from the copy: ids, names, ranges, pack bounds
 *                 against `total` (nothing in the packs is read), the TOC's own window bound, previews, and unique id_hash in O(n log n) (a sort of the already read hashes, never n^2 reads).
 *   mb_check_pack at selection: ONE pack's own header against its row (magic, size, ABI, CRC, pstate, bss as the row says, the exact window fit, the window origin); mb_check_pack_image sums the image against pack_crc (the loader does the
 *                 same while it copies the pack into the window).
 *   mb_check_file the tools' and tests' check: the whole-file CRC, then mb_open_toc, then every pack's header.
 * A bin is refused WHOLE with one of the codes below (the Info row `MTOC ERR n`; the player then runs on Winamp Bars, or tau-packs.bin when it was tau-meters.bin that failed); the first failing check is reported (in the order of this file),
 * and `bad_row` names the row it was about (-1 for the header). A row of a sound bin may still be UNAVAILABLE (mb_avail): needs another ABI, or a newer TAU; the list greys it, nothing is refused.
 *   0 OK   1 SIZE (shorter than header + CRC, over 4,456,448, total size not the file's)   2 MAGIC   3 VERSION (not 2)   4 CRC (toc_crc)   5 COUNT (over 64)
 *   6 LAYOUT (the regions are not header | TOC | previews | packs | CRC in order)   7 ID (not 32 lower-case hex digits then NULs)   8 HASH (id_hash is not the id's first 8 digits)   9 DUP (two rows share an id_hash)
 *   10 NAME (not 1..23 printable ASCII, NUL terminated and padded)   11 PACK (range outside the packs or not 4-aligned, no TMPK header, TOC and pack header disagree on size, ABI, CRC, pstate or bss)
 *   12 WINDOW (the pack and its pstate do not fit the 64 KiB window)   13 PREVIEW (outside the previews, over 1 KiB, not 56 x 32 pixels)   14 RESERVED (a reserved byte or flag bit is not zero)
 *   15 ORIGIN (a pack not linked for the window, E24 in the loader)   16 PARAMS (over 48 parameters)   17 READ (the reader failed: a bug or a failing card)   19 FILECRC (the whole-file CRC, tools only)   (18 was the v1 descriptor, gone)
 * TUNING KEYS: a tuning is keyed by the low 16 bits of id_hash (+ the low 4 bits of params_rev). mb_tune_collisions() flags every row whose low 16 bits another row shares (about 3 in 100 bins of 64 meters): the firmware refuses
 * tuning for those rows (the meter runs from its defaults) and Omega says so at compose time. */
#ifndef METERBIN_CORE_H
#define METERBIN_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define MB_MAGIC 0x424B5054u          /* "TPKB" little endian */
#define MB_HDR 40u
#define MB_TOC 256u
#define MB_MAX_METERS 64u
#define MB_MAX_BIN 4456448u
#define MB_WINDOW 0x24970000u         /* pack slot 7 (0x24900000 + 7 x 0x10000), the window P0 proved on a Pocket (the spec, after review, says the same) */
#define MB_WINDOW_SIZE 0x10000u
#define MB_PACK_HDR 48u
#define MB_PAL 64u
#define MB_MAX_PREVIEW 1024u
#define MB_PIXELS (56u * 32u)
#define MB_MAX_PARAMS 48u
#define MB_F_VISIBLE 1u
#define MB_F_FALLBACK 2u
#define MB_F_BINARY_ONLY 4u
enum { MB_OK = 0, MB_E_SIZE = 1, MB_E_MAGIC = 2, MB_E_VERSION = 3, MB_E_CRC = 4, MB_E_COUNT = 5, MB_E_LAYOUT = 6, MB_E_ID = 7, MB_E_HASH = 8, MB_E_DUP = 9, MB_E_NAME = 10, MB_E_PACK = 11, MB_E_WINDOW = 12,
       MB_E_PREVIEW = 13, MB_E_RESERVED = 14, MB_E_ORIGIN = 15, MB_E_PARAMS = 16, MB_E_READ = 17, MB_E_FILECRC = 19 };
enum { MB_U_OK = 0, MB_U_ABI = 1, MB_U_TAU = 2 };
typedef int (*mb_rd_fn)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n);        /* 1 = read, 0 = outside what the caller serves */
typedef struct { uint16_t count; int16_t bad_row; uint32_t file_len, pv_off, packs_off, total; } mb_info_t;
typedef struct {
    uint32_t id_hash, max_cycles, pack_off, pack_size, pack_crc, pstate, bss, preview_off, preview_len;
    uint16_t ver[3], abi, tau_min, flags, params_rev, nparams, cmds;
    char id[33], name[24];
} mb_row_t;
static inline uint32_t mb_ld16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t mb_ld32(const uint8_t *p) { return mb_ld16(p) | (mb_ld16(p + 2) << 16); }
static inline uint32_t mb_crc_update(uint32_t c, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return c;
}
/* CRC-32 of `n` bytes at `off`, read in 256-byte pieces; returns 0 when a read failed. */
static inline int mb_crc_range(mb_rd_fn rd, void *ctx, uint32_t off, uint32_t n, uint32_t *out)
{
    uint8_t c[256]; uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t o = 0; o < n; ) {
        uint32_t k = n - o; if (k > sizeof c) k = sizeof c;
        if (!rd(ctx, off + o, c, k)) return 0;
        crc = mb_crc_update(crc, c, k); o += k;
    }
    *out = ~crc; return 1;
}
static inline int mb_bad(mb_info_t *in, int code, int row) { in->bad_row = (int16_t)row; return code; }
/* ---- shared checks ---- */
static inline int mb_window_ok(uint32_t size, uint32_t ps_off, uint32_t ps_size)
{
#if BUG == 6
    (void)ps_off; (void)ps_size;
    return size > 0u && size <= MB_WINDOW_SIZE;
#else
    return size > 0u && size <= MB_WINDOW_SIZE && ps_off >= size && ps_off <= MB_WINDOW_SIZE && ps_size <= MB_WINDOW_SIZE - ps_off;
#endif
}
/* A preview block at `off`, `len` bytes: 65..1024 bytes, its run-length stream (after the 64-byte palette) covering exactly 56 x 32 pixels. */
static inline int mb_preview_ok(mb_rd_fn rd, void *ctx, uint32_t off, uint32_t len, int *rerr)
{
    uint8_t b[64]; uint32_t px = 0;
    if (len <= MB_PAL || len > MB_MAX_PREVIEW) return 0;
    for (uint32_t o = MB_PAL; o < len; ) {
        uint32_t n = len - o; if (n > sizeof b) n = sizeof b;
        if (!rd(ctx, off + o, b, n)) { *rerr = 1; return 0; }
        for (uint32_t i = 0; i < n; i++) px += (uint32_t)(b[i] & 31u) + 1u;
        o += n;
    }
    return px == MB_PIXELS;
}
/* 1..23 printable ASCII, then NUL, then NULs to the end of the field of `cap` bytes. */
static inline int mb_name_ok(const uint8_t *f, uint32_t cap)
{
    uint32_t n = 0;
    while (n < cap && f[n]) { if (f[n] < 0x20u || f[n] > 0x7Eu) return 0; n++; }
    if (n < 1u || n > 23u || n >= cap) return 0;
    for (uint32_t i = n; i < cap; i++) if (f[i]) return 0;
    return 1;
}
/* Heapsort of up to 64 keys: O(n log n), no recursion, no allocation. */
static inline void mb_sort64(uint64_t *a, uint32_t n)
{
    for (uint32_t i = n / 2u; i-- > 0u; ) {
        uint32_t r = i;
        for (;;) { uint32_t c = 2u * r + 1u; if (c >= n) break; if (c + 1u < n && a[c + 1u] > a[c]) c++; if (a[r] >= a[c]) break; uint64_t t = a[r]; a[r] = a[c]; a[c] = t; r = c; }
    }
    for (uint32_t e = n; e-- > 1u; ) {
        uint64_t t = a[0]; a[0] = a[e]; a[e] = t;
        uint32_t r = 0;
        for (;;) { uint32_t c = 2u * r + 1u; if (c >= e) break; if (c + 1u < e && a[c + 1u] > a[c]) c++; if (a[r] >= a[c]) break; t = a[r]; a[r] = a[c]; a[c] = t; r = c; }
    }
}
/* The first row (lowest index) whose id_hash equals an EARLIER row's, or -1: n log n on the hashes already read. */
static inline int mb_first_dup(const uint32_t *h, uint32_t n)
{
    uint64_t k[MB_MAX_METERS]; int best = -1;
    for (uint32_t i = 0; i < n; i++) k[i] = ((uint64_t)h[i] << 8) | i;
    mb_sort64(k, n);
    for (uint32_t i = 1; i < n; i++) if ((k[i] >> 8) == (k[i - 1u] >> 8)) { const int row = (int)(k[i] & 255u); if (best < 0 || row < best) best = row; }
    return best;
}
/* Tuning keys: flag[i] = 1 when another row has the same low 16 bits of id_hash as row i (the firmware starts such a meter from its defaults and does not save its tuning). */
static inline void mb_tune_collisions(const uint32_t *h, uint32_t n, uint8_t *flag)
{
    uint64_t k[MB_MAX_METERS];
    for (uint32_t i = 0; i < n; i++) { k[i] = ((uint64_t)(h[i] & 0xFFFFu) << 8) | i; flag[i] = 0u; }
#if BUG != 9
    mb_sort64(k, n);
    for (uint32_t i = 1; i < n; i++) if ((k[i] >> 8) == (k[i - 1u] >> 8)) { flag[k[i] & 255u] = 1u; flag[k[i - 1u] & 255u] = 1u; }
#endif
}
/* ---- one row, from the TOC copy alone ---- */
static inline int mb_row_check(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t i, mb_row_t *r, int *rerr)
{
    uint8_t t[MB_TOC];
    if (!rd(ctx, MB_HDR + MB_TOC * i, t, MB_TOC)) { *rerr = 1; return MB_E_READ; }
    for (uint32_t k = 0; k < 32u; k++) if (!((t[4 + k] >= '0' && t[4 + k] <= '9') || (t[4 + k] >= 'a' && t[4 + k] <= 'f'))) return MB_E_ID;
    for (uint32_t k = 32u; k < 40u; k++) if (t[4 + k]) return MB_E_ID;
    uint32_t h = 0;
    for (uint32_t k = 0; k < 8u; k++) h = (h << 4) | (uint32_t)(t[4 + k] <= '9' ? t[4 + k] - '0' : t[4 + k] - 'a' + 10);
    if (mb_ld32(t) != h) return MB_E_HASH;
    if (!mb_name_ok(t + 44, 24u)) return MB_E_NAME;
    const uint32_t flags = mb_ld16(t + 78);
    if ((flags & ~7u) || mb_ld16(t + 86)) return MB_E_RESERVED;
    for (uint32_t k = 120u; k < MB_TOC; k++) if (t[k]) return MB_E_RESERVED;
    const uint32_t nparams = mb_ld16(t + 82);
    if (nparams > MB_MAX_PARAMS) return MB_E_PARAMS;
    const uint32_t pack_off = mb_ld32(t + 92), pack_size = mb_ld32(t + 96), pack_crc = mb_ld32(t + 100), pstate = mb_ld32(t + 104), bss = mb_ld32(t + 108), pv_off = mb_ld32(t + 112), pv_len = mb_ld32(t + 116);
    const uint32_t end = in->total - 4u;
#if BUG == 5
    (void)end;
    if (pack_size < MB_PACK_HDR || (pack_off & 3u)) return MB_E_PACK;
#else
    if (pack_size < MB_PACK_HDR || (pack_off & 3u) || pack_off < in->packs_off || pack_off > end || pack_size > end - pack_off) return MB_E_PACK;
#endif
    if (pack_size - MB_PACK_HDR > MB_WINDOW_SIZE || pstate > MB_WINDOW_SIZE || pack_size - MB_PACK_HDR + pstate > MB_WINDOW_SIZE) return MB_E_WINDOW;
    if (pv_off < in->pv_off || pv_off > in->packs_off || pv_len > in->packs_off - pv_off) return MB_E_PREVIEW;
    if (!mb_preview_ok(rd, ctx, pv_off, pv_len, rerr)) return *rerr ? MB_E_READ : MB_E_PREVIEW;
    r->id_hash = h; for (uint32_t k = 0; k < 32u; k++) r->id[k] = (char)t[4 + k]; r->id[32] = 0;
    for (uint32_t k = 0; k < 24u; k++) r->name[k] = (char)t[44 + k];
    for (uint32_t k = 0; k < 3u; k++) r->ver[k] = (uint16_t)mb_ld16(t + 68 + 2 * k);
    r->abi = (uint16_t)mb_ld16(t + 74); r->tau_min = (uint16_t)mb_ld16(t + 76); r->flags = (uint16_t)flags; r->params_rev = (uint16_t)mb_ld16(t + 80); r->nparams = (uint16_t)nparams; r->cmds = (uint16_t)mb_ld16(t + 84);
    r->max_cycles = mb_ld32(t + 88); r->pack_off = pack_off; r->pack_size = pack_size; r->pack_crc = pack_crc; r->pstate = pstate; r->bss = bss; r->preview_off = pv_off; r->preview_len = pv_len;
    return MB_OK;
}
/* ---- the boot path ---- */
/* Reads the header and bytes 40..packs_off (and nothing else: `rd` may serve only those, `len` is the file's size), checks toc_crc and every row. 0 = sound: rows are read with mb_row(). */
static inline int mb_open_toc(mb_rd_fn rd, void *ctx, uint32_t len, mb_info_t *in)
{
    uint8_t h[MB_HDR];
    in->count = 0u; in->bad_row = -1; in->file_len = len; in->pv_off = in->packs_off = in->total = 0u;
    if (len < 8u) return MB_E_SIZE;
    if (!rd(ctx, 0u, h, 8u)) return MB_E_READ;
    if (mb_ld32(h) != MB_MAGIC) return MB_E_MAGIC;
    if (mb_ld16(h + 4) != 2u) return MB_E_VERSION;
    if (len < MB_HDR + 4u || len > MB_MAX_BIN) return MB_E_SIZE;
    if (!rd(ctx, 0u, h, MB_HDR)) return MB_E_READ;
    const uint32_t count = mb_ld16(h + 6);
#if BUG == 3
    if (count > MB_MAX_METERS + 1u) return MB_E_COUNT;
#else
    if (count > MB_MAX_METERS) return MB_E_COUNT;
#endif
    const uint32_t toc_off = mb_ld32(h + 8), toc_len = mb_ld32(h + 12), pv_off = mb_ld32(h + 16), pv_len = mb_ld32(h + 20), packs_off = mb_ld32(h + 24), total = mb_ld32(h + 28), toc_crc = mb_ld32(h + 32);
    if (total != len) return MB_E_SIZE;
    if (toc_off != MB_HDR || toc_len != MB_TOC * count || (uint64_t)pv_off != (uint64_t)MB_HDR + toc_len || (uint64_t)packs_off != (uint64_t)pv_off + pv_len || (packs_off & 3u) || (uint64_t)packs_off + 4u > total) return MB_E_LAYOUT;
    if (mb_ld32(h + 36)) return MB_E_RESERVED;
    uint32_t crc;
    if (!mb_crc_range(rd, ctx, MB_HDR, packs_off - MB_HDR, &crc)) return MB_E_READ;
#if BUG != 8
    if (crc != toc_crc) return MB_E_CRC;
#else
    (void)toc_crc;
#endif
    in->count = (uint16_t)count; in->pv_off = pv_off; in->packs_off = packs_off; in->total = total;
    uint32_t hashes[MB_MAX_METERS]; mb_row_t r;
    for (uint32_t i = 0; i < count; i++) {
        int rerr = 0; const int e = mb_row_check(rd, ctx, in, i, &r, &rerr);
        if (e) return mb_bad(in, e, (int)i);
        hashes[i] = r.id_hash;
    }
#if BUG == 7
    for (uint32_t i = 0; i < count; i++) { uint8_t p[MB_PACK_HDR]; int re = 0; mb_row_check(rd, ctx, in, i, &r, &re); if (!rd(ctx, r.pack_off, p, MB_PACK_HDR)) return mb_bad(in, MB_E_READ, (int)i); }
#endif
#if BUG != 1
    const int d = mb_first_dup(hashes, count);
    if (d >= 0) return mb_bad(in, MB_E_DUP, d);
#endif
    return MB_OK;
}
/* Row i of a TOC that mb_open_toc() accepted (re-validated: cheap, from the copy). */
static inline int mb_row(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t i, mb_row_t *r)
{
    int rerr = 0;
    if (i >= in->count) return MB_E_COUNT;
    return mb_row_check(rd, ctx, in, i, r, &rerr);
}
/* Every row's id_hash (n reads of 4 bytes) into h[count]; 0 when a read failed. */
static inline int mb_hashes(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t *h)
{
    uint8_t b[4];
    for (uint32_t i = 0; i < in->count; i++) { if (!rd(ctx, MB_HDR + MB_TOC * i, b, 4u)) return 0; h[i] = mb_ld32(b); }
    return 1;
}
/* Is this row usable by this firmware? fw_abi = major << 8 | minor of the host table, fw_tau = major << 8 | minor of the TAU version. */
static inline int mb_avail(const mb_row_t *r, uint32_t fw_abi, uint32_t fw_tau)
{
#if BUG == 4
    if ((uint32_t)(r->abi >> 8) != (fw_abi >> 8) || (uint32_t)(r->abi & 255u) >= (fw_abi & 255u)) return MB_U_ABI;
#else
    if ((uint32_t)(r->abi >> 8) != (fw_abi >> 8) || (uint32_t)(r->abi & 255u) > (fw_abi & 255u)) return MB_U_ABI;
#endif
    return r->tau_min > fw_tau ? MB_U_TAU : MB_U_OK;
}
/* The row whose id_hash is `hash` (the key the selection is stored by), or -1. */
static inline int mb_find_hash(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t hash)
{
    uint8_t b[4];
    for (uint32_t i = 0; i < in->count; i++) if (rd(ctx, MB_HDR + MB_TOC * i, b, 4u) && mb_ld32(b) == hash) return (int)i;
    return -1;
}
/* ---- at selection: one pack ---- */
/* The pack's own header (read from the file at row->pack_off) against its row. Returns 0 or MB_E_PACK / MB_E_WINDOW / MB_E_ORIGIN / MB_E_READ. */
static inline int mb_check_pack(mb_rd_fn rd, void *ctx, const mb_row_t *r)
{
    uint8_t ph[MB_PACK_HDR];
    if (!rd(ctx, r->pack_off, ph, MB_PACK_HDR)) return MB_E_READ;
    if (mb_ld32(ph) != 0x4B504D54u) return MB_E_PACK;
    if (mb_ld32(ph + 8) + MB_PACK_HDR != r->pack_size || mb_ld16(ph + 4) != r->abi || mb_ld32(ph + 20) != r->pack_crc || mb_ld32(ph + 44) != r->pstate || mb_ld32(ph + 36) != r->bss) return MB_E_PACK;
    if (!mb_window_ok(mb_ld32(ph + 8), mb_ld32(ph + 40), mb_ld32(ph + 44))) return MB_E_WINDOW;
    if (mb_ld32(ph + 12) != MB_WINDOW) return MB_E_ORIGIN;
    return MB_OK;
}
/* The image against pack_crc (the loader sums it while it copies it into the window; this is the same sum without the copy). 1 = matches. */
static inline int mb_check_pack_image(mb_rd_fn rd, void *ctx, const mb_row_t *r)
{
    uint32_t crc;
    return mb_crc_range(rd, ctx, r->pack_off + MB_PACK_HDR, r->pack_size - MB_PACK_HDR, &crc) && crc == r->pack_crc;
}
/* ---- tools and tests: everything ---- */
static inline int mb_check_file(mb_rd_fn rd, void *ctx, uint32_t len, mb_info_t *in)
{
    int e = mb_open_toc(rd, ctx, len, in);
    if (e) return e;
    uint8_t c[4]; uint32_t crc;
    if (!mb_crc_range(rd, ctx, MB_HDR, in->total - 4u - MB_HDR, &crc) || !rd(ctx, in->total - 4u, c, 4u)) return MB_E_READ;
#if BUG != 2
    if (crc != mb_ld32(c)) return MB_E_FILECRC;
#endif
    for (uint32_t i = 0; i < in->count; i++) {
        mb_row_t r;
        e = mb_row(rd, ctx, in, i, &r); if (!e) e = mb_check_pack(rd, ctx, &r);
        if (e) return mb_bad(in, e, (int)i);
    }
    return MB_OK;
}
#endif
