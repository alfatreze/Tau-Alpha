/* fw2/cores/mset_file_core.h -- the meter-settings save file `tau-meter-settings.bin` v1 (docs/features/meters/METER_SAVE_FILE_SPEC.md section 4). PURE: it works through a word-stream accessor (seek / read / write of the
 * save RAM, little-endian file order, every access steps the index), so the same code runs on the Pocket (MMIO 0x1F0/0x1F4), against a RAM model on the host (sim/p2_mset_file_harness.c) and is checked byte for byte against
 * the Python reference (tools/meter_settings.py) by sim/test_p2_mset_file.py.
 *
 * The file is two generations of 4,096 bytes: bank A (words 0..1023) and bank B (words 1024..2047). A generation is a 20-byte header (magic "TMSF", version 1, count, seq, used, CRC-32) and `used` bytes of records
 * (id_hash u32, params_rev u16, preset u8, n u8, n x (index u8, value u16)); order in the file is recency, oldest first. A write NEVER touches the valid generation: it builds the new one in the other bank, records first,
 * zero fill, header LAST (the magic word last of all), so at every instant at least one complete generation exists (a Quit in the middle costs the newest edit, never the file).
 * STATELESS on purpose: every call scans the headers again (about 2 x 1,000 word reads, ~1 ms) so the module holds no RAM at all (hot RAM is at its floor). Cold code. */
#ifndef P2_MSET_FILE_CORE_H
#define P2_MSET_FILE_CORE_H
#include <stdint.h>
#ifndef MF_FN
#define MF_FN static inline
#endif
#ifndef BUG
#define BUG 0                /* the host test builds mutants (sim/test_p2_mset_file.py); the firmware never defines it */
#endif
#define MF_MAGIC       0x46534D54u        /* bytes 54 4D 53 46 */
#define MF_VERSION     1u
#define MF_BANK_WORDS  1024u
#define MF_HDR_BYTES   20u
#define MF_MAX_USED    4076u              /* 4096 - 20 */
#define MF_MAXN        64u                /* parameter indexes are below 64 */
#define MF_REC_HDR     8u
typedef struct { void (*seek)(uint32_t w); uint32_t (*rd)(void); void (*wr)(uint32_t v); } mf_io_t;
typedef struct { uint8_t valid, newer; uint16_t count, used; uint32_t seq; } mf_gen_t;   /* newer: right magic, a version above 1 (never overwritten) */
typedef struct { mf_gen_t g[2]; int active; int locked; } mf_scan_t;                      /* active: 0, 1 or -1 (no valid generation); locked: a newer version is present, write nothing */
typedef struct { uint32_t id_hash; uint16_t rev; uint8_t preset, n; uint8_t idx[MF_MAXN]; uint16_t val[MF_MAXN]; } mf_rec_t;

/* ---- byte streams over the word stream ---- */
typedef struct { const mf_io_t *io; uint32_t w, cur; uint32_t nb; uint32_t crc; } mf_rd_t;
MF_FN uint32_t mf_crc_byte(uint32_t c, uint32_t b) { c ^= b & 0xFFu; for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); return c; }
MF_FN void mf_rd_open(mf_rd_t *r, const mf_io_t *io, uint32_t byte_pos, uint32_t bank_base_words)
{
    r->io = io; r->w = bank_base_words + (byte_pos >> 2); r->nb = 0; r->cur = 0; r->crc = 0xFFFFFFFFu;
    if (byte_pos & 3u) { io->seek(r->w); r->cur = io->rd() >> (8u * (byte_pos & 3u)); r->nb = 4u - (byte_pos & 3u); r->w++; }
}
MF_FN uint32_t mf_rd_byte(mf_rd_t *r)
{
    if (!r->nb) { r->io->seek(r->w); r->cur = r->io->rd(); r->w++; r->nb = 4u; }
    const uint32_t b = r->cur & 0xFFu; r->cur >>= 8; r->nb--;
    return b;
}
MF_FN uint32_t mf_rd_byte_crc(mf_rd_t *r) { const uint32_t b = mf_rd_byte(r); r->crc = mf_crc_byte(r->crc, b); return b; }
MF_FN void mf_rd_skip(mf_rd_t *r, uint32_t n) { while (n--) (void)mf_rd_byte(r); }
typedef struct { const mf_io_t *io; uint32_t w, cur, nb, crc; } mf_wr_t;
MF_FN void mf_wr_open(mf_wr_t *x, const mf_io_t *io, uint32_t bank_base_words, uint32_t byte_pos) { x->io = io; x->w = bank_base_words + (byte_pos >> 2); x->cur = 0; x->nb = 0; x->crc = 0xFFFFFFFFu; }   /* byte_pos is word aligned */
MF_FN void mf_wr_byte(mf_wr_t *x, uint32_t b, int crc)
{
    if (crc) x->crc = mf_crc_byte(x->crc, b);
    x->cur |= (b & 0xFFu) << (8u * x->nb); x->nb++;
    if (x->nb == 4u) { x->io->seek(x->w); x->io->wr(x->cur); x->w++; x->cur = 0; x->nb = 0; }
}
MF_FN void mf_wr_u8(mf_wr_t *x, uint32_t v) { mf_wr_byte(x, v, 1); }
MF_FN void mf_wr_u16(mf_wr_t *x, uint32_t v) { mf_wr_byte(x, v, 1); mf_wr_byte(x, v >> 8, 1); }
MF_FN void mf_wr_u32(mf_wr_t *x, uint32_t v) { mf_wr_u16(x, v); mf_wr_u16(x, v >> 16); }

/* ---- reading a generation ---- */
/* Validates one bank: magic, version 1, used <= 4076, count records that fill `used` exactly, CRC-32 of header bytes 0..15 then the records. */
MF_FN void mf_check(const mf_io_t *io, uint32_t bank, mf_gen_t *g)
{
    const uint32_t base = bank * MF_BANK_WORDS;
    g->valid = 0; g->newer = 0; g->count = 0; g->used = 0; g->seq = 0;
    uint32_t h[5]; io->seek(base); for (uint32_t i = 0; i < 5u; i++) h[i] = io->rd();
    if (h[0] != MF_MAGIC) return;
    const uint32_t ver = h[1] & 0xFFFFu, count = h[1] >> 16, seq = h[2], used = h[3] & 0xFFFFu, rsv = h[3] >> 16;
    if (ver > MF_VERSION) { g->newer = 1; return; }
    if (ver != MF_VERSION || rsv || used > MF_MAX_USED) return;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < 4u; i++) for (uint32_t k = 0; k < 4u; k++) crc = mf_crc_byte(crc, h[i] >> (8u * k));
    mf_rd_t r; mf_rd_open(&r, io, MF_HDR_BYTES, base); r.crc = crc;
    uint32_t pos = 0, parsed = 0;
    while (parsed < count) {
        if (pos + MF_REC_HDR > used) return;
        uint32_t n = 0;
        for (uint32_t k = 0; k < MF_REC_HDR; k++) { const uint32_t b = mf_rd_byte_crc(&r); if (k == 7u) n = b; }
        if (n > MF_MAXN || pos + MF_REC_HDR + 3u * n > used) return;
        for (uint32_t k = 0; k < 3u * n; k++) (void)mf_rd_byte_crc(&r);
        pos += MF_REC_HDR + 3u * n; parsed++;
    }
    if (pos != used) return;
    if (~r.crc != h[4]) return;
    g->valid = 1; g->count = (uint16_t)count; g->used = (uint16_t)used; g->seq = seq;
}
MF_FN void mf_scan(const mf_io_t *io, mf_scan_t *s)
{
    mf_check(io, 0, &s->g[0]); mf_check(io, 1, &s->g[1]);
    s->locked = s->g[0].newer || s->g[1].newer;
    if (s->g[0].valid && s->g[1].valid) s->active = ((int32_t)(s->g[1].seq - s->g[0].seq) > 0) ? 1 : 0;
    else s->active = s->g[0].valid ? 0 : (s->g[1].valid ? 1 : -1);
}
/* The record of id_hash in the active generation: 1 = found (out filled, *rev_ok = whether its params_rev equals `rev`), 0 = none. A record of another revision is reported with rev_ok 0 and is not used. */
MF_FN int mf_find(const mf_io_t *io, const mf_scan_t *s, uint32_t id_hash, uint32_t rev, mf_rec_t *out, int *rev_ok)
{
    if (s->active < 0) return 0;
    const uint32_t base = (uint32_t)s->active * MF_BANK_WORDS;
    mf_rd_t r; mf_rd_open(&r, io, MF_HDR_BYTES, base);
    int found = 0;
    for (uint32_t i = 0; i < s->g[s->active].count; i++) {
        uint32_t id = 0, rv = 0, pr, n;
        for (uint32_t k = 0; k < 4u; k++) id |= mf_rd_byte(&r) << (8u * k);
        rv = mf_rd_byte(&r); rv |= mf_rd_byte(&r) << 8; pr = mf_rd_byte(&r); n = mf_rd_byte(&r);
        if (id == id_hash && n <= MF_MAXN) {
            out->id_hash = id; out->rev = (uint16_t)rv; out->preset = (uint8_t)pr; out->n = (uint8_t)n;
            for (uint32_t k = 0; k < n; k++) { out->idx[k] = (uint8_t)mf_rd_byte(&r); const uint32_t lo = mf_rd_byte(&r); out->val[k] = (uint16_t)(lo | mf_rd_byte(&r) << 8); }
            found = 1; *rev_ok = (rv == (rev & 0xFFFFu));
        } else mf_rd_skip(&r, 3u * n);
    }
    return found;
}
/* ---- writing ----
 * Plan (two streams over the active generation): which records survive (all but this meter's old one, the OLDEST dropped while the new generation would exceed 4,076 bytes). */
MF_FN void mf_plan(const mf_io_t *io, const mf_scan_t *s, uint32_t id_hash, uint32_t newsize, uint32_t *keep_count, uint32_t *keep_used, uint32_t *excess)
{
    uint32_t total = 0, cnt = 0;
    *keep_count = 0; *keep_used = 0; *excess = 0;
    if (s->active >= 0) for (int pass = 0; pass < 2; pass++) {
        mf_rd_t r; mf_rd_open(&r, io, MF_HDR_BYTES, (uint32_t)s->active * MF_BANK_WORDS);
        uint32_t dropped = 0;
        for (uint32_t i = 0; i < s->g[s->active].count; i++) {
            uint32_t id = 0, n;
            for (uint32_t k = 0; k < 4u; k++) id |= mf_rd_byte(&r) << (8u * k);
            (void)mf_rd_byte(&r); (void)mf_rd_byte(&r); (void)mf_rd_byte(&r); n = mf_rd_byte(&r);
            mf_rd_skip(&r, 3u * n);
            const uint32_t sz = MF_REC_HDR + 3u * n;
            if (id == id_hash) continue;                                               /* replaced by the new record */
            if (pass == 0) { total += sz; continue; }
            if (dropped < *excess) { dropped += sz; continue; }                           /* the oldest go first */
            cnt++; *keep_used += sz;
        }
        if (pass == 0) *excess = (total + newsize > MF_MAX_USED) ? total + newsize - MF_MAX_USED : 0;
    }
    *keep_count = cnt;
}
/* Stores the record of id_hash (n = 0 removes it). 1 = written, 0 = refused (a newer file version is present: nothing is touched). The generation in the other bank is built first and its header last. */
MF_FN int mf_put(const mf_io_t *io, uint32_t id_hash, uint32_t rev, uint32_t preset, uint32_t n, const uint8_t *idx, const uint16_t *val)
{
    mf_scan_t s; mf_scan(io, &s);
    if (s.locked) return 0;
    if (n > MF_MAXN) n = MF_MAXN;
    const uint32_t dst = (s.active < 0) ? 0u : (uint32_t)((BUG == 3) ? s.active : 1 - s.active), base = dst * MF_BANK_WORDS;
    const uint32_t seq = (s.active < 0) ? 1u : s.g[s.active].seq + 1u;
    const uint32_t newsize = n ? MF_REC_HDR + 3u * n : 0u;
    uint32_t kc, ku, excess; mf_plan(io, &s, id_hash, newsize, &kc, &ku, &excess);
    if (BUG == 4) excess = 0;
    const uint32_t count = kc + (n ? 1u : 0u), used = ku + newsize;
    /* header fields first into the CRC (not yet into the file) */
    mf_wr_t x; mf_wr_open(&x, io, base, MF_HDR_BYTES);
    uint32_t c = 0xFFFFFFFFu; const uint32_t hw[4] = { MF_MAGIC, MF_VERSION | count << 16, seq, used };
    for (uint32_t i = 0; i < 4u; i++) for (uint32_t k = 0; k < 4u; k++) c = mf_crc_byte(c, hw[i] >> (8u * k));
    x.crc = c;
    if (s.active >= 0) {
        mf_rd_t r; mf_rd_open(&r, io, MF_HDR_BYTES, (uint32_t)s.active * MF_BANK_WORDS);
        uint32_t dropped = 0;
        for (uint32_t i = 0; i < s.g[s.active].count; i++) {
            uint8_t b[MF_REC_HDR]; uint32_t id = 0;
            for (uint32_t k = 0; k < MF_REC_HDR; k++) b[k] = (uint8_t)mf_rd_byte(&r);
            for (uint32_t k = 0; k < 4u; k++) id |= (uint32_t)b[k] << (8u * k);
            const uint32_t m = b[7], sz = MF_REC_HDR + 3u * m;
            const int same = (id == id_hash);
            const int drop = !same && dropped < excess;
            if (same) { mf_rd_skip(&r, 3u * m); continue; }
            if (drop) { dropped += sz; mf_rd_skip(&r, 3u * m); continue; }
            for (uint32_t k = 0; k < MF_REC_HDR; k++) mf_wr_u8(&x, b[k]);
            for (uint32_t k = 0; k < 3u * m; k++) mf_wr_u8(&x, mf_rd_byte(&r));
        }
    }
    if (n) {
        mf_wr_u32(&x, id_hash); mf_wr_u16(&x, rev); mf_wr_u8(&x, preset); mf_wr_u8(&x, n);
        for (uint32_t k = 0; k < n; k++) { mf_wr_u8(&x, idx[k]); mf_wr_u16(&x, val[k]); }
    }
    while (x.nb) mf_wr_byte(&x, 0, 0);                                                   /* finish the word, then zero to the end of the bank */
    while (BUG != 1 && x.w < base + MF_BANK_WORDS) { io->seek(x.w); io->wr(0); x.w++; }
    io->seek(base + 1u); io->wr(hw[1]); io->wr(hw[2]); io->wr(hw[3]); io->wr(BUG == 2 ? x.crc : ~x.crc);   /* the header last ... */
    io->seek(base); io->wr(MF_MAGIC);                                                   /* ... the magic word last of all */
    return 1;
}
#endif
