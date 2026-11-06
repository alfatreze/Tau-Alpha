/* Host check of the preset files inside the meter host (fw2/ui/meters.c with fw2/ui/mpreset_host.c, B-967): a pack-only meter's descriptor is registered from a file (as if its pack had been installed), the slot driver is a
 * directory on the host (the files under <dir>/tau-presets/<meter id>/), and the scenarios the firmware meets are walked through. Reuses every stub of p2_meter_budget_harness.c.
 *   h DESC_FILE PRESET_DIR [DEGRADE]      DEGRADE: the governor's level while the scenario runs (0 NORMAL)
 * Prints "npre N", then for list positions 0..N-1 "pos P NAME values...", then the outcome of the other scenarios. */
#define MPH_REAL 1                                                       /* the real preset-file code is included below: meters.c must not supply its stand-ins */
#define main budget_main
#include "p2_meter_budget_harness.c"
#undef main
#include <stdio.h>
#include <string.h>
static uint8_t tag_mem[4096]; static uint32_t mph_mem[1100]; char _tag_start;
#define MPH_TAGB ((const volatile uint8_t *)tag_mem)
#define MPH_PSRAM mph_mem
static char dir_root[256]; static uint32_t opens, open_fail_all;      /* degrade_level is the budget harness's: the governor's level */
uint32_t library_root(char *out, uint32_t cap) { (void)cap; out[0] = 0; return 0u + (strcpy(out, "/R/"), 3u); }          /* the real root ends with a slash too */
void mp3play_io_quiesce(void) { }
static char opened[256];
int slot_open_path(uint32_t slot, const char *path) { (void)slot; opens++; if (open_fail_all) return 0; snprintf(opened, sizeof opened, "%s%s", dir_root, path + 2); FILE *f = fopen(opened, "rb"); if (!f) return 0; fclose(f); return 1; }
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst, uint32_t len, uint32_t to) { (void)slot; (void)dst; (void)to; FILE *f = fopen(opened, "rb"); if (!f) return 0; fseek(f, (long)off, SEEK_SET); const uint32_t n = (uint32_t)fread(tag_mem, 1, len, f); fclose(f); return n == len; }
#include "../fw2/ui/mpreset_host.c"
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    strncpy(dir_root, argv[2], sizeof dir_root - 1); if (argc > 3) degrade_level = (uint32_t)atoi(argv[3]);
    caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50);
    char *df[1] = { argv[1] }; dyn_load_files(1, df);
    const uint32_t R = getenv("ROW") ? (uint32_t)atoi(getenv("ROW")) : (uint32_t)METER_N;      /* the pack-only row under test */
    meters_select(R);                                                        /* the meter comes on screen: its index is read */
    printf("npre %u idx_ok %u err %u\n", meters_npre(R), mph_idx_ok, mph_err);
    const meter_def_t *d = meters_def(R);
    for (uint32_t p = 0; p < meters_npre(R); p++) {
        const uint32_t before = mph_reads; meters_preset_select(R, p);
        printf("pos %u sel %u name %s reads +%u vals", p, meters_preset_of(R), meters_preset_name(), mph_reads - before); for (uint32_t i = 0; i < d->np; i++) printf(" %u", d->mv[i]); printf("\n");
    }
    printf("fails %u reads %u\n", mph_fails, mph_reads);
    return 0;
}
