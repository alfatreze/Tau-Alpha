/* fw/track_details.h -- the text of the track details page (B-727, docs/features/TRACK_DETAILS_SPEC.md). Pure code, no firmware dependencies: host-tested by sim/test_track_details_fw.py
 * against an independent Python formatter; fw/library.inc only calls it. All text lives in ONE list (TDET_STRINGS): the firmware stores it as a single cold-data blob and the test
 * as an array, so the ids can never drift. The includer defines TDET_S(id) (a const char * for a string id) before including this file. */
#ifndef TAU_TRACK_DETAILS_H
#define TAU_TRACK_DETAILS_H
#include <stdint.h>

#define TDET_STRINGS(X) \
    X(L_TITLE, "TITLE") \
    X(L_ARTIST, "ARTIST") \
    X(L_ALBUM, "ALBUM") \
    X(L_TRACKNO, "TRACK NO") \
    X(L_YEAR, "YEAR") \
    X(L_LENGTH, "LENGTH") \
    X(L_FILE, "FILE") \
    X(L_FOLDER, "FOLDER") \
    X(L_FORMAT, "FORMAT") \
    X(L_AUDIO, "AUDIO") \
    X(L_BITRATE, "BITRATE") \
    X(L_SIZE, "FILE SIZE") \
    X(L_ENCODER, "ENCODER") \
    X(L_GAPLESS, "GAPLESS") \
    X(L_RGT, "REPLAYGAIN TRK") \
    X(L_RGA, "REPLAYGAIN ALB") \
    X(L_COVER, "COVER") \
    X(L_LIVE_DEC, "DECODER NOW") \
    X(L_LIVE_RG, "RG IN FORCE") \
    X(L_LIVE_LOAD, "LOAD MS") \
    X(TRACK_DETAILS_PLAYING, "TRACK DETAILS  PLAYING") \
    X(TRACK_DETAILS, "TRACK DETAILS") \
    X(SCROLL_HINT, "UP DOWN SCROLL   B BACK") \
    X(DISC, "DISC ") \
    X(SP2, "  ") \
    X(TRACK, "TRACK ") \
    X(FLAC, "FLAC") \
    X(MP3, "MP3") \
    X(BIT, " BIT") \
    X(VBR, "  VBR") \
    X(CBR, "  CBR") \
    X(KHZ, " KHZ  ") \
    X(MONO, "MONO") \
    X(STEREO, "STEREO") \
    X(KBPS, " KBPS") \
    X(KBPS_AVG, " KBPS AVG") \
    X(MB, " MB") \
    X(EXACT, "EXACT, NO TRIM") \
    X(DELAY, "DELAY ") \
    X(PAD, "  PAD ") \
    X(SIDECAR, "SIDECAR 128 PX") \
    X(PLUS, " + ") \
    X(EMBEDDED, "EMBEDDED ") \
    X(KB, " KB") \
    X(NONE, "NONE") \
    X(HZ, " HZ  ") \
    X(OFF, "OFF") \
    X(TRACKW, "TRACK") \
    X(ALBUM, "ALBUM") \
    X(CAP, "  CAP ") \
    X(PCT, "%") \
    X(DB, " DB") \
    X(PEAK, "  PEAK ") \
    X(SLASH, " / ") \
    X(DASH, "-") \
    X(PLUSS, "+") \
    X(HINT_TRACKS, "A PLAY  Y DETAILS  B BACK")

enum {
#define X(n, s) TDS_##n,
TDET_STRINGS(X)
#undef X
    TDS_COUNT
};
#define TDET_BLOB_INIT(X) TDET_STRINGS(X)

#ifndef TDET_FN
#define TDET_FN static inline
#endif

/* detail record flags (tools/tau_details.py): 0 VBR, 1 LAME, 2 sidecar, 3 embedded cover, 4..7 ReplayGain track gain, album gain, track peak, album peak */
enum { TD_VBR = 1, TD_LAME = 2, TD_SIDECAR = 4, TD_COVER = 8, TD_RGTG = 16, TD_RGAG = 32, TD_RGTP = 64, TD_RGAP = 128 };

TDET_FN char *tdet_cat(char *q, const char *s) { while (*s) *q++ = *s++; return q; }
TDET_FN char *tdet_dec(char *q, uint32_t v)
{
    char t[10]; uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *q++ = t[--n];
    return q;
}
/* v / scale with `digits` decimals (scale = 10^digits) */
TDET_FN char *tdet_fix(char *q, uint32_t v, uint32_t scale, uint32_t digits)
{
    q = tdet_dec(q, v / scale);
    if (digits) { *q++ = '.'; uint32_t f = v % scale, d = scale / 10u; while (d) { *q++ = (char)('0' + (f / d) % 10u); d /= 10u; } }
    return q;
}
/* "-4.31 DB  PEAK 0.9707": gain in centi-dB, the peak in Q15 (0 = no peak tag) shown with four decimals */
TDET_FN char *tdet_gain(char *q, int32_t cdb, uint32_t peak)
{
    q = tdet_cat(q, cdb < 0 ? TDET_S(TDS_DASH) : TDET_S(TDS_PLUSS));
    q = tdet_fix(q, (uint32_t)(cdb < 0 ? -cdb : cdb), 100u, 2u);
    q = tdet_cat(q, TDET_S(TDS_DB));
    if (peak) { q = tdet_cat(q, TDET_S(TDS_PEAK)); q = tdet_fix(q, (peak * 10000u + 16384u) / 32768u, 10000u, 4u); }
    return q;
}
/* the fields; each returns the end of what it wrote (nothing written = unknown) */
TDET_FN char *tdet_trackno(char *q, uint32_t tno)
{
    if (tno >> 10) { q = tdet_cat(q, TDET_S(TDS_DISC)); q = tdet_dec(q, tno >> 10); q = tdet_cat(q, TDET_S(TDS_SP2)); }
    if (tno & 1023u) { q = tdet_cat(q, TDET_S(TDS_TRACK)); q = tdet_dec(q, tno & 1023u); }
    return q;
}
TDET_FN char *tdet_length(char *q, uint32_t secs)
{
    if (!secs) return q;
    q = tdet_dec(q, secs / 60u); *q++ = ':'; *q++ = (char)('0' + (secs % 60u) / 10u); *q++ = (char)('0' + secs % 10u);
    return q;
}
/* codec: 1 MP3, 2 FLAC (from the details record, else from the track record); bits is the FLAC bit depth */
TDET_FN char *tdet_format(char *q, uint32_t codec, uint32_t bits, uint32_t flags, int has)
{
    q = tdet_cat(q, codec == 2u ? TDET_S(TDS_FLAC) : TDET_S(TDS_MP3));
    if (has && codec == 2u && bits) { *q++ = ' '; q = tdet_dec(q, bits); q = tdet_cat(q, TDET_S(TDS_BIT)); }
    if (has && codec == 1u) q = tdet_cat(q, (flags & TD_VBR) ? TDET_S(TDS_VBR) : TDET_S(TDS_CBR));
    return q;
}
TDET_FN char *tdet_audio(char *q, uint32_t rate, uint32_t ch)
{
    if (!rate) return q;
    q = tdet_fix(q, rate / 100u, 10u, 1u); q = tdet_cat(q, TDET_S(TDS_KHZ));
    return tdet_cat(q, ch == 1u ? TDET_S(TDS_MONO) : TDET_S(TDS_STEREO));
}
TDET_FN char *tdet_bitrate(char *q, uint32_t kbps, uint32_t codec, uint32_t flags)
{
    if (!kbps) return q;
    q = tdet_dec(q, kbps);
    return tdet_cat(q, (codec == 1u && !(flags & TD_VBR)) ? TDET_S(TDS_KBPS) : TDET_S(TDS_KBPS_AVG));
}
TDET_FN char *tdet_size(char *q, uint32_t bytes)           /* megabytes (1 MB = 1,048,576 B) with one decimal, rounded to nearest */
{
    if (!bytes) return q;
    q = tdet_fix(q, bytes / 104858u + ((bytes % 104858u) >= 52430u ? 1u : 0u), 10u, 1u);      /* = (bytes + 52428) / 104858 without the overflow near 4 GiB */
    return tdet_cat(q, TDET_S(TDS_MB));
}
TDET_FN char *tdet_gapless(char *q, uint32_t codec, uint32_t flags, uint32_t delay, uint32_t pad)
{
    if (codec == 2u) return tdet_cat(q, TDET_S(TDS_EXACT));
    if (flags & TD_LAME) { q = tdet_cat(q, TDET_S(TDS_DELAY)); q = tdet_dec(q, delay); q = tdet_cat(q, TDET_S(TDS_PAD)); q = tdet_dec(q, pad); }
    return q;
}
TDET_FN char *tdet_cover(char *q, uint32_t flags, uint32_t kib)
{
    if (flags & TD_SIDECAR) q = tdet_cat(q, TDET_S(TDS_SIDECAR));
    if ((flags & TD_SIDECAR) && (flags & TD_COVER)) q = tdet_cat(q, TDET_S(TDS_PLUS));
    if (flags & TD_COVER) { q = tdet_cat(q, TDET_S(TDS_EMBEDDED)); q = tdet_dec(q, kib); q = tdet_cat(q, TDET_S(TDS_KB)); }
    if (!(flags & (TD_SIDECAR | TD_COVER))) q = tdet_cat(q, TDET_S(TDS_NONE));
    return q;
}
#endif
