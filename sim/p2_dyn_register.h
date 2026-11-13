#include <string.h>
/* For the host harnesses that include fw2/ui/meters.c with P2_PACKS and no bin of packs (define MTR_TEST_WINDOW_LOAD, PACK_WINDOW_DATA = win_mem, MTR_TOC_BASE/MTR_PV_BASE host buffers BEFORE the include): builds a SYNTHETIC TOC in
 * those buffers from descriptor files (what tools/player2/meter_desc.py builds) -- row = the descriptor's manifest slot, the rows in between present but hidden (not selectable), the ids those of the shipped meters of that slot --
 * and runs the real registry on it (toc_index: selectable rows, fallback row, tuning collisions), and provides the window loader: selecting a row puts "its pack" in the window (a 64-byte stub whose first word is the row
 * number, then the descriptor as the image's last section), so the real dyn_register, tick dispatch, strikes, presets and tuning run on it. Include AFTER meters.c. */
static uint8_t desc_by_row[8][4096]; static uint32_t desc_len_by_row[8];
typedef struct { const uint8_t *b; uint32_t n; } dyn_buf_t;
static int dyn_buf_rd(void *c, uint32_t off, uint8_t *dst, uint32_t n) { const dyn_buf_t *d = (const dyn_buf_t *)c; if (off > d->n || n > d->n - off) return 0; memcpy(dst, d->b + off, n); return 1; }
static uint32_t win_tag(void) { return (uint32_t)win_mem[0] | ((uint32_t)win_mem[1] << 8) | ((uint32_t)win_mem[2] << 16) | ((uint32_t)win_mem[3] << 24); }       /* the row whose stub pack is in the window */
static int window_load(uint32_t m)
{
    win_row = 0xFFu; pack_act = 0xFFu;
    if (m >= 8u || !desc_len_by_row[m]) return MPK_E_NOFILE;
    memset(win_mem, 0, 64u); win_mem[0] = (uint8_t)m; memcpy(win_mem + 64u, desc_by_row[m], desc_len_by_row[m]);
    memset(&win_info, 0, sizeof win_info); win_info.size = 64u + desc_len_by_row[m]; win_info.entry = MB_WINDOW; win_info.ps_off = win_info.size;
    win_row = m; pack_act = m; mwin_loads++;
    return MPK_OK;
}
/* the raw 256-byte TOC row k (the layout of meterbin_core.h) */
static void syn_row(uint32_t k, uint32_t hash, const char *name, uint32_t flags, uint32_t cmds, uint32_t cyc, uint32_t np, uint32_t rev, uint32_t abi, uint32_t tau_min)
{
    uint8_t t[MB_TOC]; memset(t, 0, sizeof t);
    t[0] = (uint8_t)hash; t[1] = (uint8_t)(hash >> 8); t[2] = (uint8_t)(hash >> 16); t[3] = (uint8_t)(hash >> 24);
    for (uint32_t i = 0; i < 24u && name[i]; i++) t[44 + i] = (uint8_t)name[i];
    t[74] = (uint8_t)abi; t[75] = (uint8_t)(abi >> 8); t[76] = (uint8_t)tau_min; t[77] = (uint8_t)(tau_min >> 8); t[78] = (uint8_t)flags;
    t[80] = (uint8_t)rev; t[81] = (uint8_t)(rev >> 8); t[82] = (uint8_t)np; t[84] = (uint8_t)cmds; t[85] = (uint8_t)(cmds >> 8);
    for (uint32_t i = 0; i < 4u; i++) t[88 + i] = (uint8_t)(cyc >> (8u * i));
    for (uint32_t i = 0; i < MB_TOC; i += 4u) MTR_TOC_BASE[(MB_HDR + MB_TOC * k + i) >> 2] = (uint32_t)t[i] | ((uint32_t)t[i + 1u] << 8) | ((uint32_t)t[i + 2u] << 16) | ((uint32_t)t[i + 3u] << 24);
}
/* a TOC of `count` rows whose bytes are already written by syn_row: the registry as the boot path leaves it */
static void syn_install(uint32_t count)
{
    mb_info_t in; memset(&in, 0, sizeof in);
    in.count = (uint16_t)count; in.pv_off = MB_HDR + MB_TOC * count; in.packs_off = in.pv_off; in.total = in.packs_off + 4u; in.file_len = in.total;
    toc_pv_off = in.pv_off; toc_packs_off = in.packs_off; toc_total = in.total;
    toc_index(&in);
}
static void syn_hash_to_row(uint32_t k, const char *name, uint32_t cmds, uint32_t cyc, uint32_t np, uint32_t slot, uint32_t flags)
{
    syn_row(k, slot < SHIPPED_METERS_N ? shipped_meter_hash[slot] : 0x10000000u + k, name, flags, cmds, cyc, np, 1u, MTR_PACK_ABI, 0u);
}
static uint32_t dyn_load_files(int n, char **files)
{
    uint32_t up = 0, top = 0; static char names[8][MD_MAX_STR + 1u]; static uint32_t cm[8], cy[8], np[8];
    for (uint32_t k = 0; k < 8u; k++) { names[k][0] = 0; desc_len_by_row[k] = 0u; }
    for (int a = 0; a < n; a++) {
        FILE *f = fopen(files[a], "rb"); static uint8_t desc[4096]; const uint32_t len = f ? (uint32_t)fread(desc, 1, sizeof desc, f) : 0u; if (f) fclose(f);
        if (len < 40u) continue;
        const uint32_t slot = (uint32_t)desc[34] | ((uint32_t)desc[35] << 8);
        if (slot < METER_N || slot >= METER_SLOTS) continue;
        memcpy(desc_by_row[slot], desc, len); desc_len_by_row[slot] = len;
        dyn_buf_t c = { desc_by_row[slot], len }; md_hdr_t h;
        if (!md_header(dyn_buf_rd, &c, len, &h) || md_string(dyn_buf_rd, &c, &h, h.name, names[slot], MD_MAX_STR + 1u) < 0) { desc_len_by_row[slot] = 0u; continue; }
        cm[slot] = h.cmds; cy[slot] = h.max_cycles; np[slot] = h.np;
        if (slot + 1u > top) top = slot + 1u;
        up++;
    }
    if (top < METER_N) top = METER_N;
    syn_row(0u, shipped_meter_hash[0], "WINAMP BARS", MB_F_VISIBLE | MB_F_FALLBACK, 64u, 200000u, 8u, 1u, MTR_PACK_ABI, 0u);
    for (uint32_t k = METER_N; k < top; k++) syn_hash_to_row(k, desc_len_by_row[k] ? names[k] : "HIDDEN", cm[k], cy[k], np[k], k, desc_len_by_row[k] ? MB_F_VISIBLE : 0u);
    syn_install(top);
    return up;
}
