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
#define NM_MANIFEST 3072u                            /* matrix.tnm is exactly this long (tools/name_matrix.py pads it), so a fixed read works; the opened file's first bytes are read right after it in the 4 KB tag zone */
#define NM_MAX 24u
#define NM_HDR 32u                                   /* the first bytes of the opened file that are read back */

uint32_t nm_res[3], nm_cod[3], nm_bad, nm_err, nm_dry, nm_runs;

P2_COLD_TEXT static uint32_t check_open(uint32_t fid)      /* after an open that worked: 1 the right file, 2 another file, 3 the read failed */
{
    if (!slot_read_blocking(NM_SLOT, 0u, TAG_OFF + NM_MANIFEST, NM_HDR, CLK_HZ * 2u)) return 3u;
    const volatile uint8_t *h = TAGB + NM_MANIFEST;                        /* ID3v2 header (10 bytes), a frame header (10), the encoding byte, then the title "NMii" */
    return (h[0] == 'I' && h[1] == 'D' && h[2] == '3' && h[21] == 'N' && h[22] == 'M' && h[23] == (uint8_t)('0' + fid / 10u) && h[24] == (uint8_t)('0' + fid % 10u)) ? 1u : 2u;
}

P2_COLD_TEXT static uint32_t nibble_of(uint32_t fid, const char *path, uint32_t *code)     /* the outcome of the plain open of one path (see name_matrix.h) */
{
    *code = 0;
    if (slot_open_path(NM_SLOT, path)) return check_open(fid);
    *code = slot_err == SLOT_E_OPEN ? (uint32_t)(slot_errcode & 7u) : 8u + slot_err;
    return slot_err == SLOT_E_TOOLONG ? 6u : slot_err == SLOT_E_TIMEOUT ? 7u : (slot_err == SLOT_E_OPEN && slot_errcode == 4u) ? 4u : 5u;      /* 4 = the target's code 4, MALFORMED PATH (docs/host-target-commands: 3 not found, 4 malformed path, 5 general error); 5 = any other code, nm_cod has it */
}

P2_COLD_TEXT uint32_t name_matrix_run(void)
{
    char path[304]; uint8_t rn[NM_MAX], rc[NM_MAX];                    /* a path longer than the 255 bytes the open accepts is read whole: the firmware must refuse it, not cut it */
    for (uint32_t i = 0; i < NM_MAX; i++) rn[i] = rc[i] = 0;
    for (uint32_t i = 0; i < 3u; i++) nm_res[i] = nm_cod[i] = 0;
    nm_bad = 0; nm_err = 0; nm_dry = 0;
    if (mp3play_degrade_now()) { nm_err = 4u; return 1u; }
    nm_runs++;
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
    if (!(m[0] == 'T' && m[1] == 'N' && m[2] == 'M' && m[3] == '2' && m[4] == ' ')) { nm_err = 5u; return 1u; }
    while (m[p] >= '0' && m[p] <= '9' && count < 100u) count = count * 10u + (m[p++] - '0');
    if (!count || count > NM_MAX || m[p] != '\n') { nm_err = 5u; return 1u; }
    p++;
    for (uint32_t k = 0; k < count; k++) {
        if (p + 9u >= NM_MANIFEST || m[p] < '0' || m[p] > '9' || m[p + 1u] < '0' || m[p + 1u] > '9' || m[p + 2u] != '|' || m[p + 4u] != '|' || m[p + 5u] < '0' || m[p + 5u] > '9' || m[p + 6u] < '0' || m[p + 6u] > '9' || m[p + 7u] != '|') { nm_err = 6u; break; }
        const uint32_t id = (uint32_t)(m[p] - '0') * 10u + (m[p + 1u] - '0'), fid = (uint32_t)(m[p + 5u] - '0') * 10u + (m[p + 6u] - '0'); const uint8_t expect = m[p + 3u];
        uint32_t q = p + 8u, len = 0;
        while (q < NM_MANIFEST && m[q] != '\n' && len < 300u) path[len++] = (char)m[q++];
        path[len] = 0; p = q + 1u;
        if (id >= NM_MAX) { nm_err = 6u; nm_bad++; continue; }
        mp3play_io_quiesce();
        uint32_t code; const uint32_t nb = nibble_of(fid, path, &code);
        rn[id] = (uint8_t)nb; rc[id] = (uint8_t)code;
        if (expect == 'o' ? nb != 1u : expect == 'f' ? nb < 4u : 0) nm_bad++;
    }
    for (uint32_t i = 0; i < NM_MAX; i++) { nm_res[i / 8u] |= (uint32_t)rn[i] << (4u * (i % 8u)); nm_cod[i / 8u] |= (uint32_t)rc[i] << (4u * (i % 8u)); }
    nm_dry = aout_stats.dry + aout_stats.under_ring - dry0;
    return nm_bad;
}
P2_COLD_TEXT void name_matrix_auto(void) { if (!nm_runs) (void)name_matrix_run(); }
