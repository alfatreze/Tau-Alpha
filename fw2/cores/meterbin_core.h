/* fw2/cores/meterbin_core.h -- the meter bin, v2 (docs/features/meters/EXTERNAL_METERS_SPEC.md sections 2, 3, 7; plan P2.1) and the v1 bundle mapped to the same rows. Pure and host-tested (sim/test_p2_meterbin.py against the
 * independent Python reference tools/meter_bin.py): nothing is allocated, every byte comes through a reader callback (the file is up to 4,456,448 bytes, so it is never held whole: the end CRC is summed in 256-byte pieces),
 * and every read is inside the file or fails. NOT linked into the firmware yet (P2.2 and later do that): no firmware behaviour and no hot RAM change.
 *
 * Layout (little endian). v2: header 32 B { 'TPKB', u16 version 2, u16 count <= 64, u32 toc_off = 32, toc_len = 256 x count, prev_off (the preview blocks), prev_len, packs_off, total = the file's size } | TOC rows 256 B in display order
 * | previews | packs (4-byte aligned) | u32 CRC-32 of bytes 32.. before it. A row: id_hash u32 (the first 8 hex digits of the id, as a number) | id[40] (32 lower-case hex digits, NUL padded) | name[24] (1..23 printable ASCII, NUL padded)
 * | version 3 x u16 | abi | tau_min | flags (bit0 visible, bit1 built-in fallback, bit2 binary only) | params_rev | nparams (<= 48) | cmds | reserved u16 | max_cycles | pack_off | pack_size (the whole TMPK, header included) |
 * pack_crc (the pack header's image CRC) | pstate_bytes | bss_bytes | preview_off | preview_len | reserved to 256 (zero). A preview block is the 64-byte palette plus the 56 x 32 run-length stream (65..1024 bytes).
 * v1: 'TPKB', u16 version 1, u16 count, the packs back to back (TAU_PACKS_FORMAT.md): each pack becomes a row built from its header and its descriptor (name, parameters, commands, cycles, preview); it has no 128-bit id, so
 * id_hash = MB_V1_HASH_BASE | the pack's meter id, the id is empty and flag bit 3 (MB_F_V1) is set. The v1 reader needs `v1_off` (256 bytes) in the info.
 *
 * A bin is refused WHOLE with one of the codes below (the Info row `MTOC ERR n`; the player then runs on Winamp Bars, or tau-packs.bin when it was tau-meters.bin that failed); the checks are made in the order of this file, the
 * first failure is reported, and `bad_row` names the row it was about (-1 for the header). A row of a sound bin may still be UNAVAILABLE (mb_avail): needs another ABI, or a newer TAU; the list greys it, nothing is refused.
 *   0 OK   1 SIZE (shorter than header + CRC, over 4,456,448, total size not the file's, a pack cut off, bytes after the last v1 pack)   2 MAGIC   3 VERSION (not 1 or 2)   4 CRC (the end CRC-32)   5 COUNT (over 64)
 *   6 LAYOUT (the regions are not header | TOC | previews | packs | CRC in order)   7 ID (not 32 lower-case hex digits then NULs)   8 HASH (id_hash is not the id's first 8 digits)   9 DUP (two rows share an id_hash)
 *   10 NAME (not 1..23 printable ASCII, NUL terminated and padded)   11 PACK (range outside the packs, not 4-aligned, no TMPK header, TOC and pack header disagree on size, ABI, CRC, pstate or bss)
 *   12 WINDOW (the pack and its pstate do not fit the 64 KiB window)   13 PREVIEW (outside the previews, over 1 KiB, not 56 x 32 pixels)   14 RESERVED (a reserved byte or flag bit is not zero)
 *   15 ORIGIN (a pack not linked for the window, E24 in the loader)   16 PARAMS (over 48 parameters)   17 READ (the reader failed: a bug or a failing card)   18 DESC (a v1 pack's descriptor is missing or damaged) */
#ifndef METERBIN_CORE_H
#define METERBIN_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define MB_MAGIC 0x424B5054u          /* "TPKB" little endian */
#define MB_HDR 32u
#define MB_TOC 256u
#define MB_MAX_METERS 64u
#define MB_MAX_BIN 4456448u
#define MB_WINDOW 0x24970000u         /* pack slot 7 (0x24900000 + 7 x 0x10000), the window P0 proved on a Pocket; the spec's text also says 0x24900000: one constant, to be settled in P2.0/P2.2 */
#define MB_WINDOW_SIZE 0x10000u
#define MB_PACK_HDR 48u
#define MB_PAL 64u
#define MB_MAX_PREVIEW 1024u
#define MB_PIXELS (56u * 32u)
#define MB_MAX_PARAMS 48u
#define MB_V1_HASH_BASE 0x56310000u
#define MB_F_VISIBLE 1u
#define MB_F_FALLBACK 2u
#define MB_F_BINARY_ONLY 4u
#define MB_F_V1 8u
enum { MB_OK = 0, MB_E_SIZE = 1, MB_E_MAGIC = 2, MB_E_VERSION = 3, MB_E_CRC = 4, MB_E_COUNT = 5, MB_E_LAYOUT = 6, MB_E_ID = 7, MB_E_HASH = 8, MB_E_DUP = 9, MB_E_NAME = 10, MB_E_PACK = 11, MB_E_WINDOW = 12,
       MB_E_PREVIEW = 13, MB_E_RESERVED = 14, MB_E_ORIGIN = 15, MB_E_PARAMS = 16, MB_E_READ = 17, MB_E_DESC = 18 };
enum { MB_U_OK = 0, MB_U_ABI = 1, MB_U_TAU = 2 };
typedef int (*mb_rd_fn)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n);        /* 1 = read, 0 = outside the file */
typedef struct { uint8_t version; uint16_t count; int16_t bad_row; uint32_t file_len, prev_off, packs_off, total; uint32_t v1_off[MB_MAX_METERS]; } mb_info_t;
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
/* ---- v2 ---- */
static inline int mb_row_v2(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t i, mb_row_t *r, int *rerr)
{
    uint8_t t[MB_TOC], ph[MB_PACK_HDR];
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
    if (!rd(ctx, pack_off, ph, MB_PACK_HDR)) { *rerr = 1; return MB_E_READ; }
    if (mb_ld32(ph) != 0x4B504D54u) return MB_E_PACK;
    if (mb_ld32(ph + 8) + MB_PACK_HDR != pack_size || mb_ld16(ph + 4) != mb_ld16(t + 74) || mb_ld32(ph + 20) != pack_crc || mb_ld32(ph + 44) != pstate || mb_ld32(ph + 36) != bss) return MB_E_PACK;
    if (!mb_window_ok(mb_ld32(ph + 8), mb_ld32(ph + 40), mb_ld32(ph + 44))) return MB_E_WINDOW;
    if (mb_ld32(ph + 12) != MB_WINDOW) return MB_E_ORIGIN;
    if (pv_off < in->prev_off || pv_off > in->packs_off || pv_len > in->packs_off - pv_off) return MB_E_PREVIEW;
    if (!mb_preview_ok(rd, ctx, pv_off, pv_len, rerr)) return *rerr ? MB_E_READ : MB_E_PREVIEW;
    r->id_hash = h; for (uint32_t k = 0; k < 32u; k++) r->id[k] = (char)t[4 + k]; r->id[32] = 0;
    for (uint32_t k = 0; k < 24u; k++) r->name[k] = (char)t[44 + k];
    for (uint32_t k = 0; k < 3u; k++) r->ver[k] = (uint16_t)mb_ld16(t + 68 + 2 * k);
    r->abi = (uint16_t)mb_ld16(t + 74); r->tau_min = (uint16_t)mb_ld16(t + 76); r->flags = (uint16_t)flags; r->params_rev = (uint16_t)mb_ld16(t + 80); r->nparams = (uint16_t)nparams; r->cmds = (uint16_t)mb_ld16(t + 84);
    r->max_cycles = mb_ld32(t + 88); r->pack_off = pack_off; r->pack_size = pack_size; r->pack_crc = pack_crc; r->pstate = pstate; r->bss = bss; r->preview_off = pv_off; r->preview_len = pv_len;
    return MB_OK;
}
/* ---- v1 ---- */
static inline int mb_row_v1(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t i, mb_row_t *r, int *rerr)
{
    uint8_t ph[MB_PACK_HDR], t[8], d[40], nm[24];
    const uint32_t off = in->v1_off[i];
    if (!rd(ctx, off, ph, MB_PACK_HDR)) { *rerr = 1; return MB_E_READ; }
    const uint32_t size = mb_ld32(ph + 8), img = off + MB_PACK_HDR;
    if (size < 48u || !rd(ctx, img + size - 8u, t, 8u)) { return size < 48u ? MB_E_DESC : (*rerr = 1, MB_E_READ); }
    const uint32_t tot = mb_ld32(t);
    if (mb_ld32(t + 4) != 0x31435344u || tot < 48u || tot > size || tot > 4096u || (tot & 3u)) return MB_E_DESC;
    const uint32_t ds = img + size - tot, end = tot - 8u;
    if (!rd(ctx, ds, d, 40u)) { *rerr = 1; return MB_E_READ; }
    if (mb_ld32(d) != 0x4353444Du || mb_ld16(d + 4) != 3u || mb_ld16(d + 6) != tot) return MB_E_DESC;
    if (d[8] > MB_MAX_PARAMS) return MB_E_PARAMS;
    const uint32_t name_o = mb_ld16(d + 20), pal_o = mb_ld16(d + 26), rle_o = mb_ld16(d + 28), rle_n = mb_ld16(d + 30);
    if (name_o >= end || pal_o < 40u || pal_o + MB_PAL > end || rle_o + rle_n > end) return MB_E_DESC;
    uint32_t n = end - name_o; if (n > sizeof nm) n = sizeof nm;
    if (!rd(ctx, ds + name_o, nm, n)) { *rerr = 1; return MB_E_READ; }
    uint32_t z = 0; while (z < n && nm[z]) z++;
    if (z >= n || z < 1u || z > 23u) return MB_E_NAME;
    for (uint32_t k = 0; k < z; k++) if (nm[k] < 0x20u || nm[k] > 0x7Eu) return MB_E_NAME;
    if (rle_o != pal_o + MB_PAL) return MB_E_PREVIEW;
    if (!mb_window_ok(size, mb_ld32(ph + 40), mb_ld32(ph + 44))) return MB_E_WINDOW;
    if (!mb_preview_ok(rd, ctx, ds + pal_o, MB_PAL + rle_n, rerr)) return *rerr ? MB_E_READ : MB_E_PREVIEW;
    r->id_hash = MB_V1_HASH_BASE | mb_ld16(ph + 6); r->id[0] = 0;
    for (uint32_t k = 0; k < 24u; k++) r->name[k] = k <= z ? (char)nm[k] : 0;
    r->ver[0] = r->ver[1] = r->ver[2] = 0; r->abi = (uint16_t)mb_ld16(ph + 4); r->tau_min = 0;
    r->flags = (uint16_t)(MB_F_VISIBLE | MB_F_V1 | (mb_ld16(ph + 6) == 0u ? MB_F_FALLBACK : 0u)); r->params_rev = 0; r->nparams = d[8]; r->cmds = (uint16_t)mb_ld16(d + 10);
    r->max_cycles = mb_ld32(d + 16); r->pack_off = off; r->pack_size = MB_PACK_HDR + size; r->pack_crc = mb_ld32(ph + 20); r->pstate = mb_ld32(ph + 44); r->bss = mb_ld32(ph + 36);
    r->preview_off = ds + pal_o; r->preview_len = MB_PAL + rle_n;
    return MB_OK;
}
/* Row i of a bin that mb_open() accepted (or, inside mb_open, the row being checked). Returns a refusal code; `*rerr` is set when the reader failed. */
static inline int mb_row(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t i, mb_row_t *r)
{
    int rerr = 0;
    if (i >= in->count) return MB_E_COUNT;
    return in->version == 2u ? mb_row_v2(rd, ctx, in, i, r, &rerr) : mb_row_v1(rd, ctx, in, i, r, &rerr);
}
static inline int mb_hash_of(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t i, uint32_t *h)
{
    uint8_t b[4];
    if (in->version == 2u) { if (!rd(ctx, MB_HDR + MB_TOC * i, b, 4u)) return 0; *h = mb_ld32(b); return 1; }
    if (!rd(ctx, in->v1_off[i] + 6u, b, 2u)) return 0;
    *h = MB_V1_HASH_BASE | mb_ld16(b); return 1;
}
static inline int mb_dups(mb_rd_fn rd, void *ctx, mb_info_t *in)
{
#if BUG != 1
    for (uint32_t i = 1; i < in->count; i++) {
        uint32_t a, b;
        if (!mb_hash_of(rd, ctx, in, i, &a)) return mb_bad(in, MB_E_READ, (int)i);
        for (uint32_t j = 0; j < i; j++) {
            if (!mb_hash_of(rd, ctx, in, j, &b)) return mb_bad(in, MB_E_READ, (int)j);
            if (a == b) return mb_bad(in, MB_E_DUP, (int)i);
        }
    }
#else
    (void)rd; (void)ctx; (void)in;
#endif
    return MB_OK;
}
static inline int mb_open_rows(mb_rd_fn rd, void *ctx, mb_info_t *in)
{
    mb_row_t r;
    for (uint32_t i = 0; i < in->count; i++) { const int e = mb_row(rd, ctx, in, i, &r); if (e) return mb_bad(in, e, (int)i); }
    return mb_dups(rd, ctx, in);
}
static inline int mb_open_v2(mb_rd_fn rd, void *ctx, uint32_t len, mb_info_t *in)
{
    uint8_t h[MB_HDR], c[256];
    if (len < MB_HDR + 4u || len > MB_MAX_BIN) return MB_E_SIZE;
    if (!rd(ctx, 0u, h, MB_HDR)) return MB_E_READ;
    const uint32_t count = mb_ld16(h + 6);
#if BUG == 3
    if (count > MB_MAX_METERS + 1u) return MB_E_COUNT;
#else
    if (count > MB_MAX_METERS) return MB_E_COUNT;
#endif
    const uint32_t toc_off = mb_ld32(h + 8), toc_len = mb_ld32(h + 12), prev_off = mb_ld32(h + 16), prev_len = mb_ld32(h + 20), packs_off = mb_ld32(h + 24), total = mb_ld32(h + 28);
    if (total != len) return MB_E_SIZE;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t o = MB_HDR; o < total - 4u; ) {
        uint32_t n = total - 4u - o; if (n > sizeof c) n = sizeof c;
        if (!rd(ctx, o, c, n)) return MB_E_READ;
        crc = mb_crc_update(crc, c, n); o += n;
    }
    if (!rd(ctx, total - 4u, c, 4u)) return MB_E_READ;
#if BUG != 2
    if (~crc != mb_ld32(c)) return MB_E_CRC;
#endif
    if (toc_off != MB_HDR || toc_len != MB_TOC * count || (uint64_t)prev_off != (uint64_t)MB_HDR + toc_len || (uint64_t)packs_off != (uint64_t)prev_off + prev_len || (packs_off & 3u) || (uint64_t)packs_off + 4u > total) return MB_E_LAYOUT;
    in->version = 2u; in->count = (uint16_t)count; in->file_len = len; in->prev_off = prev_off; in->packs_off = packs_off; in->total = total;
    return mb_open_rows(rd, ctx, in);
}
static inline int mb_open_v1(mb_rd_fn rd, void *ctx, uint32_t len, mb_info_t *in)
{
    uint8_t h[MB_PACK_HDR];
    if (len > MB_MAX_BIN) return MB_E_SIZE;
    if (!rd(ctx, 0u, h, 8u)) return MB_E_READ;
    const uint32_t count = mb_ld16(h + 6);
    if (count > MB_MAX_METERS) return MB_E_COUNT;
    uint32_t off = 8u;
    for (uint32_t i = 0; i < count; i++) {
        if ((uint64_t)off + MB_PACK_HDR > len) return mb_bad(in, MB_E_SIZE, (int)i);
        if (!rd(ctx, off, h, MB_PACK_HDR)) return mb_bad(in, MB_E_READ, (int)i);
        if (mb_ld32(h) != 0x4B504D54u) return mb_bad(in, MB_E_PACK, (int)i);
        const uint32_t size = mb_ld32(h + 8);
        if (size == 0u || (uint64_t)off + MB_PACK_HDR + size > len) return mb_bad(in, MB_E_SIZE, (int)i);
        in->v1_off[i] = off; off += MB_PACK_HDR + size;
    }
    if (off != len) return MB_E_SIZE;
    in->version = 1u; in->count = (uint16_t)count; in->file_len = len; in->prev_off = in->packs_off = 8u; in->total = len;
    return mb_open_rows(rd, ctx, in);
}
/* Reads, checks and counts a bin of `len` bytes. 0 = sound: rows are read with mb_row(). Otherwise the refusal code (table above) and in->bad_row. */
static inline int mb_open(mb_rd_fn rd, void *ctx, uint32_t len, mb_info_t *in)
{
    uint8_t h[8];
    in->version = 0u; in->count = 0u; in->bad_row = -1; in->file_len = len; in->prev_off = in->packs_off = in->total = 0u;
    if (len < 8u) return MB_E_SIZE;
    if (!rd(ctx, 0u, h, 8u)) return MB_E_READ;
    if (mb_ld32(h) != MB_MAGIC) return MB_E_MAGIC;
    if (mb_ld16(h + 4) == 2u) return mb_open_v2(rd, ctx, len, in);
    if (mb_ld16(h + 4) == 1u) return mb_open_v1(rd, ctx, len, in);
    return MB_E_VERSION;
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
/* The row whose id_hash is `hash` (the key the selection and the tunings are stored by), or -1. */
static inline int mb_find_hash(mb_rd_fn rd, void *ctx, const mb_info_t *in, uint32_t hash)
{
    for (uint32_t i = 0; i < in->count; i++) { uint32_t h; if (mb_hash_of(rd, ctx, in, i, &h) && h == hash) return (int)i; }
    return -1;
}
#endif
