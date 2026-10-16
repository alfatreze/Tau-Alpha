/* fw2/cores/flac2_track_core.h -- the Player 2 FLAC track driver: open (metadata), play, seek, end, on top of the stream reader (stream_core.h) and the FLAC core (flac2_core.h). Pure logic, host-tested
 * (sim/test_p2_flac2_track.py) against a card model; fw2/decode/flac2.c is the thin firmware wrapper. Spec: docs/features/FLAC_DECODER_V2_REVIEW.md 5.
 *
 * The caller owns the loop: it pumps the stream (starting the reads the pump asks for), calls f2t_step, and acts on what comes back:
 *   F2T_NEED    nothing can happen until more bytes have arrived: call again later;
 *   F2T_WORK    progress without output (metadata, scanning, a seek probe): call again at once;
 *   F2T_OPENED  the metadata is read: t->op.si and the seek-table / comment captures are valid, the first frame is next;
 *   F2T_PAIRS   *np pairs in `out`, to be treated as *ev says (F2_EV_NONE: stage; F2_EV_FRAME_OK: commit everything staged; F2_EV_FRAME_BAD: discard what is staged; F2_EV_SILENCE: commit at once);
 *   F2T_END     the track is complete: every pair up to STREAMINFO's total has been delivered (a missing tail arrives first, as silence);
 *   F2T_FAIL    unrecoverable; t->why says what. */
#ifndef FLAC2_TRACK_CORE_H
#define FLAC2_TRACK_CORE_H
#include "flac2_core.h"
#include "stream_core.h"
enum { F2T_NEED = 0, F2T_WORK, F2T_OPENED, F2T_PAIRS, F2T_END, F2T_FAIL };
enum { F2T_PH_OPEN = 0, F2T_PH_PLAY, F2T_PH_SEEK, F2T_PH_END, F2T_PH_FAIL };
enum { F2T_WHY_NONE = 0, F2T_WHY_NOTFLAC, F2T_WHY_IO, F2T_WHY_GIVEUP, F2T_WHY_UNSUPPORTED, F2T_WHY_SEEKFAIL };
#define F2T_BRIDGE 1024u                                  /* the largest unit the core needs in one piece is about 540 bytes (a 4,096-zero unary run plus its remainder) */
typedef struct {
    f2_open_t op; f2_rec_t rec; f2_seek_t sk;
    uint8_t phase, why, discard_known, dropping;
    uint8_t bridge[F2T_BRIDGE];
    uint32_t audio_off;                                   /* file offset of the first frame */
    uint32_t base;                                        /* the sample the delivered stretch starts at (0, or the seek target; later if the frame holding the target was lost) */
    uint32_t target, skip_left;                           /* after a seek: the target and the pairs still to drop at the front of the frame in progress */
    uint32_t put, staged;                                 /* pairs committed since `base` (silence included); pairs staged by the frame in progress */
    uint32_t file_size, wraps, repositioned, seeks, seek_probes;
    const f2_hw_t *hw;                                    /* the LPC unit offered to the decoder (survives re-initialisations; cleared for the session on a fault) */
} f2t_t;
static inline void f2t_set_hw(f2t_t *t, const f2_hw_t *hw) { t->hw = hw; t->rec.d.hw = hw; }

static inline void f2t_init(f2t_t *t, uint8_t *seek_buf, uint32_t seek_cap, uint8_t *vc_buf, uint32_t vc_cap, int32_t *frame_buf, uint32_t frame_cap, uint32_t flags)
{
    uint8_t *z = (uint8_t *)t; for (uint32_t i = 0; i < sizeof *t; i++) z[i] = 0;
    f2_open_init(&t->op, seek_buf, seek_cap, vc_buf, vc_cap);
    f2_info_t blank = {16, 16, 44100, 2, 16, 0};
    f2_rec_init(&t->rec, &blank, frame_buf, frame_cap, flags);
}
/* Starts a track: the stream is (re)opened at byte 0. */
static inline void f2t_begin(f2t_t *t, stream_t *s, uint32_t file_size)
{
    t->phase = F2T_PH_OPEN; t->why = F2T_WHY_NONE; t->file_size = file_size; t->put = 0; t->staged = 0; t->base = 0; t->skip_left = 0; t->dropping = 0; t->discard_known = 0;
    f2_open_init(&t->op, t->op.seek_buf, t->op.seek_cap, t->op.vc_buf, t->op.vc_cap);
    stream_open(s, 0, file_size, 0);
}
static inline void f2t_fail(f2t_t *t, int why) { t->phase = F2T_PH_FAIL; t->why = (uint8_t)why; }
/* The window handed to the core: the contiguous bytes at the ring's read index; when a unit straddles the wrap point the bytes are gathered into the bridge. */
static inline uint32_t f2t_contig(const stream_t *s) { uint32_t a = stream_avail(s), c = s->cap - s->rd; return a < c ? a : c; }
static inline uint32_t f2t_gather(f2t_t *t, const stream_t *s, const uint8_t *ring)
{
    uint32_t n = stream_avail(s) < F2T_BRIDGE ? stream_avail(s) : F2T_BRIDGE, r = s->rd;
    for (uint32_t i = 0; i < n; i++) { t->bridge[i] = ring[r]; r = r + 1u == s->cap ? 0u : r + 1u; }
    t->wraps++; return n;
}

/* ---- open ---- */
static inline int f2t_open_step(f2t_t *t, stream_t *s, const uint8_t *ring)
{
    f2_open_t *o = &t->op;
    uint32_t sk = f2_open_skippable(o);
    if (sk > 8192u) {                                      /* a big body (a picture): reposition the stream instead of reading it */
        uint32_t to = o->pos + sk;
        if (to < o->pos || to > t->file_size) { f2t_fail(t, F2T_WHY_NOTFLAC); return F2T_FAIL; }
        stream_seek(s, to); t->repositioned += sk;
        if (f2_open_skipped(o, sk) != F2_OK) { f2t_fail(t, F2T_WHY_NOTFLAC); return F2T_FAIL; }
        return F2T_WORK;
    }
    uint32_t avail = stream_avail(s);
    if (!avail) { if (s->eof || s->err) { f2t_fail(t, s->err ? F2T_WHY_IO : F2T_WHY_NOTFLAC); return F2T_FAIL; } return F2T_NEED; }
    uint32_t used = 0, c = f2t_contig(s); int st = f2_open_step(o, ring + s->rd, c, &used);
    if (used) stream_skip(s, used);                        /* (the walker is byte-wise: a field straddling the wrap simply continues on the next call) */
    if (st == F2_ERR) { f2t_fail(t, F2T_WHY_NOTFLAC); return F2T_FAIL; }
    if (o->state == F2_OP_DONE) {
        t->audio_off = o->audio_off;
        if (o->si.channels > 2u || o->si.bps > 24u) { f2t_fail(t, F2T_WHY_UNSUPPORTED); return F2T_FAIL; }
        int32_t *fb = t->rec.d.buf; uint32_t fc = t->rec.d.cap, fl = t->rec.d.flags; f2_rec_init(&t->rec, &o->si, fb, fc, fl); f2_rec_set_timeline(&t->rec, 0); t->rec.d.hw = t->hw;
        t->phase = F2T_PH_PLAY; return F2T_OPENED;
    }
    return F2T_WORK;
}

/* ---- play ---- */
static inline uint32_t f2t_drop_front(int32_t *out, uint32_t np, uint32_t n)       /* drops the first n pairs of out[0..np) */
{
    if (n >= np) return 0u;
    for (uint32_t i = 0; i < 2u * (np - n); i++) out[i] = out[i + 2u * n];
    return np - n;
}
static inline int f2t_play_step(f2t_t *t, stream_t *s, const uint8_t *ring, int32_t *out, uint32_t outcap, uint32_t maxres, uint32_t *np, int *ev)
{
    f2_rec_t *r = &t->rec; uint32_t used = 0, n = 0, avail = stream_avail(s), c = f2t_contig(s); int e = F2_EV_NONE, st; *np = 0; *ev = F2_EV_NONE;
    if (r->mode == F2_RM_DEAD) { f2t_fail(t, F2T_WHY_GIVEUP); return F2T_FAIL; }
    if (s->err && s->eof && !avail) { f2t_fail(t, F2T_WHY_IO); return F2T_FAIL; }
    if (!avail && !s->eof) return F2T_NEED;
    st = f2_rec_step(r, ring + s->rd, c, out, outcap, maxres, &used, &n, &e);
    if (used) stream_skip(s, used);
    if (st == F2_NEED && used == 0u && n == 0u && e == F2_EV_NONE && c < avail) {            /* the unit straddles the wrap point: the bridge */
#if BUG != 41
        uint32_t bn = f2t_gather(t, s, ring);
        st = f2_rec_step(r, t->bridge, bn, out, outcap, maxres, &used, &n, &e);
        if (used) stream_skip(s, used);
#endif
    }
    if (e & F2_EV_GIVEUP) { f2t_fail(t, F2T_WHY_GIVEUP); return F2T_FAIL; }
    if (t->hw && !r->d.hw) t->hw = 0;                       /* the unit faulted: off for the rest of the session
    */
    if (n || e) {
        if (t->dropping) {                                 /* after a seek: drop the pairs before the target, relative to the frame's presumed start; a failed frame is recomputed against the next one */
            if (e & F2_EV_FRAME_BAD) t->discard_known = 0;
            else if (e & F2_EV_SILENCE) {                  /* lost audio on the timeline counts like any other pairs: the same distance to the target */
                uint32_t d = t->skip_left < n ? (uint32_t)t->skip_left : n;
                if (d) { n = f2t_drop_front(out, n, d); t->skip_left -= d; }
                if (t->skip_left == 0u) t->dropping = 0;
            } else {
                if (r->in_frame || (e & F2_EV_FRAME_OK)) {
                    if (!t->discard_known) {
                        uint32_t S = r->frame_start_s;
                        if (!r->frame_known) { t->skip_left = 0; t->base = t->target; }
                        else if (S <= t->target) { t->skip_left = t->target - S; t->base = t->target; }
                        else { t->skip_left = 0; t->base = S; }                                   /* landed after the target (the frame holding it was lost) */
                        t->discard_known = 1;
                    }
                    uint32_t d = t->skip_left < n ? (uint32_t)t->skip_left : n;
#if BUG == 40
                    if (d && t->skip_left > 1u) d--;                                               /* one pair too many kept at the front of a seek */
#endif
                    if (d) { n = f2t_drop_front(out, n, d); t->skip_left -= d; }
                }
                if (e & F2_EV_FRAME_OK) { t->discard_known = 0; if (t->skip_left == 0u) t->dropping = 0; }
            }
        }
        if (e & F2_EV_SILENCE) t->put += n;
#if BUG == 43
        else if (e & F2_EV_FRAME_BAD) { t->put += t->staged; t->staged = 0; }                  /* a failed frame's pairs counted as delivered */
#else
        else if (e & F2_EV_FRAME_BAD) t->staged = 0;
#endif
        else { t->staged += n; if (e & F2_EV_FRAME_OK) { t->put += t->staged; t->staged = 0; } }
        *np = n; *ev = e; return F2T_PAIRS;
    }
    if (used) return F2T_WORK;                              /* bytes were consumed (junk skipped, a header read): go round again, there may be more in the ring */
    if (st == F2_NEED) {
        uint32_t seen = (c < avail) ? 0u : used;           /* (all remaining bytes were presented to the core and it still wants more) */
        if (s->eof && (avail == 0u || (c == avail && seen == 0u))) {
            if (r->in_frame) { r->in_frame = 0; f2_dec_abort(&r->d); t->staged = 0; *ev = F2_EV_FRAME_BAD; r->owed += 0u; return F2T_PAIRS; }       /* the last frame is cut off: it is lost */
            if (s->err) { f2t_fail(t, F2T_WHY_IO); return F2T_FAIL; }                      /* the stream ended because the card failed, not because the file did: the track is cut short */
            uint32_t total = r->d.si.total, got = t->base + t->put; (void)total; (void)got;
#if BUG != 42
            if (total && got < total && r->owed == 0u) { r->owed = total - got; return F2T_WORK; }
#endif
            if (r->owed == 0u) { t->phase = F2T_PH_END; return F2T_END; }
            return F2T_WORK;                                // (the debt is paid out as silence by the next calls)
        }
        return F2T_NEED;
    }
    return F2T_WORK;
}

/* ---- seek ---- */
/* Starts a seek to `target` (a sample number). Valid once the track is open. Returns 0 if not possible now. */
static inline int f2t_seek(f2t_t *t, stream_t *s, uint32_t target)
{
    if (t->phase != F2T_PH_PLAY && t->phase != F2T_PH_END) return 0;
    const f2_info_t *si = &t->op.si; uint32_t total = si->total, lo_off = 0, lo_s = 0, ps, po, pn;
    if (!total) return 0;
    if (target >= total) target = total - 1u;
    if (t->op.have_seek && f2_seekpoint(t->op.seek_buf, t->op.seek_len / 18u, target, &ps, &po, &pn)) { lo_off = po; lo_s = ps; }
    int wv = t->rec.want_var == 0xFFu ? -1 : (int)t->rec.want_var;
    t->target = target; t->seeks++;
    int r = f2_seek_begin(&t->sk, si, target, total, t->file_size - t->audio_off, lo_off, lo_s, wv);
    t->phase = F2T_PH_SEEK;
    if (r == F2_SEEK_FAIL) { f2t_fail(t, F2T_WHY_SEEKFAIL); return 0; }
    stream_seek(s, t->audio_off + (r == F2_SEEK_CONT ? t->sk.probe : t->sk.found_off));
    return 1;
}
static inline void f2t_seek_land(f2t_t *t, stream_t *s)
{
    stream_seek(s, t->audio_off + t->sk.found_off);
    int32_t *fb = t->rec.d.buf; uint32_t fc = t->rec.d.cap, fl = t->rec.d.flags; f2_rec_init(&t->rec, &t->op.si, fb, fc, fl); t->rec.d.hw = t->hw;       /* timeline unknown: the first verified frame sets it */
    t->base = t->target; t->put = 0; t->staged = 0; t->skip_left = 1; t->discard_known = 0; t->dropping = 1; t->phase = F2T_PH_PLAY;
}
static inline int f2t_seek_step(f2t_t *t, stream_t *s, const uint8_t *ring)
{
    if (t->sk.state != F2_SEEK_CONT) { f2t_seek_land(t, s); return F2T_WORK; }
    uint32_t want = t->file_size - t->audio_off - t->sk.probe; if (want > F2_SEEK_WIN) want = F2_SEEK_WIN;
    if (s->err) { f2t_fail(t, F2T_WHY_IO); return F2T_FAIL; }
    if (stream_avail(s) < want && !s->eof) return F2T_NEED;
    uint32_t n = stream_avail(s) < want ? stream_avail(s) : (uint32_t)want, c = f2t_contig(s);
    if (c < n) n = c;                                      /* (after stream_seek the data starts at ring[0]: contiguous for a window of 4,096) */
    t->seek_probes++;
    int st = f2_seek_feed(&t->sk, &t->op.si, ring + s->rd, n);
    if (st == F2_SEEK_CONT) stream_seek(s, t->audio_off + t->sk.probe);
    else f2t_seek_land(t, s);
    return F2T_WORK;
}
static inline int f2t_step(f2t_t *t, stream_t *s, const uint8_t *ring, int32_t *out, uint32_t outcap, uint32_t maxres, uint32_t *np, int *ev)
{
    *np = 0; *ev = F2_EV_NONE;
    switch (t->phase) {
    case F2T_PH_OPEN: return f2t_open_step(t, s, ring);
    case F2T_PH_PLAY: return f2t_play_step(t, s, ring, out, outcap, maxres, np, ev);
    case F2T_PH_SEEK: return f2t_seek_step(t, s, ring);
    case F2T_PH_END: return F2T_END;
    default: return F2T_FAIL;
    }
}
#endif
