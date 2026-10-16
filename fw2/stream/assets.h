/* Player 2 tau-assets.bin (data slot 8): the optional file Tau Omega writes with the user's own extras (Halcyon presets PRST; later themes THEM and meter presets METR). Read once at boot, validated by
 * fw/assets_core.h (every CRC, the version and every size before anything is used); no file is not an error; a bad section leaves the built-ins and a code for Info. */
#ifndef P2_ASSETS_H
#define P2_ASSETS_H
#include <stdint.h>
extern uint8_t assets_state;      /* 0 not tried, 1 no file, 2 loaded, 3 error */
extern uint32_t assets_err;       /* AS_E_* of the load (0 none) */
extern uint32_t assets_bytes;     /* size of the file when loaded */
void assets_load(void);           /* at boot, after the cold image and the library */
#endif
