/* The core's own meter-pack bundle, data slot 9 (tau-packs.bin, shipped in the release next to tau.rom and tau-cold.bin: core-specific, built with the firmware so its packs always match its ABI). Read through
 * the slot driver in small pieces and installed straight into the PSRAM pack slots (ui/meters.c meters_packs_load_file): no staging copy. */
#ifndef P2_PACKFILE_H
#define P2_PACKFILE_H
#include <stdint.h>
int packfile_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len);       /* a lib_read_fn over the slot; 0 when the file is not there or the read fails (also past its end) */
#endif
