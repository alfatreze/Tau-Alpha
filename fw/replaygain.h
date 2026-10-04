/* fw/replaygain.h -- ReplayGain (Cymo C6, B-599). Pure code, host-tested by sim/test_replaygain.py; fw/player.c and fw/flac.c only call it.
 *
 * What it does: read the ReplayGain gain tags a file already carries (ID3v2 TXXX "REPLAYGAIN_TRACK_GAIN" / "REPLAYGAIN_ALBUM_GAIN", FLAC Vorbis comments of the same names,
 * values like "-6.48 dB"), turn the one the setting asks for into a Q15 factor, and fold it into the volume target. The result is ATTENUATE-ONLY: the target never exceeds
 * unity (0 dB), so a positive ReplayGain can only bring a quiet volume setting back toward full volume and can never clip; at volume position 100 only negative gains
 * act. No peak or limiter logic is needed for that reason. Values are kept in centi-dB (hundredths of a dB) as int32: "-6.48 dB" is -648.
 * Not read: iTunNORM, ID3 RVA2, APE tags, R128 tags (a later step if the library needs them). */
#ifndef REPLAYGAIN_H
#define REPLAYGAIN_H
#include <stdint.h>
/* RG_COLD marks the parsing helpers: fw/player.c defines it as its cold-code attribute, so these live in PSRAM and not in the on-chip RAM (about 1.2 KB); host tests leave it empty. */
#ifndef RG_COLD
#define RG_COLD
#endif

#define RG_UNITY   32768u        /* Q15 */
#define RG_CDB_MIN (-3000)       /* -30 dB: a corrupt tag cannot mute a track */
#define RG_CDB_MAX ( 1800)       /* +18 dB */
enum { RG_OFF = 0, RG_TRACK, RG_ALBUM, RG_MODES };

typedef struct { int32_t track_cdb, album_cdb; uint8_t have; } rg_t;   /* have: bit 0 track gain found, bit 1 album gain found */
static inline void rg_clear(rg_t *r) { r->track_cdb = r->album_cdb = 0; r->have = 0; }

/* "-6.48 dB", "+3.2 dB", "-0.5", " 1.00 dB": optional spaces and sign, digits, optional '.' and up to two (more are ignored) fraction digits, then anything (" dB").
 * Returns 1 and the value in centi-dB, or 0 if there is no number. */
static RG_COLD int rg_parse_cdb(const char *s, uint32_t n, int32_t *out)
{
    uint32_t i = 0;
    while (i < n && s[i] == ' ') i++;
    int neg = 0;
    if (i < n && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); i++; }
    uint32_t digits = 0; int32_t whole = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') { if (whole < 10000) whole = whole * 10 + (s[i] - '0'); digits++; i++; }
    int32_t frac = 0; uint32_t fd = 0;
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') { if (fd < 2u) { frac = frac * 10 + (s[i] - '0'); fd++; } digits++; i++; }
    }
    if (!digits) return 0;
    if (fd == 1u) frac *= 10;
    int32_t v = whole * 100 + frac;
    if (neg) v = -v;
    if (v < RG_CDB_MIN) v = RG_CDB_MIN;
    if (v > RG_CDB_MAX) v = RG_CDB_MAX;
    *out = v;
    return 1;
}

/* Q15 factor for a gain in centi-dB: 10^(cdb/2000). Computed as 2^(x) with x = cdb * log2(10) / 2000, an integer part (a shift) and a fractional part (a 17-entry table of 2^(i/16) in Q15, linear interpolation between entries: accurate to about 0.003 dB). The result
 * is a uint32 (up to +18 dB = 7.9, so about 260,000): fold it into a volume target with rg_target(), never into samples directly. */
static RG_COLD uint32_t rg_factor_q15(int32_t cdb)
{
    static const uint32_t t[17] = { 32768u, 34219u, 35734u, 37312u, 38968u, 40693u, 42494u, 44376u, 46341u, 48393u, 50535u, 52773u, 55109u, 57549u, 60097u, 62757u, 65536u };
    const int32_t x = (cdb * 10885) / 100;                        /* Q16 exponent: cdb * log2(10) / 2000 * 65536; |cdb| <= 3000 so this fits int32 */
    const int32_t n = x >> 16;                                    /* floor (arithmetic shift) */
    const uint32_t f = (uint32_t)(x - (n << 16));                 /* 0 .. 65535 */
    const uint32_t i = f >> 12, r = f & 0xFFFu;                   /* 16 steps of 4096 */
    const uint32_t m = t[i] + (((t[i + 1] - t[i]) * r) >> 12);    /* Q15, 1.0 .. 2.0; the product is below 2^27 */
    if (n >= 0) return n > 4 ? m << 4 : m << n;
    return -n > 15 ? 0u : (m >> -n);
}

/* The factor for the chosen mode. Album mode falls back to the track gain when the file has no album gain; no tag at all is unity (0 dB). */
static RG_COLD uint32_t rg_pick_factor(uint32_t mode, const rg_t *r)
{
    if (mode == RG_ALBUM && (r->have & 2u)) return rg_factor_q15(r->album_cdb);
    if (mode != RG_OFF && (r->have & 1u))   return rg_factor_q15(r->track_cdb);
    if (mode != RG_OFF && (r->have & 2u))   return rg_factor_q15(r->album_cdb);   /* track mode with only an album gain */
    return RG_UNITY;
}

/* The volume target with ReplayGain folded in: the volume's Q15 gain times the factor, never above unity. */
static inline int32_t rg_target(int32_t vol_q15, uint32_t factor)
{
    if (factor > (1u << 18)) factor = 1u << 18;                    /* +18 dB is the most a tag can ask for */
    const uint32_t g = ((uint32_t)vol_q15 * (factor >> 2)) >> 13;  /* vol <= 2^15 and factor >> 2 <= 2^16: the product stays below 2^32; costs 0.4% of resolution at -30 dB */
    return g > RG_UNITY ? (int32_t)RG_UNITY : (int32_t)g;
}

/* ---- tag readers ------------------------------------------------------------------------------------------------------------------------------------------- */
static inline char rg_up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static RG_COLD int rg_key_eq(const char *s, uint32_t n, const char *key)
{
    uint32_t i = 0;
    for (; key[i]; i++) { if (i >= n || rg_up(s[i]) != key[i]) return 0; }
    return i == n;
}

/* Record one name/value pair (ASCII, name already without '=' or NUL). */
static RG_COLD void rg_note(rg_t *r, const char *name, uint32_t nn, const char *val, uint32_t vn)
{
    int32_t c;
    if (rg_key_eq(name, nn, "REPLAYGAIN_TRACK_GAIN")) { if (rg_parse_cdb(val, vn, &c)) { r->track_cdb = c; r->have |= 1u; } }
    else if (rg_key_eq(name, nn, "REPLAYGAIN_ALBUM_GAIN")) { if (rg_parse_cdb(val, vn, &c)) { r->album_cdb = c; r->have |= 2u; } }
}

/* One ID3v2 TXXX frame BODY (encoding byte, description, NUL, value). Encodings 0 (Latin-1) and 3 (UTF-8) are read as ASCII; 1 and 2 (UTF-16, with or without BOM) are
 * folded to ASCII (a code unit above 0xFF becomes '?', which never matches a key or a digit). Bodies longer than 96 bytes are not ReplayGain tags. */
static RG_COLD void rg_txxx_body(rg_t *r, const uint8_t *b, uint32_t n)
{
    if (n < 4u || n > 96u) return;
    char d[40], v[24]; uint32_t dn = 0, vn = 0;
    const uint8_t enc = b[0];
    if (enc == 1u || enc == 2u) {
        uint32_t s = 1u; int be = (enc == 2u);
        for (int part = 0; part < 2; part++) {
            if (s + 1u < n && ((b[s] == 0xFFu && b[s + 1u] == 0xFEu) || (b[s] == 0xFEu && b[s + 1u] == 0xFFu))) { be = (b[s] == 0xFEu); s += 2u; }
            while (s + 1u < n) {
                const uint32_t u = be ? (((uint32_t)b[s] << 8) | b[s + 1u]) : (((uint32_t)b[s + 1u] << 8) | b[s]);
                s += 2u;
                if (!u) break;
                const char c = u < 0x100u ? (char)u : '?';
                if (part == 0) { if (dn < sizeof d) d[dn++] = c; } else { if (vn < sizeof v) v[vn++] = c; }
            }
        }
    } else {
        uint32_t s = 1u;
        while (s < n && b[s]) { if (dn < sizeof d) d[dn++] = (char)b[s]; s++; }
        s++;
        while (s < n && b[s]) { if (vn < sizeof v) v[vn++] = (char)b[s]; s++; }
    }
    rg_note(r, d, dn, v, vn);
}

/* Scan the frames of an ID3v2 tag that is already in memory (`avail` bytes loaded, `tag_len` the whole tag) for ReplayGain TXXX frames. Handles v2.3 (plain
 * big-endian frame sizes) and v2.4 (syncsafe). Stops at padding, at a frame that runs past what is loaded, or at the end. */
static RG_COLD void rg_id3_scan(rg_t *r, const uint8_t *tag, uint32_t avail, uint32_t tag_len)
{
    if (avail < 10u || tag[0] != 'I' || tag[1] != 'D' || tag[2] != '3') return;
    const uint8_t major = tag[3];
    const uint32_t lim = tag_len < avail ? tag_len : avail;
    uint32_t p = 10u;
    while (p + 10u <= lim) {
        if (tag[p] == 0) break;
        const uint32_t fsize = (major >= 4u)
            ? (((uint32_t)(tag[p + 4] & 0x7Fu) << 21) | ((uint32_t)(tag[p + 5] & 0x7Fu) << 14) | ((uint32_t)(tag[p + 6] & 0x7Fu) << 7) | (uint32_t)(tag[p + 7] & 0x7Fu))
            : (((uint32_t)tag[p + 4] << 24) | ((uint32_t)tag[p + 5] << 16) | ((uint32_t)tag[p + 6] << 8) | (uint32_t)tag[p + 7]);
        if (!fsize || p + 10u + fsize > lim) break;
        if (tag[p] == 'T' && tag[p + 1] == 'X' && tag[p + 2] == 'X' && tag[p + 3] == 'X') rg_txxx_body(r, &tag[p + 10u], fsize);
        p += 10u + fsize;
    }
}

/* A Vorbis comment entry "NAME=value" (FLAC). */
static RG_COLD __attribute__((unused)) void rg_vorbis_entry(rg_t *r, const char *e, uint32_t n)
{
    uint32_t k = 0;
    while (k < n && e[k] != '=') k++;
    if (k >= n) return;
    rg_note(r, e, k, e + k + 1u, n - k - 1u);
}
#endif
