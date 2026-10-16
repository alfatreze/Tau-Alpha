/* fw2/cores/mp3_frames_core.h -- Player 2 MP3 frame driver (spec section 8.1). Pure glue over Helix, host-tested against whole-file Helix decodes (sim/test_p2_mp3.py).
 *
 * It owns the framing between the stream (a circular byte ring) and Helix (which needs one whole frame in a linear buffer): the ID3v2 tag is skipped by a stream seek,
 * sync words are searched in a small window, the Xing/Info frame is parsed for the frame count and the LAME delay/padding (gapless trim, mp3_trim.h), a corrupt frame
 * is skipped one byte at a time (as the old player did), and every call decodes AT MOST ONE frame, so the longest atomic step is one Helix frame (about 12 ms).
 * It never blocks: with too few bytes it says NEED_DATA. The output is a block of already-trimmed interleaved int16 pairs (spec 7.3). */
#ifndef MP3_FRAMES_CORE_H
#define MP3_FRAMES_CORE_H
#include <stdint.h>
#include "stream_core.h"
#include "mp3_trim.h"
#include "lame_tag.h"
#include "mp3dec.h"
#ifndef BUG
#define BUG 0
#endif

#if BUG == 3
#define MP3D_WIN 1200u
#else
#define MP3D_WIN 2304u
#endif
#define MP3D_WIN_UNUSED 0                 /* a frame is at most 1441 bytes; the rest is look-ahead for sync search and the Xing parse */
typedef enum { MS_BLOCK = 0, MS_NEED_DATA, MS_EOF, MS_ERROR } mp3d_step_t;
typedef struct { const int16_t *pcm; uint32_t pairs; uint32_t ch; uint32_t src_pos; } pcm_blk_t;   /* src_pos: source pair index of the first pair after trim */

enum { D_TAGHDR = 0, D_TAGWAIT, D_RUN, D_DONE };
typedef struct {
    uint8_t  win[MP3D_WIN];
    uint32_t wn, wp;                   /* valid bytes, read position */
    HMP3Decoder dec;
    uint8_t  state, trimmed, hdr_done, err;
    mp3_trim_cfg_t trim;
    uint32_t xing_frames, delay, pad;  /* from the Info frame (0 = absent) */
    uint32_t frame_idx;                /* decoded frames so far, the Info frame included */
    uint32_t ch, rate, spf, kbps;
    uint32_t pairs_total;              /* the P3 expectation: pairs the track holds after trim (0 = unknown) */
    uint32_t pairs_out;
    uint32_t errors, resyncs, skipped_bytes, tag_bytes;
    int16_t  pcm[1152 * 2];
} mp3d_t;

static inline int mp3d_open(mp3d_t *d, HMP3Decoder dec)
{
    uint8_t *p = (uint8_t *)d; for (uint32_t i = 0; i < sizeof *d; i++) p[i] = 0;
    d->dec = dec; d->state = D_TAGHDR;
    return dec != 0;
}

static inline uint32_t mp3d_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* Top the window up from the stream (compacting first). */
static inline void mp3d_refill(mp3d_t *d, stream_t *s, const uint8_t *ring)
{
    if (d->wp) { for (uint32_t i = 0; i + d->wp < d->wn; i++) d->win[i] = d->win[d->wp + i]; d->wn -= d->wp; d->wp = 0; }
    uint32_t room = MP3D_WIN - d->wn, n = stream_avail(s);
    if (n > room) n = room;
    if (n) { stream_take(s, ring, d->win + d->wn, n); d->wn += n; }
}

/* The Xing/Info frame: frame count and the LAME numbers. `f` points at the frame's header. */
static inline void mp3d_parse_info(mp3d_t *d, const uint8_t *f, uint32_t avail)
{
    for (uint32_t i = 0; i + 16u < avail && i < 64u; i++) {
        int xing = f[i] == 'X' && f[i + 1] == 'i' && f[i + 2] == 'n' && f[i + 3] == 'g';
        int info = f[i] == 'I' && f[i + 1] == 'n' && f[i + 2] == 'f' && f[i + 3] == 'o';
        if (!xing && !info) continue;
        uint32_t flags = mp3d_be32(f + i + 4), e = i + 8u;
        if (flags & 1u) { d->xing_frames = mp3d_be32(f + e); e += 4u; }
        if (flags & 2u) e += 4u;
        if (flags & 4u) e += 100u;
        if (flags & 8u) e += 4u;
        uint16_t de = 0, pa = 0;
        if (e + 24u <= avail && lame_ext_gapless(f + e, avail - e, &de, &pa)) { d->delay = de; d->pad = pa; }
        return;
    }
}

/* One step. `ring` is the stream's byte buffer (uncached on the device). */
static inline mp3d_step_t mp3d_step(mp3d_t *d, stream_t *s, const uint8_t *ring, pcm_blk_t *out)
{
    if (d->state == D_DONE) return MS_EOF;
    if (d->err) return MS_ERROR;
    mp3d_refill(d, s, ring);

    if (d->state == D_TAGHDR) {                                        /* the first 10 bytes decide whether a tag must be skipped */
        if (d->wn < 10u) return s->eof ? (d->state = D_RUN, MS_NEED_DATA) : MS_NEED_DATA;
#if BUG == 1
        if (0) {
#else
        if (d->win[0] == 'I' && d->win[1] == 'D' && d->win[2] == '3') {
#endif
            uint32_t sz = ((uint32_t)(d->win[6] & 0x7Fu) << 21) | ((uint32_t)(d->win[7] & 0x7Fu) << 14) | ((uint32_t)(d->win[8] & 0x7Fu) << 7) | (d->win[9] & 0x7Fu);
            uint32_t total = 10u + sz + ((d->win[5] & 0x10u) ? 10u : 0u);
            d->tag_bytes = total; d->wn = d->wp = 0;
            stream_seek(s, total);                                      /* a seek, not a read-through: a cover makes a tag of hundreds of KB */
            d->state = D_TAGWAIT; return MS_NEED_DATA;
        }
        d->state = D_RUN;
    }
    if (d->state == D_TAGWAIT) { d->state = D_RUN; }

    for (;;) {
        uint32_t left = d->wn - d->wp;
        if (left < 4u) { if (s->eof && !stream_avail(s)) { d->state = D_DONE; return MS_EOF; } return MS_NEED_DATA; }
        int off = MP3FindSyncWord(d->win + d->wp, (int)left);
        if (off < 0) {                                                  /* no sync in the window: drop all but the last 3 bytes (a sync word may straddle) */
            uint32_t drop = left - 3u; d->wp += drop; d->skipped_bytes += drop; d->resyncs++;
            if (s->eof && !stream_avail(s)) { d->state = D_DONE; return MS_EOF; }
            return MS_NEED_DATA;
        }
        if (off) { d->wp += (uint32_t)off; d->skipped_bytes += (uint32_t)off; d->resyncs++; left -= (uint32_t)off; }
        /* a frame needs up to 1441 bytes: unless the stream is over, wait until the window holds that much */
        if (left < 1441u && !(s->eof && !stream_avail(s))) return MS_NEED_DATA;

        if (!d->hdr_done) { d->hdr_done = 1; mp3d_parse_info(d, d->win + d->wp, left); }
        unsigned char *in = d->win + d->wp; int bl = (int)left;
        int e = MP3Decode(d->dec, &in, &bl, d->pcm, 0);
        uint32_t used = (uint32_t)((int)left - bl);
        if (e) {
            d->errors++;
            if (e == ERR_MP3_INDATA_UNDERFLOW && !(s->eof && !stream_avail(s))) return MS_NEED_DATA;
            if (e == ERR_MP3_INDATA_UNDERFLOW) { d->state = D_DONE; return MS_EOF; }
            d->wp += used ? used : 1u; d->skipped_bytes += used ? used : 1u;      /* a bad frame (or bit-reservoir warm-up): skip it and carry on */
            continue;
        }
        d->wp += used;
        MP3FrameInfo fi; MP3GetLastFrameInfo(d->dec, &fi);
        if (!d->ch) {
            d->ch = (uint32_t)fi.nChans; d->rate = (uint32_t)fi.samprate; d->kbps = (uint32_t)fi.bitrate / 1000u;
            d->spf = (uint32_t)fi.outputSamps / (uint32_t)fi.nChans;
            d->trimmed = (uint8_t)mp3_trim_cfg(&d->trim, d->xing_frames, d->spf, d->delay, d->pad);
#if BUG == 2
            d->trimmed = 0;
#endif
            d->pairs_total = d->trimmed ? d->trim.P : (d->xing_frames ? d->xing_frames * d->spf : 0u);
        }
        uint32_t fidx = d->frame_idx++, first = 0, count = d->spf;
        if (d->trimmed) {
            mp3_trim_range(&d->trim, fidx, &first, &count);
            if ((fidx * d->spf) >= d->trim.S + d->trim.P) { d->state = D_DONE; return MS_EOF; }      /* the padding frames are never decoded to the end */
        }
        if (!count) continue;                                           /* the Info frame, or a frame entirely trimmed */
        out->pcm = d->pcm + first * d->ch; out->pairs = count; out->ch = d->ch;
        out->src_pos = d->trimmed ? fidx * d->spf + first - d->trim.S : fidx * d->spf + first;
        d->pairs_out += count;
        return MS_BLOCK;
    }
}
#endif
