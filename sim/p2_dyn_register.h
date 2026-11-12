#include <string.h>
/* For the host harnesses that include fw2/ui/meters.c with P2_PACKS and no bin of packs (define MTR_TEST_WINDOW_LOAD, PACK_WINDOW_DATA = win_mem, MTR_TOC_BASE/MTR_PV_BASE host buffers BEFORE the include): registers rows from
 * descriptor files (what tools/player2/meter_desc.py builds) as if the TOC listed them -- row = the descriptor's manifest slot, rows in between absent -- and provides the window loader: selecting a row puts "its pack" in
 * the window (a 64-byte stub whose first word is the row number, then the descriptor as the image's last section), so the real dyn_register, tick dispatch, strikes and tuning run on it. Include AFTER meters.c. */
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
    win_row = m; pack_act = m; ps_reset(&win_strike); mwin_loads++;
    return MPK_OK;
}
static void dyn_row_synth(uint32_t slot, const char *name, uint32_t cmds, uint32_t cyc, uint32_t np, uint32_t mid)
{
    mb_row_t r; memset(&r, 0, sizeof r);
    r.id_hash = slot ? 0x10000000u + slot : 0x7DDDFE63u; r.cmds = (uint16_t)cmds; r.max_cycles = cyc; r.nparams = (uint16_t)np; r.abi = MTR_PACK_ABI; r.params_rev = 1u; r.preview_off = 0u; r.preview_len = 0u;
    strncpy(r.name, name, sizeof r.name - 1u); r.pack_size = 64u;
    dyn_row_from_toc(slot, &r); dyn[slot].mid = (uint16_t)mid; dyn[slot].ok = 1u;
}
static uint32_t dyn_load_files(int n, char **files)
{
    uint32_t up = 0;
    mtoc_state = 1u; fb_row = 0u; dyn_row_synth(0u, "WINAMP BARS", 64u, 200000u, 8u, 0u); meter_count = METER_N;
    for (int a = 0; a < n; a++) {
        FILE *f = fopen(files[a], "rb"); static uint8_t desc[4096]; const uint32_t len = f ? (uint32_t)fread(desc, 1, sizeof desc, f) : 0u; if (f) fclose(f);
        if (len < 40u) continue;
        const uint32_t slot = (uint32_t)desc[34] | ((uint32_t)desc[35] << 8);
        if (slot < METER_N || slot >= METER_SLOTS) continue;
        memcpy(desc_by_row[slot], desc, len); desc_len_by_row[slot] = len;
        dyn_buf_t c = { desc_by_row[slot], len }; md_hdr_t h; char nm[MD_MAX_STR + 1u];
        if (!md_header(dyn_buf_rd, &c, len, &h) || md_string(dyn_buf_rd, &c, &h, h.name, nm, sizeof nm) < 0) continue;
        dyn_row_synth(slot, nm, h.cmds, h.max_cycles, h.np, h.mid);
        if (slot + 1u > meter_count) meter_count = slot + 1u;
        up++;
    }
    return up;
}
