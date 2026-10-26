/* Reads tau-assets.bin (data slot 8) once at boot: extra themes (THEM, theme step 0d) and per-meter preset sets (METR, meter module M4).
 * docs/THEME_FILE_FORMAT.md. Cold code. No file is not an error (a core without the slot fails the read, exactly like the library
 * index); a bad section leaves the built-ins and leaves a code for Info; the two sections fail independently. The parsing and every
 * check are in assets_core.h, tested on the host. */
#include "assets_core.h"

#define ASSETS_SLOT_ID 8u
static uint8_t mf_n, mf_err;      /* METR: meters whose presets were replaced, and why not (0 none, else an AS_E_* code) */

COLD_FN static void th_assets_themes(const uint8_t *base, uint32_t total)
{
    uint32_t off, n, k = 0;
    int e = as_find(base, total, "THEM", &off, &n);
    if (e == AS_E_NOTHEME) return;                                                 /* a container without themes: fine */
    if (e) { th_file_err = (uint8_t)e; return; }
    as_theme_t meta[TH_FILE_MAX];
    uint16_t roles[TH_FILE_MAX * 2u * TR_COUNT];                                   /* cold function: stack, not RAM */
    for (uint32_t i = 0; i < TH_FILE_MAX; i++)                                     /* roles the file does not carry: theme 0 */
        for (uint32_t p = 0; p < 2u; p++)
            for (uint32_t r = 0; r < TR_COUNT; r++) roles[(i * 2u + p) * TR_COUNT + r] = th_themes[0].role[p][r];
    e = as_themes(&base[off], n, TR_COUNT, TH_FILE_MAX, meta, roles, TR_COUNT, &k);
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

COLD_FN static void th_assets_meters(const uint8_t *base, uint32_t total)
{
    uint32_t off, n, done = 0;
    int e = as_find(base, total, "METR", &off, &n);
    if (e == AS_E_NOTHEME) return;                                                 /* no METR section: built-in presets */
    if (e) { mf_err = (uint8_t)e; return; }
    e = as_metr(&base[off], n, mtr_modules, MTR_MODULE_N, &done);
    if (e) { mf_err = (uint8_t)e; return; }
    mf_n = (uint8_t)done;
}

/* B-628: files larger than one 4 KiB window are staged in PSRAM (0xA4520000, 64 KiB, docs/features/MEDIA_LIBRARY_0.4_SPEC.md section 4); the window is proven first, like the cold image's. */
#define ASSETS_PSRAM_BASE 0xA4520000u
COLD_FN static int assets_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx; (void)dst;
    return target_read_slot(ASSETS_SLOT_ID, off, TAG_OFF, len);
}
COLD_FN static int assets_psram_ok(void)
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG(0x80000088u) != 0x50535231u) return 0;                 /* PSRAM bus present */
    if (!((REG(0x800000A0u) >> 16) & 1u)) return 0;
    volatile uint32_t *w0 = (volatile uint32_t *)(uintptr_t)ASSETS_PSRAM_BASE;
    volatile uint32_t *w1 = (volatile uint32_t *)(uintptr_t)(ASSETS_PSRAM_BASE + AS_MAX_FILE - 4u);
    for (uint32_t k = 0; k < 2u; k++) {
        *w0 = pat[k]; *w1 = ~pat[k];
        if (*w0 != pat[k] || *w1 != ~pat[k]) return 0;
    }
    return 1;
}

COLD_FN static void th_assets_load(void)
{
    th_file_n = 0; th_file_err = 0; mf_n = 0; mf_err = 0;
    if (TAG_SIZE < LIB_WIN) {                                                      /* the tag buffer must be one whole window (it is, 4 KiB: link.ld _tag_size); with no file this is silent as before */
        if (!target_read_slot(ASSETS_SLOT_ID, 0u, TAG_OFF, 12u)) return;
        th_file_err = mf_err = AS_E_SIZE; return;
    }
    /* A file that fits one window is read exactly as before. A larger one needs PSRAM: prove it only then, so a bitstream without PSRAM still runs every small file. */
    const uint8_t *base = 0; uint32_t total = 0;
    int e = as_load(assets_rd, 0, tagbuf, (volatile uint8_t *)(uintptr_t)ASSETS_PSRAM_BASE, AS_MAX_FILE, assets_psram_ok, &base, &total);
    if (e == AS_NOFILE) return;                                                    /* no file: built-ins only, not an error */
    if (e) { th_file_err = mf_err = (uint8_t)e; return; }
    th_assets_themes(base, total);
    th_assets_meters(base, total);
#if TAU_HALCYON_FW
    hal_prst_load(base, total, (const uint8_t *)base == (const uint8_t *)(uintptr_t)ASSETS_PSRAM_BASE, assets_psram_ok);   /* B-641: Halcyon user presets (PRST) */
#endif
}
