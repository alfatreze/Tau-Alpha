#pragma GCC optimize ("Os")
#include "mpreset_host.h"
#include "../stream/slot.h"
#include "../stream/library.h"
#include "../platform/mmio.h"                               /* P2_COLD_TEXT */
extern char _tag_start;
extern uint32_t mp3play_degrade_now(void);         /* the governor's level: 0 NORMAL */
extern void mp3play_io_quiesce(void);
#define TAG_OFF ((uint32_t)(uintptr_t)&_tag_start)
#ifndef MPH_TAGB
#define MPH_TAGB ((const volatile uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start))
#endif
#ifndef MPH_PSRAM
#define MPH_PSRAM ((volatile uint32_t *)(uintptr_t)0xA455F000u)    /* the last 4 KB of the METR region (0xA4550000..0xA4560000; meters_metr_load stops at 0xF000): the index, then the last preset read */
#endif
#define MPH_SLOT 7u                                                  /* the cover's slot: the same kind of open and read (measured: DEV 212, about 33 ms for 200 bytes) */
#define PRE_AT (((MP_IDX_MAX_BYTES + 3u) & ~3u) / 4u)               /* word index of the cached preset file */
uint32_t mph_idx_ok, mph_reads, mph_fails, mph_err, mph_read_max, mph_open_max;
static uint32_t idx_n, idx_mid = 0xFFFFFFFFu, pre_id = 0xFFFFFFFFu, pre_len, bad_mask;     /* bad_mask: the meters whose index is missing or damaged: not asked again until the next boot */

P2_COLD_TEXT static uint32_t build_path(char *out, uint32_t cap, uint32_t mid, int id)       /* <root>/tau-presets/<mid>/index.tpi or p<id>.tpr */
{
    uint32_t n = library_root(out, cap);
    if (!n || n + 40u > cap) return 0;
    if (out[n - 1u] == '/') n--;                                                          /* the library's root ends with a slash ("/Assets/tau/common/") */
    const char *a = "/tau-presets/"; for (; *a; a++) out[n++] = *a;
    char d[4]; uint32_t k = 0; uint32_t v = mid; do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 3u); while (k) out[n++] = d[--k];
    out[n++] = '/';
    if (id < 0) { const char *f = "index.tpi"; for (; *f; f++) out[n++] = *f; }
    else { out[n++] = 'p'; v = (uint32_t)id; k = 0; do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 3u); while (k) out[n++] = d[--k]; const char *f = ".tpr"; for (; *f; f++) out[n++] = *f; }
    out[n] = 0; return n;
}
/* Opens `path` and reads `len` bytes from 0 into the tag buffer; 1 when read. Governor NORMAL only; the stream is let finish first. Times go to the diagnostics. */
P2_COLD_TEXT static int read_file(const char *path, uint32_t len)
{
#if BUG != 12                                                                               /* BUG: host-test mutants (sim/test_p2_mpreset_meters.py) */
    if (mp3play_degrade_now() != 0u) { mph_err = 10u; return 0; }                      /* the audio comes first: not now */
#endif
    mp3play_io_quiesce();
    uint32_t t0 = cycles();
    if (!slot_open_path(MPH_SLOT, path)) { mph_err = 11u; return 0; }
    uint32_t t1 = cycles(); if (t1 - t0 > mph_open_max) mph_open_max = t1 - t0;
    if (!slot_read_blocking(MPH_SLOT, 0u, TAG_OFF, len, CLK_HZ * 2u)) { mph_err = 12u; return 0; }
    t0 = cycles() - t1; if (t0 > mph_read_max) mph_read_max = t0;
    return 1;
}
P2_COLD_TEXT static void to_psram(uint32_t word0, uint32_t n)                                         /* copies n bytes of the tag buffer into the PSRAM cache, a word at a time (the window takes words only) */
{
    for (uint32_t w = 0; w < (n + 3u) / 4u; w++) { uint32_t v = 0; for (uint32_t k = 0; k < 4u && 4u * w + k < n; k++) v |= (uint32_t)MPH_TAGB[4u * w + k] << (8u * k); MPH_PSRAM[word0 + w] = v; }
}
P2_COLD_TEXT static void from_psram(uint32_t word0, uint8_t *dst, uint32_t n)
{
    for (uint32_t w = 0; w < (n + 3u) / 4u; w++) { const uint32_t v = MPH_PSRAM[word0 + w]; for (uint32_t k = 0; k < 4u && 4u * w + k < n; k++) dst[4u * w + k] = (uint8_t)(v >> (8u * k)); }
}
P2_COLD_TEXT static int idx_name_at(uint32_t i, char *out, uint8_t *id)
{
    uint8_t e[MP_IDX_ENTRY]; const uint32_t at = 14u + i * MP_IDX_ENTRY;
    uint32_t w0 = at & ~3u, w1 = (at + MP_IDX_ENTRY + 3u) & ~3u; uint8_t buf[MP_IDX_ENTRY + 8u];
    for (uint32_t w = w0; w < w1; w += 4u) { const uint32_t v = MPH_PSRAM[w >> 2]; for (uint32_t k = 0; k < 4u; k++) buf[w - w0 + k] = (uint8_t)(v >> (8u * k)); }
    for (uint32_t k = 0; k < MP_IDX_ENTRY; k++) e[k] = buf[at - w0 + k];
    if (id) *id = e[0];
    uint32_t k = 0; for (; k < MP_NAME - 1u && e[2u + k]; k++) out[k] = (char)e[2u + k];
    out[k] = 0; return 1;
}
P2_COLD_TEXT static uint32_t enter(uint32_t mid)
{
    if (mid == idx_mid && idx_n) return idx_n;                                            /* already cached */
    idx_n = 0; idx_mid = 0xFFFFFFFFu; pre_id = 0xFFFFFFFFu;
    char path[LIBP_MAX_PATH];
    if (!build_path(path, sizeof path, mid, -1)) { mph_err = 1u; return 0; }
    if (!read_file(path, 14u)) return 0;
    const uint32_t cnt = (uint32_t)MPH_TAGB[12] | ((uint32_t)MPH_TAGB[13] << 8);
    if (cnt > MP_IDX_MAX) { mph_err = 2u; return 0; }
    const uint32_t total = 14u + cnt * MP_IDX_ENTRY;
    if (!read_file(path, total)) return 0;
    uint8_t *b = (uint8_t *)(uintptr_t)MPH_TAGB;                                           /* the read is complete: plain reads */
    const int e = mp_index_check(b, total, mid);
    if (e) { mph_err = 20u + (uint32_t)e; return 0; }
    to_psram(0u, total);
    idx_n = cnt; idx_mid = mid; mph_idx_ok++; mph_err = 0u;
    return idx_n;
}
P2_COLD_TEXT uint32_t mph_enter(uint32_t mid)
{
    if (mid < 32u && (bad_mask >> mid & 1u)) return 0u;
    const uint32_t n = enter(mid);
    if (!n && mph_err != 10u && mid < 32u) bad_mask |= 1u << mid;                          /* missing or damaged: remembered; "the governor is busy" is not a verdict on the file */
    return n;
}
P2_COLD_TEXT uint32_t mph_count(void) { return idx_n; }
P2_COLD_TEXT void mph_name(uint32_t pos, char *out)
{
    out[0] = 0;
    if (pos == 0u) { const char *s = "DEFAULT"; uint32_t k = 0; for (; s[k]; k++) out[k] = s[k]; out[k] = 0; return; }
    if (pos <= idx_n) idx_name_at(pos - 1u, out, 0);
}
P2_COLD_TEXT uint32_t mph_id1(uint32_t pos)
{
    char nm[MP_NAME]; uint8_t id;
    if (pos == 0u || pos > idx_n) return 0u;
    idx_name_at(pos - 1u, nm, &id); return (uint32_t)id + 1u;
}
P2_COLD_TEXT int mph_find(uint32_t id1)
{
    if (!id1) return -1;
#if BUG == 13
    return (int)id1 <= (int)idx_n ? (int)id1 : -1;                                          /* mutant: the remembered id taken for a list position */
#endif
    for (uint32_t i = 1u; i <= idx_n; i++) if (mph_id1(i) == id1) return (int)i;
    return -1;
}
P2_COLD_TEXT int mph_values(uint32_t mid, uint32_t pos, const mp_param_t *ps, uint32_t np, uint16_t *vals)
{
    if (pos == 0u) { mp_defaults(ps, np, vals); return 1; }
    if (mid != idx_mid || pos > idx_n) return 0;
    char nm[MP_NAME]; uint8_t id; idx_name_at(pos - 1u, nm, &id);
    uint8_t pb[MP_PRE_MAX_BYTES];
    if (pre_id == id) { from_psram(PRE_AT, pb, pre_len); }                                   /* the preset read last (the tuning's base): no read */
    else {
        char path[LIBP_MAX_PATH];
        if (!build_path(path, sizeof path, mid, id)) { mph_err = 1u; mph_fails++; return 0; }
        if (!read_file(path, 14u)) { mph_fails++; return 0; }
        const uint32_t len = 14u + MPH_TAGB[13] + 4u * (uint32_t)MPH_TAGB[12];
        if (len > MP_PRE_MAX_BYTES || !read_file(path, len)) { mph_err = len > MP_PRE_MAX_BYTES ? 3u : mph_err; mph_fails++; return 0; }
        for (uint32_t k = 0; k < len; k++) pb[k] = MPH_TAGB[k];
        const int e = mp_preset_check(pb, len, mid, id);
        if (e) { mph_err = 30u + (uint32_t)e; mph_fails++; return 0; }
        to_psram(PRE_AT, len); pre_id = id; pre_len = len; mph_reads++; mph_err = 0u;
    }
#if BUG == 11
    mp_defaults(ps, np, vals);                                                              /* mutant: the file is read and checked but not applied */
#else
    mp_preset_apply(pb, ps, np, vals);
#endif
    return 1;
}
