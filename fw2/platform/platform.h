/* Player 2 platform layer: capabilities (one typed struct, probed once), the boot interlock, the version stamp, the stack high-water mark. Spec principle P10. */
#ifndef P2_PLATFORM_H
#define P2_PLATFORM_H
#include <stdint.h>
#include "mmio.h"

typedef struct {
    uint8_t ring, gain, cymo, halcyon, poly, lpc, wave, stats, spec, dbuf, under, font;   /* 1 = the unit is built into this bitstream */
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
/* Pack watchdog (docs/features/PACK_WATCHDOG_PLAN.md): runs fn(arg) with the hardware watchdog armed for `limit` clk cycles and the CPU's timer interrupt open only for that window. Returns 0 when fn returned, 1 when the
 * watchdog cut it off (the caller's registers and stack are back, the engine is reconciled by gfx_recover; the caller strikes the pack). Without a watchdog in the bitstream it just calls fn. Host builds: a plain call
 * (-DWD_HOST_CUT makes the host stub report a cut without running fn, for the strike-path test). */
#ifdef P2_HOST
#ifdef WD_HOST_CUT
static inline int wd_run(uint32_t limit, void (*fn)(const void *), const void *arg) { (void)limit; (void)fn; (void)arg; return 1; }
#else
static inline int wd_run(uint32_t limit, void (*fn)(const void *), const void *arg) { (void)limit; fn(arg); return 0; }
#endif
#else
int wd_run(uint32_t limit, void (*fn)(const void *), const void *arg);
#endif
uint32_t platform_free_bytes(void);              /* between the image and the DMA buffers */
#endif
