#pragma GCC optimize ("Os")
#include "theme.h"
#define AS_FN static
#include "../../fw/assets_core.h"

/* The THEM section of tau-assets.bin (docs/THEME_FILE_FORMAT.md): up to four extra themes, validated whole by as_themes (CRCs, versions, sizes, ranges); any failure means the built-in themes only and a code for Info. */
static char file_name[TH_FILE_MAX][16]; static uint8_t file_luma[TH_FILE_MAX][2]; static uint16_t file_role[TH_FILE_MAX][2][TR_COUNT];
const char *theme_file_name(uint32_t i) { return i < th_file_n ? file_name[i] : "?"; }
uint16_t theme_file_role(uint32_t i, uint32_t p, uint32_t r) { return i < th_file_n ? file_role[i][p & 1u][r] : theme_builtin_role(p, r); }
uint32_t theme_file_luma(uint32_t i, uint32_t p) { return i < th_file_n ? file_luma[i][p & 1u] : 45u; }

void theme_file_load(const uint8_t *base, uint32_t total)
{
    uint32_t off, n, k = 0;
    th_file_n = 0; th_file_err = 0;
    int e = as_find(base, total, "THEM", &off, &n);
    if (e == AS_E_NOTHEME) return;                                                 /* a container without themes: fine */
    if (e) { th_file_err = (uint8_t)e; return; }
    as_theme_t meta[TH_FILE_MAX];
    uint16_t roles[TH_FILE_MAX * 2u * TR_COUNT];
    for (uint32_t i = 0; i < TH_FILE_MAX; i++)                                     /* roles the file does not carry: built-in theme 0 */
        for (uint32_t p = 0; p < 2u; p++)
            for (uint32_t r = 0; r < TR_COUNT; r++) roles[(i * 2u + p) * TR_COUNT + r] = theme_builtin_role(p, r);
    e = as_themes(&base[off], n, TR_COUNT, TH_FILE_MAX, meta, roles, TR_COUNT, &k);
    if (e) { th_file_err = (uint8_t)e; return; }
    for (uint32_t i = 0; i < k; i++) {
        for (uint32_t j = 0; j < 16u; j++) file_name[i][j] = meta[i].name[j];
        for (uint32_t p = 0; p < 2u; p++) {
            file_luma[i][p] = meta[i].luma[p];
            for (uint32_t r = 0; r < TR_COUNT; r++) file_role[i][p][r] = roles[(i * 2u + p) * TR_COUNT + r];
        }
    }
    th_file_n = (uint8_t)k;
}
