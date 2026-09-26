/* Reads tau-assets.bin (data slot 8) once at boot: extra themes (THEM, theme step 0d) and per-meter preset sets (METR, meter module M4).
 * docs/THEME_FILE_FORMAT.md. Cold code. No file is not an error (a core without the slot fails the read, exactly like the library
 * index); a bad section leaves the built-ins and leaves a code for Info; the two sections fail independently. The parsing and every
 * check are in assets_core.h, tested on the host. */
#include "assets_core.h"

#define ASSETS_SLOT_ID 8u
static uint8_t mf_n, mf_err;      /* METR: meters whose presets were replaced, and why not (0 none, else an AS_E_* code) */

COLD_FN static void th_assets_themes(uint32_t total)
{
    uint32_t off, n, k = 0;
    int e = as_find(tagbuf, total, "THEM", &off, &n);
    if (e == AS_E_NOTHEME) return;                                                 /* a container without themes: fine */
    if (e) { th_file_err = (uint8_t)e; return; }
    as_theme_t meta[TH_FILE_MAX];
    uint16_t roles[TH_FILE_MAX * 2u * TR_COUNT];                                   /* cold function: stack, not RAM */
    for (uint32_t i = 0; i < TH_FILE_MAX; i++)                                     /* roles the file does not carry: theme 0 */
        for (uint32_t p = 0; p < 2u; p++)
            for (uint32_t r = 0; r < TR_COUNT; r++) roles[(i * 2u + p) * TR_COUNT + r] = th_themes[0].role[p][r];
    e = as_themes(&tagbuf[off], n, TR_COUNT, TH_FILE_MAX, meta, roles, TR_COUNT, &k);
    if (e) { th_file_err = (uint8_t)e; return; }
    for (uint32_t i = 0; i < k; i++) {
        for (uint32_t j = 0; j < 16u; j++) th_file_name[i][j] = meta[i].name[j];
        th_file[i].name = th_file_name[i];
        for (uint32_t p = 0; p < 2u; p++) {
            th_file[i].bg_luma[p] = meta[i].luma[p];
            for (uint32_t r = 0; r < TR_COUNT; r++) th_file[i].role[p][r] = roles[(i * 2u + p) * TR_COUNT + r];
        }
    }
    th_file_n = (uint8_t)k;
}

COLD_FN static void th_assets_meters(uint32_t total)
{
    uint32_t off, n, done = 0;
    int e = as_find(tagbuf, total, "METR", &off, &n);
    if (e == AS_E_NOTHEME) return;                                                 /* no METR section: built-in presets */
    if (e) { mf_err = (uint8_t)e; return; }
    e = as_metr(&tagbuf[off], n, mtr_modules, MTR_MODULE_N, &done);
    if (e) { mf_err = (uint8_t)e; return; }
    mf_n = (uint8_t)done;
}

COLD_FN static void th_assets_load(void)
{
    th_file_n = 0; th_file_err = 0; mf_n = 0; mf_err = 0;
    if (!target_read_slot(ASSETS_SLOT_ID, 0u, TAG_OFF, 12u)) return;              /* no file: built-ins only, not an error */
    if (TAG_SIZE < 1024u) { th_file_err = mf_err = AS_E_SIZE; return; }
    if (tagbuf[0] != 'T' || tagbuf[1] != 'A' || tagbuf[2] != 'U' || tagbuf[3] != 'A') { th_file_err = mf_err = AS_E_MAGIC; return; }
    /* Learn the size from the header and its section table, then read the whole file in one go. */
    uint32_t sections = lib_ld16(tagbuf + 6), hdr = 12u + 16u * sections;
    if (sections > AS_MAX_SECTIONS) { th_file_err = mf_err = AS_E_SIZE; return; }
    if (!target_read_slot(ASSETS_SLOT_ID, 0u, TAG_OFF, hdr)) { th_file_err = mf_err = AS_E_READ; return; }
    uint32_t total = as_total_size(tagbuf, hdr);
    if (!total || total > 1024u || total > TAG_SIZE) { th_file_err = mf_err = AS_E_SIZE; return; }
    if (!target_read_slot(ASSETS_SLOT_ID, 0u, TAG_OFF, total)) { th_file_err = mf_err = AS_E_READ; return; }
    th_assets_themes(total);
    th_assets_meters(total);
}
