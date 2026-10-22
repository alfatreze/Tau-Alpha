/* fw2/cores/meter_desc_core.h -- reads the meter descriptor a pack carries (layout and builder: tools/player2/meter_desc.py; B-812). Pure: every byte comes through a reader callback, so the firmware can read the
 * descriptor straight out of the PSRAM slot (a little at a time, never a copy of all of it) and the host test can read a file and check that no read goes outside the descriptor. md_check() validates EVERYTHING the
 * accessors will later trust (offsets, string terminators, ranges, the preview stream): a descriptor that fails it is not used and the meter does not appear. Host-tested (sim/test_p2_meter_desc.py). */
#ifndef METER_DESC_CORE_H
#define METER_DESC_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define MD_MAGIC 0x4353444Du
#define MD_TRAILER 0x31435344u
#define MD_VERSION 1u
#define MD_HDR 32u
#define MD_MAX_PARAMS 48u
#define MD_MAX_PRESETS 16u
#define MD_MAX_NVAL 12u
#define MD_MAX_STR 23u
#define MD_MAX_LEN 4096u
typedef int (*md_rd_fn)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n);        /* 1 = read, 0 = outside */
typedef struct { uint32_t total, cmp, max_cycles; uint16_t cmds, name, params, presets, pal, rle, rle_len; uint8_t np, npre; } md_hdr_t;
typedef struct { uint16_t label, min, max, step, when_val, vals; uint8_t type, hidden, nval, when_idx; } md_param_t;
typedef struct { uint16_t fixed; uint8_t a, b, t, n; } md_col_t;
static inline uint32_t md_u16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t md_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint32_t md_stride(uint32_t np) { return (2u + 2u * np + 3u) & ~3u; }
/* The whole descriptor is `len` bytes (the pack loader knows it from the trailer). Returns 1 and the header when it is sound. */
static inline int md_header(md_rd_fn rd, void *ctx, uint32_t len, md_hdr_t *h)
{
    uint8_t b[MD_HDR + 8];
    if (len < MD_HDR + 8u || len > MD_MAX_LEN || (len & 3u)) return 0;
    if (!rd(ctx, 0u, b, MD_HDR)) return 0;
    if (md_u32(b) != MD_MAGIC || md_u16(b + 4) != MD_VERSION || md_u16(b + 6) != len) return 0;
    h->total = len; h->np = b[8]; h->npre = b[9]; h->cmds = (uint16_t)md_u16(b + 10); h->cmp = md_u32(b + 12); h->max_cycles = md_u32(b + 16);
    h->name = (uint16_t)md_u16(b + 20); h->params = (uint16_t)md_u16(b + 22); h->presets = (uint16_t)md_u16(b + 24); h->pal = (uint16_t)md_u16(b + 26); h->rle = (uint16_t)md_u16(b + 28); h->rle_len = (uint16_t)md_u16(b + 30);
    uint8_t t[8];
    if (!rd(ctx, len - 8u, t, 8u) || md_u32(t) != len || md_u32(t + 4) != MD_TRAILER) return 0;
    return h->np <= MD_MAX_PARAMS && h->npre >= 1u && h->npre <= MD_MAX_PRESETS;
}
/* a NUL-terminated string at `off` (inside the descriptor, before the trailer), at most cap - 1 characters; returns its length or -1 */
static inline int md_string(md_rd_fn rd, void *ctx, const md_hdr_t *h, uint32_t off, char *out, uint32_t cap)
{
    if (off >= h->total - 8u) return -1;
    for (uint32_t i = 0; i < cap; i++) {
        uint8_t c;
        if (off + i >= h->total - 8u || !rd(ctx, off + i, &c, 1u)) return -1;
        out[i] = (char)c;
        if (!c) return (int)i;
    }
    return -1;
}
static inline int md_param(md_rd_fn rd, void *ctx, const md_hdr_t *h, uint32_t i, md_param_t *p)
{
    uint8_t b[16];
    if (i >= h->np || !rd(ctx, h->params + 16u * i, b, 16u)) return 0;
    p->label = (uint16_t)md_u16(b); p->type = b[2]; p->hidden = b[3]; p->min = (uint16_t)md_u16(b + 4); p->max = (uint16_t)md_u16(b + 6); p->step = (uint16_t)md_u16(b + 8);
    p->nval = b[10]; p->when_idx = b[11]; p->when_val = (uint16_t)md_u16(b + 12); p->vals = (uint16_t)md_u16(b + 14);
    return 1;
}
static inline int md_val_off(md_rd_fn rd, void *ctx, const md_param_t *p, uint32_t j, uint32_t *off)
{
    uint8_t b[2];
    if (j >= p->nval || !rd(ctx, p->vals + 2u * j, b, 2u)) return 0;
    *off = md_u16(b); return 1;
}
static inline int md_preset(md_rd_fn rd, void *ctx, const md_hdr_t *h, uint32_t k, uint32_t *name_off, uint16_t *vals)
{
    uint8_t b[2];
    const uint32_t at = h->presets + md_stride(h->np) * k;
    if (k >= h->npre || !rd(ctx, at, b, 2u)) return 0;
    *name_off = md_u16(b);
    for (uint32_t i = 0; i < h->np; i++) { if (!rd(ctx, at + 2u + 2u * i, b, 2u)) return 0; vals[i] = (uint16_t)md_u16(b); }
    return 1;
}
static inline int md_col(md_rd_fn rd, void *ctx, const md_hdr_t *h, uint32_t i, md_col_t *c)
{
    uint8_t b[8];
    if (i >= 8u || !rd(ctx, h->pal + 8u * i, b, 8u)) return 0;
    c->fixed = (uint16_t)md_u16(b); c->a = b[2]; c->b = b[3]; c->t = b[4]; c->n = b[5];
    return 1;
}
static inline int md_rle_byte(md_rd_fn rd, void *ctx, const md_hdr_t *h, uint32_t i, uint8_t *v) { return i < h->rle_len && rd(ctx, h->rle + i, v, 1u); }
/* Full validation. `roles` is the number of theme roles (palette entries may name a role below it). Returns 0 when sound, else a small code for Info. */
static inline int md_check(md_rd_fn rd, void *ctx, uint32_t len, uint32_t roles, md_hdr_t *hout)
{
    md_hdr_t h; char s[MD_MAX_STR + 2];
    if (!md_header(rd, ctx, len, &h)) return 1;
    const uint32_t end = len - 8u;
    if (h.params < MD_HDR || (h.params & 1u) || h.params + 16u * h.np > end) return 2;
    if (h.presets < MD_HDR || (h.presets & 1u) || h.presets + md_stride(h.np) * h.npre > end) return 2;
    if (h.pal < MD_HDR || (h.pal & 1u) || h.pal + 64u > end) return 2;
    if (h.rle < MD_HDR || h.rle + (uint32_t)h.rle_len > end) return 2;
    if (md_string(rd, ctx, &h, h.name, s, sizeof s) < 1) return 3;
    for (uint32_t i = 0; i < h.np; i++) {
        md_param_t p;
        if (!md_param(rd, ctx, &h, i, &p)) return 4;
        if (md_string(rd, ctx, &h, p.label, s, sizeof s) < 1) return 3;
        if (p.type > 3u || p.hidden > 1u || p.min > p.max || p.step == 0u) return 4;
        if (p.when_idx != 255u && p.when_idx >= h.np) return 4;
        if (p.type == 1u && (p.nval < 2u || p.nval > MD_MAX_NVAL || p.max != p.nval - 1u || p.min != 0u)) return 4;
        if (p.type != 1u && p.nval != 0u) return 4;
        if (p.nval && (p.vals < MD_HDR || (p.vals & 1u) || p.vals + 2u * p.nval > end)) return 4;
        for (uint32_t j = 0; j < p.nval; j++) { uint32_t o; if (!md_val_off(rd, ctx, &p, j, &o) || md_string(rd, ctx, &h, o, s, sizeof s) < 1) return 3; }
    }
    for (uint32_t k = 0; k < h.npre; k++) {
        uint32_t no; uint16_t v[MD_MAX_PARAMS];
        if (!md_preset(rd, ctx, &h, k, &no, v) || md_string(rd, ctx, &h, no, s, sizeof s) < 1) return 5;
        for (uint32_t i = 0; i < h.np; i++) {
            md_param_t p; if (!md_param(rd, ctx, &h, i, &p)) return 5;
#if BUG != 1
            if (v[i] < p.min || v[i] > p.max) return 5;                                    /* a preset value outside its parameter's range */
#endif
        }
    }
    for (uint32_t i = 0; i < 8u; i++) {
        md_col_t c; if (!md_col(rd, ctx, &h, i, &c)) return 6;
        if (c.a != 255u && (c.a >= roles || (c.n && (c.b >= roles || c.t > c.n)))) return 6;
    }
    uint32_t px = 0;
    for (uint32_t i = 0; i < h.rle_len; i++) { uint8_t v; if (!md_rle_byte(rd, ctx, &h, i, &v)) return 7; px += (uint32_t)(v & 31u) + 1u; }
#if BUG != 2
    if (px != 56u * 32u) return 7;                                                        /* the preview must be exactly 56 x 32 */
#else
    (void)px;
#endif
    if (hout) *hout = h;
    return 0;
}
#endif
