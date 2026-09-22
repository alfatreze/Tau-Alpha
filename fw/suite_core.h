/* Diagnostics report record (docs/TEST_SUITE_SPEC.md sections 2, 7, 8). Portable C, no libc, no globals: the caller
 * supplies the buffers. Built into the firmware by fw/suite.inc and into tools/host/suite_harness.c, which
 * sim/test_suite.py checks against tools/decode_tau_suite.py.
 *
 * Record   : 'T' 'D' fmt(1) profile(1) then TLV entries {tag u8, len u8, value}, then CRC32 (LE) of all bytes before it.
 * Text     : "TAUD1:" + base64url (no padding) of the record: what the QR code carries.
 * Persist  : four 31-bit words (APF stores signed int32): a summary that survives Quit without any QR (sp_pack).
 * Short    : 36 characters, Crockford base32, of a 22-byte summary with a CRC16 (fallback when only a photo exists). */
#ifndef TAU_SUITE_CORE_H
#define TAU_SUITE_CORE_H
#include <stdint.h>

#ifndef SR_FN
#define SR_FN static      /* firmware: cold code */
#endif
#define SR_FMT 1u
enum { SR_T_BUILD = 1, SR_T_MEM, SR_T_TEST, SR_T_SDRAM, SR_T_PSRAM, SR_T_COLD, SR_T_TIME, SR_T_AUDIO, SR_T_LIB,
       SR_T_SET, SR_T_ERR, SR_T_NOTE, SR_T_DECPROF };
/* SR_T_DECPROF (B-088/B-089, docs/TEST_SUITE_SPEC.md section 11): u16 x 4 --
 * h_pct, i_pct, s_pct (MP3 Huffman/IMDCT/Subband, percent of the CT_AUD
 * window's real time, uncapped), r_pct (FLAC bit-reader share of channel 0's
 * decode, 0-100). Present only when MP3_PROFILE/FLAC_PROFILE are compiled in
 * -- a normal Check build never emits this tag, and an old decoder skips it
 * unrecognised either way. */
/* SR_T_TEST entries (len 6): id u8, result u8 (0 pass, 1 fail, 2 skipped, 3 not applicable), value u32 (test specific). */
enum { SR_PASS = 0, SR_FAIL = 1, SR_SKIP = 2, SR_NA = 3 };

typedef struct { uint8_t *b; uint32_t n, cap; int over; } sr_t;

SR_FN uint32_t sr_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return ~c;
}

SR_FN void sr_put(sr_t *s, uint8_t v) { if (s->n < s->cap) s->b[s->n++] = v; else s->over = 1; }

SR_FN void sr_init(sr_t *s, uint8_t *buf, uint32_t cap, uint32_t profile)
{
    s->b = buf; s->n = 0; s->cap = cap; s->over = 0;
    sr_put(s, 'T'); sr_put(s, 'D'); sr_put(s, (uint8_t)SR_FMT); sr_put(s, (uint8_t)profile);
}

SR_FN void sr_tlv(sr_t *s, uint8_t tag, const uint8_t *v, uint32_t len)
{
    sr_put(s, tag); sr_put(s, (uint8_t)len);
    for (uint32_t i = 0; i < len; i++) sr_put(s, v[i]);
}

/* little-endian fields of 1, 2 and 4 bytes, up to eight values per entry */
SR_FN void sr_vals(sr_t *s, uint8_t tag, uint32_t width, const uint32_t *v, uint32_t count)
{
    uint8_t t[32]; uint32_t n = 0;
    for (uint32_t i = 0; i < count && n + width <= sizeof(t); i++)
        for (uint32_t k = 0; k < width; k++) t[n++] = (uint8_t)(v[i] >> (8u * k));
    sr_tlv(s, tag, t, n);
}

SR_FN void sr_test(sr_t *s, uint32_t id, uint32_t result, uint32_t value)
{
    uint8_t t[6] = { (uint8_t)id, (uint8_t)result, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
    sr_tlv(s, SR_T_TEST, t, 6);
}

/* Appends the CRC32; returns the record length, or 0 if anything did not fit. */
SR_FN uint32_t sr_finish(sr_t *s)
{
    uint32_t c = sr_crc32(s->b, s->n);
    for (int k = 0; k < 4; k++) sr_put(s, (uint8_t)(c >> (8 * k)));
    return s->over ? 0u : s->n;
}

/* "TAUD1:" + base64url; returns the string length (out must hold 6 + 4*ceil(len/3) + 1), 0 if it does not fit. */
SR_FN uint32_t sr_text(const uint8_t *rec, uint32_t len, char *out, uint32_t cap)
{
    static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t need = 6u + ((len * 4u + 2u) / 3u) + 1u;
    if (need > cap) return 0;
    uint32_t o = 0;
    for (const char *p = "TAUD1:"; *p; p++) out[o++] = *p;
    for (uint32_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)rec[i] << 16;
        if (i + 1 < len) v |= (uint32_t)rec[i + 1] << 8;
        if (i + 2 < len) v |= rec[i + 2];
        out[o++] = a[(v >> 18) & 63u]; out[o++] = a[(v >> 12) & 63u];
        if (i + 1 < len) out[o++] = a[(v >> 6) & 63u];
        if (i + 2 < len) out[o++] = a[v & 63u];
    }
    out[o] = 0;
    return o;
}

/* ---- persisted summary: four 31-bit words ---- */
typedef struct {
    uint32_t profile;      /* 3 bits */
    uint32_t run;          /* 8 bits, wraps */
    uint32_t verdict;      /* 2 bits: 0 none, 1 all passed, 2 something failed, 3 incomplete */
    uint32_t pass, fail;   /* 15-bit masks over test ids 0..14 */
    uint32_t worst;        /* 9 bits, cycles, saturating */
    uint32_t cold10;       /* 9 bits, cycles per instruction word x 10 */
    uint32_t late;         /* 6 bits saturating */
    uint32_t stall;        /* 7 bits, ms, saturating */
    uint32_t load100;      /* 12 bits, tenths of a second of the last track load, saturating */
    uint32_t lib_err;      /* 6 bits */
    uint32_t cold_err;     /* 6 bits */
    uint32_t fw_minor;     /* 7 bits */
} sp_t;

SR_FN uint32_t sp_sat(uint32_t v, uint32_t bits) { uint32_t m = (1u << bits) - 1u; return v > m ? m : v; }

SR_FN void sp_pack(const sp_t *p, uint32_t w[4])
{
    w[0] = (SR_FMT & 15u) | ((p->profile & 7u) << 4) | ((p->run & 255u) << 7) | ((p->verdict & 3u) << 15);
    w[1] = (p->pass & 0x7FFFu) | ((p->fail & 0x7FFFu) << 15);
    w[2] = sp_sat(p->worst, 9) | (sp_sat(p->cold10, 9) << 9) | (sp_sat(p->late, 6) << 18) | (sp_sat(p->stall, 7) << 24);
    w[3] = sp_sat(p->load100, 12) | (sp_sat(p->lib_err, 6) << 12) | (sp_sat(p->cold_err, 6) << 18) | (sp_sat(p->fw_minor, 7) << 24);
}

/* ---- short code: 22 bytes -> 36 Crockford base32 characters, grouped by the caller ---- */
SR_FN uint32_t sr_crc16(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= (uint32_t)p[i] << 8;
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? ((c << 1) ^ 0x1021u) & 0xFFFFu : (c << 1) & 0xFFFFu;
    }
    return c;
}

/* w = the four persisted words, bitstream = FPGA revision; out gets 36 characters plus a NUL. */
SR_FN void sp_short(const uint32_t w[4], uint32_t bitstream, char *out)
{
    static const char a[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    uint8_t b[22];
    for (uint32_t i = 0; i < 4; i++) for (uint32_t k = 0; k < 4; k++) b[i * 4 + k] = (uint8_t)(w[i] >> (8 * k));
    for (uint32_t k = 0; k < 4; k++) b[16 + k] = (uint8_t)(bitstream >> (8 * k));
    uint32_t c = sr_crc16(b, 20);
    b[20] = (uint8_t)c; b[21] = (uint8_t)(c >> 8);
    uint32_t acc = 0, nb = 0, o = 0;
    for (uint32_t i = 0; i < 22; i++) {
        acc = (acc << 8) | b[i]; nb += 8;
        while (nb >= 5) { out[o++] = a[(acc >> (nb - 5)) & 31u]; nb -= 5; }
    }
    if (nb) out[o++] = a[(acc << (5 - nb)) & 31u];
    out[o] = 0;
}
#endif
