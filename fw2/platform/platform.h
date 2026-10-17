/* Player 2 platform layer: capabilities (one typed struct, probed once), the boot interlock, the version stamp, the stack high-water mark. Spec principle P10. */
#ifndef P2_PLATFORM_H
#define P2_PLATFORM_H
#include <stdint.h>
#include "mmio.h"

typedef struct {
    uint8_t ring, gain, cymo, halcyon, poly, lpc, wave, spec, dbuf, under, font;   /* 1 = the unit is built into this bitstream */
    uint32_t core_version;
} caps_t;

extern caps_t caps;

/* The CORE_VERSION values this image accepts (192 KB + CLK66 + writable font memory bitstream, rev 27). The same text is in the ROM as TAUFWPAIR (platform.c) for the package checks. */
#define P2_CORE_VERSION 0x4D50331Bu

void platform_probe(void);                       /* fills caps */
int  platform_version_ok(void);
void platform_halt_pattern(void) __attribute__((noreturn));   /* the unmistakable interlock failure */
extern const char p2_ver_field[48];              /* "TAUVER:" + the stamped build version (the packagers patch it into the ROM); the version text starts at +7 */
uint32_t platform_stack_peak(void);              /* bytes of the stack ever used */
uint32_t platform_image_bytes(void);
uint32_t platform_free_bytes(void);              /* between the image and the DMA buffers */
#endif
