#pragma GCC optimize ("Os")
#include "name_matrix.h"
#include "../stream/slot.h"
#include "../stream/library.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
extern char _tag_start;
extern void mp3play_io_quiesce(void);
extern uint32_t mp3play_degrade_now(void);
#define TAG_OFF ((uint32_t)(uintptr_t)&_tag_start)
#ifndef TAGB                                         /* the host test points it at a buffer */
#define TAGB    ((const volatile uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start))
#endif
#define NM_SLOT 7u                                   /* the cover's slot, as the preset probe uses it */
#define NM_MANIFEST 2048u                            /* matrix.tnm is exactly this long (tools/name_matrix.py pads it), so a fixed read works */
#define NM_MAX 16u
#define NM_HDR 32u                                   /* the first bytes of the opened file that are read back */

uint32_t nm_res_lo, nm_res_hi, nm_bad, nm_err, nm_dry;

P2_COLD_TEXT static uint32_t nibble_of(uint32_t id, const char *path)     /* the outcome of opening one path (see name_matrix.h) */
{
    if (!slot_open_path(NM_SLOT, path))
        return slot_err == SLOT_E_TOOLONG ? 6u : slot_err == SLOT_E_TIMEOUT ? 7u : (slot_err == SLOT_E_OPEN && slot_errcode == 4u) ? 4u : 5u;
    if (!slot_read_blocking(NM_SLOT, 0u, TAG_OFF + NM_MANIFEST, NM_HDR, CLK_HZ * 2u)) return 3u;
    const volatile uint8_t *h = TAGB + NM_MANIFEST;                        /* ID3v2 header (10 bytes), a frame header (10), the encoding byte, then the title "NMii" */
    const int good = h[0] == 'I' && h[1] == 'D' && h[2] == '3' && h[21] == 'N' && h[22] == 'M' && h[23] == (uint8_t)('0' + id / 10u) && h[24] == (uint8_t)('0' + id % 10u);
    return good ? 1u : 2u;
}

P2_COLD_TEXT uint32_t name_matrix_run(void)
{
    char path[264];
    nm_res_lo = nm_res_hi = 0; nm_bad = 0; nm_err = 0; nm_dry = 0;
    if (mp3play_degrade_now()) { nm_err = 4u; return 1u; }
    uint32_t n = library_root(path, 96u);
    if (!n || n + 24u > sizeof path) { nm_err = 1u; return 1u; }
    if (path[n - 1u] != '/') path[n++] = '/';
    for (const char *s = "name-test/matrix.tnm"; *s; s++) path[n++] = *s;
    path[n] = 0;
    mp3play_io_quiesce();
    const uint32_t dry0 = aout_stats.dry + aout_stats.under_ring;
    if (!slot_open_path(NM_SLOT, path)) { nm_err = 2u; return 1u; }
    if (!slot_read_blocking(NM_SLOT, 0u, TAG_OFF, NM_MANIFEST, CLK_HZ * 2u)) { nm_err = 3u; return 1u; }
    const volatile uint8_t *m = TAGB; uint32_t p = 5u, count = 0;
    if (!(m[0] == 'T' && m[1] == 'N' && m[2] == 'M' && m[3] == '1' && m[4] == ' ')) { nm_err = 5u; return 1u; }
    while (m[p] >= '0' && m[p] <= '9' && count < 100u) count = count * 10u + (m[p++] - '0');
    if (!count || count > NM_MAX || m[p] != '\n') { nm_err = 5u; return 1u; }
    p++;
    for (uint32_t k = 0; k < count; k++) {
        if (p + 6u >= NM_MANIFEST || m[p] < '0' || m[p] > '9' || m[p + 1u] < '0' || m[p + 1u] > '9' || m[p + 2u] != '|' || m[p + 4u] != '|') { nm_err = 6u; return nm_bad + 1u; }
        const uint32_t id = (uint32_t)(m[p] - '0') * 10u + (m[p + 1u] - '0'); const uint8_t expect = m[p + 3u];
        uint32_t q = p + 5u, len = 0;
        while (q < NM_MANIFEST && m[q] != '\n' && len < 255u) path[len++] = (char)m[q++];
        path[len] = 0; p = q + 1u;
        if (id >= NM_MAX) { nm_err = 6u; nm_bad++; continue; }
        mp3play_io_quiesce();
        const uint32_t nb = nibble_of(id, path);
        if (id < 8u) nm_res_lo |= nb << (4u * id); else nm_res_hi |= nb << (4u * (id - 8u));
        if (expect == 'o' ? nb != 1u : nb < 4u) nm_bad++;
    }
    nm_dry = aout_stats.dry + aout_stats.under_ring - dry0;
    return nm_bad;
}
