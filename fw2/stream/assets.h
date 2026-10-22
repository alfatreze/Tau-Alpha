/* Player 2 tau-assets.bin (data slot 8): the optional file Tau Omega writes with the user's own extras (Halcyon presets PRST; later themes THEM and meter presets METR). Read once at boot, validated by
 * fw/assets_core.h (every CRC, the version and every size before anything is used); no file is not an error; a bad section leaves the built-ins and a code for Info. */
#ifndef P2_ASSETS_H
#define P2_ASSETS_H
#include <stdint.h>
extern uint8_t assets_state;      /* 0 not tried, 1 no file, 2 loaded, 3 error */
extern uint32_t assets_err;       /* AS_E_* of the load (0 none) */
extern uint32_t assets_bytes;     /* size of the file when loaded */
/* What reading the file cost at boot, in cycles of the 66.667 MHz counter (Info BOOT rows; the app fills the section rows): the read and validation, then each owner's handling of its section. */
typedef struct { uint32_t load, themes, prst, packs, presets; } assets_boot_t;
extern assets_boot_t assets_boot;
void assets_load(void);           /* at boot, after the cold image and the library; the app then hands each section to its owner (the stream layer knows none of them) */
const uint8_t *assets_file(uint32_t *total);                          /* the validated file (0 when there is none) */
int  assets_section(const char *tag, const uint8_t **p, uint32_t *n);  /* 1 and the section's bytes when the file carries it */
int  assets_in_psram(void);       /* the file sits in the PSRAM staging area (a larger file), not in the 4 KiB window */
int  assets_psram_ok(void);       /* the PSRAM staging area works in this bitstream */
#endif
