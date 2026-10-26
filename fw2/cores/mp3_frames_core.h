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
#define MP3D_WIN 1536u               /* a frame is at most 1441 bytes: the rest is look-ahead for the sync search */
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
    /* seek support */
    uint8_t  toc[100], has_toc, vbr;   /* vbr: the tag says 'Xing' (a CBR file's LAME tag says 'Info') */        /* the Xing table of contents (percent -> byte offset in 1/256 of the stream size) */
    uint32_t xing_bytes;               /* the Xing 'bytes' field: stream size without the tag (0 = absent) */
    uint32_t first_off;                /* file offset of the first frame (the Info frame when there is one) */
    uint32_t file_size;                /* set by the caller (the library knows it) */
    uint32_t skip_before;              /* decoded-stream pair index below which nothing is output (a seek's exact landing) */
    uint32_t seeks;
    uint32_t end_at;                   /* decoded-stream pair index at which the track ends early (skip silence at the end); 0 = the normal end */
    int16_t  pcm[1152 * 2];
} mp3d_t;

typedef struct { uint32_t byte_off, frame_idx, skip_before; } mp3_seek_t;

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
        d->vbr = (uint8_t)xing;
        if (flags & 1u) { d->xing_frames = mp3d_be32(f + e); e += 4u; }
        if (flags & 2u) { d->xing_bytes = mp3d_be32(f + e); e += 4u; }
        if (flags & 4u) { if (e + 100u <= avail) { for (uint32_t k = 0; k < 100u; k++) d->toc[k] = f[e + k]; d->has_toc = 1; } e += 100u; }
        if (flags & 8u) e += 4u;
        uint16_t de = 0, pa = 0;
        if (e + 24u <= avail && lame_ext_gapless(f + e, avail - e, &de, &pa)) { d->delay = de; d->pad = pa; }
        return;
    }
}

/* A sync word is real only if the next frame's header sits where this header says the frame ends (same version, layer and sample rate). A false 0xFFE... inside the audio data (common after a seek
 * into the middle of a frame) fails this and is skipped. Returns 1 real, 0 false, -1 not enough bytes to tell (and the stream is not over). */
static inline int mp3d_confirm(mp3d_t *d, const uint8_t *f, uint32_t left, int at_end)
{
    MP3FrameInfo fi;
    if (MP3GetNextFrameInfo(d->dec, &fi, (unsigned char *)f) != 0 || !fi.bitrate || !fi.samprate) return 0;
    uint32_t len = (uint32_t)(((fi.version == 0 ? 144u : 72u) * (uint32_t)fi.bitrate) / (uint32_t)fi.samprate) + ((f[2] >> 1) & 1u);
    if (left < len + 4u) return at_end ? 1 : -1;                         /* the last frame of the file has nothing after it */
    const uint8_t *n = f + len;
#if BUG == 7
    return 1;                                                            /* no confirmation: a false sync is trusted */
#else
    if (n[0] == 0xFFu && (n[1] & 0xFEu) == (f[1] & 0xFEu) && (n[2] & 0x0Cu) == (f[2] & 0x0Cu)) return 1;
    return at_end && left - len < 400u;                                  /* the last frame followed by a tag (ID3v1 is 128 bytes) or padding: nothing can follow it */
#endif
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
        if (left < 1445u && !(s->eof && !stream_avail(s))) return MS_NEED_DATA;
        { int cf = mp3d_confirm(d, d->win + d->wp, left, s->eof && !stream_avail(s)); if (cf < 0) return MS_NEED_DATA; if (!cf) { d->wp++; d->skipped_bytes++; d->resyncs++; continue; } }

        if (!d->hdr_done) { d->hdr_done = 1; d->first_off = (s->pos - stream_avail(s)) - d->wn + d->wp; mp3d_parse_info(d, d->win + d->wp, left); }
        unsigned char *in = d->win + d->wp; int bl = (int)left;
        int e = MP3Decode(d->dec, &in, &bl, d->pcm, 0);
        uint32_t used = (uint32_t)((int)left - bl);
        if (e) {
            d->errors++;
            if (e == ERR_MP3_INDATA_UNDERFLOW && !(s->eof && !stream_avail(s))) return MS_NEED_DATA;
            if (e == ERR_MP3_INDATA_UNDERFLOW) { d->state = D_DONE; return MS_EOF; }
            if (used) d->frame_idx++;                                   /* a frame that existed but did not decode (bit-reservoir warm-up after a seek, corruption) still occupies its place in the stream */
            d->wp += used ? used : 1u; d->skipped_bytes += used ? used : 1u;      /* skip it and carry on */
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
        if (count && d->skip_before && BUG != 5) {                                  /* a seek's exact landing: drop the warm-up pairs */
            uint32_t a0 = fidx * d->spf + first;
            if (a0 + count <= d->skip_before) count = 0;
            else if (a0 < d->skip_before) { uint32_t dl = d->skip_before - a0; first += dl; count -= dl; }
        }
        if (count && d->end_at) {                                       /* skip silence at the end: nothing at or past end_at is output, and the track ends there */
            uint32_t a1 = fidx * d->spf + first;
            if (a1 >= d->end_at) { d->state = D_DONE; return MS_EOF; }
            if (a1 + count > d->end_at) count = d->end_at - a1;
        }
        if (!count) continue;                                           /* the Info frame, or a frame entirely trimmed or skipped */
        out->pcm = d->pcm + first * d->ch; out->pairs = count; out->ch = d->ch;
        out->src_pos = d->trimmed ? fidx * d->spf + first - d->trim.S : fidx * d->spf + first;
        d->pairs_out += count;
        return MS_BLOCK;
    }
}

/* Where to restart to hear source pair `target` (the track's own coordinates: after the gapless trim) exactly. The restart is a few frames BEFORE the target so Helix's bit reservoir
 * and overlap state are rebuilt from real data; those frames are decoded and dropped (skip_before). The landing frame comes from the Xing TOC when there is one (a TOC entry is
 * the encoder's own position of a frame, so the frame index is exact), else from the constant frame size of a CBR file. Returns 0 when the file gives nothing to seek with. */
#if BUG == 4
#define MP3D_WARM 0u                    /* no warm-up: the bit reservoir and overlap are empty at the target */
#else
#define MP3D_WARM 5u                    /* frames decoded and dropped before the target: the bit reservoir (up to 511 bytes back) and the overlap need two, and the landing can be a frame or two late (a false sync skipped), so five */
#endif
static inline int mp3d_plan_seek(const mp3d_t *d, uint32_t target, mp3_seek_t *o)
{
    if (!d->spf || !d->ch) return 0;
    uint32_t abs = target + (d->trimmed ? d->trim.S : 0u);
    uint32_t f = abs / d->spf, fw = f > MP3D_WARM ? f - MP3D_WARM : 0u;
    uint32_t span = d->xing_bytes ? d->xing_bytes : (d->file_size > d->first_off ? d->file_size - d->first_off : 0u);
    if (!span) return 0;
    if (d->vbr && d->has_toc && d->xing_frames > 100u) {                /* true VBR: the TOC entry at or below fw. Its entries are 8-bit (span/256 apart, about 0.4% of the file), so the landing is only that accurate */
        uint32_t nfr = d->xing_frames + 1u;
        uint32_t i = (uint32_t)(((uint64_t)fw * 100u) / nfr);
        if (i > 99u) i = 99u;
        o->frame_idx = (uint32_t)(((uint64_t)i * nfr) / 100u);
        o->byte_off = d->first_off + (uint32_t)(((uint64_t)d->toc[i] * span) >> 8);
    } else if (d->xing_frames) {                                        /* CBR (a LAME 'Info' tag): constant frame size from the frame count; exact to a frame or two */
        o->frame_idx = fw;
        o->byte_off = d->first_off + (uint32_t)(((uint64_t)fw * span) / (d->xing_frames + 1u));
        { uint32_t half = span / (d->xing_frames + 1u) / 2u; o->byte_off = o->byte_off > d->first_off + half ? o->byte_off - half : d->first_off; }      /* aim half a frame EARLY: the sync search then lands on frame fw even when the estimate is a byte or two past its start (a frame boundary can sit anywhere within the padding bytes' error) */
    } else if (d->kbps && d->rate) {                                    /* no tag at all: assume CBR from the first frame's bitrate */
        o->frame_idx = fw;
        o->byte_off = d->first_off + (uint32_t)(((uint64_t)fw * d->spf * d->kbps * 125u) / d->rate);      /* exact fraction: a truncated average drifts a byte per frame */
        { uint32_t half = (d->spf * d->kbps * 125u / d->rate) / 2u; o->byte_off = o->byte_off > d->first_off + half ? o->byte_off - half : d->first_off; }      /* half a frame early, as above */
    } else return 0;
    o->skip_before = abs;
    return 1;
}

/* Skip silence at the end: the track ends `tail` pairs early. Needs the length (a tag with a frame count); returns 0 when it cannot apply. Call once the first frame has been decoded. */
static inline int mp3d_set_tail(mp3d_t *d, uint32_t tail)
{
    if (!tail || !d->spf || !d->pairs_total || tail >= d->pairs_total) return 0;
#if BUG == 6
    d->end_at = (d->trimmed ? d->trim.S : 0u) + d->pairs_total;                    /* forgets the tail */
#else
    d->end_at = (d->trimmed ? d->trim.S : 0u) + d->pairs_total - tail;
#endif
    d->pairs_total -= tail;                                                        /* the P3 expectation follows */
    return 1;
}

/* Restart decoding at a plan: seek the stream, rebuild the decoder, count frames from the landing index. */
static inline int mp3d_seek(mp3d_t *d, stream_t *s, const mp3_seek_t *p)
{
    stream_seek(s, p->byte_off);
    d->wn = d->wp = 0;
    MP3FreeDecoder(d->dec); d->dec = MP3InitDecoder();
    d->frame_idx = p->frame_idx; d->skip_before = p->skip_before; d->pairs_out = 0;
    d->state = D_RUN; d->hdr_done = 1; d->seeks++;
    return d->dec != 0;
}
#endif
