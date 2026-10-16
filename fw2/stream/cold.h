/* Player 2 cold image (spec principle P5, Phase G): the UI, diagnostics, art, library and settings code and their read-only data live in PSRAM and are loaded at boot from tau-cold.bin
 * (data slot 6). Code runs through the instruction alias, data through the PSRAM window. Which objects are cold is decided in fw2/platform/link.ld (by object file), not in the sources. */
#ifndef P2_COLD_H
#define P2_COLD_H
#include <stdint.h>
enum { COLD_BOOT_OK = 0 };
extern uint32_t cold_err, cold_ms, cold_bytes;
int cold_boot(void);                      /* loads, verifies and proves the image; 0 = ready, else an error code (10 no file .. 18 no instruction fetch, 19 probe) */
#endif
