/* Reads tau-assets.bin (data slot 8) once at boot and adds its themes after the built-ins (theme step 0d, docs/THEME_FILE_FORMAT.md).
 * Cold code. No file is not an error (a core without the slot fails the read, exactly like the library index); a bad file leaves the
 * built-in themes and leaves a code for Info. The parsing and every check are in assets_core.h, tested on the host. */
#include "assets_core.h"

#define ASSETS_SLOT_ID 8u

COLD_FN static void th_assets_load(void)
{
    th_file_n = 0; th_file_err = 0;
    if (!target_read_slot(ASSETS_SLOT_ID, 0u, TAG_OFF, 12u)) return;              /* no file: built-ins only, not an error */
    if (TAG_SIZE < 1024u) { th_file_err = AS_E_SIZE; return; }
    if (tagbuf[0] != 'T' || tagbuf[1] != 'A' || tagbuf[2] != 'U' || tagbuf[3] != 'A') { th_file_err = AS_E_MAGIC; return; }
    /* Learn the size from the header and its section table, then read the whole file in one go. */
    uint32_t sections = lib_ld16(tagbuf + 6), hdr = 12u + 16u * sections;
    if (sections > AS_MAX_SECTIONS) { th_file_err = AS_E_SIZE; return; }
    if (!target_read_slot(ASSETS_SLOT_ID, 0u, TAG_OFF, hdr)) { th_file_err = AS_E_READ; return; }
    uint32_t total = as_total_size(tagbuf, hdr);
    if (!total) { th_file_err = AS_E_SIZE; return; }
    if (total > 1024u || total > TAG_SIZE) { th_file_err = AS_E_SIZE; return; }
    if (!target_read_slot(ASSETS_SLOT_ID, 0u, TAG_OFF, total)) { th_file_err = AS_E_READ; return; }
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
