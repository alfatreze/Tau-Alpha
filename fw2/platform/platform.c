#include "platform.h"

caps_t caps;

extern char _image_end, _tag_start, _stack_top, _stack_size;

/* The package tools read these out of the ROM: the CORE_VERSION list this image accepts, the bitstream features it needs (none yet), and the version field
 * the packagers stamp after the build (tools/tau_version.py). Their exact text is a contract with tools/check_fw_bitstream_pair.py and tools/tau_version.py. */
static const char p2_pair_marker[] __attribute__((used, retain)) = "TAUFWPAIR:4D50331B;";
static const char p2_need_marker[] __attribute__((used, retain)) = "TAUFWNEED:FONT;";
const char p2_ver_field[48] __attribute__((used, retain)) = "TAUVER:0.6.0";

void platform_probe(void)
{
    caps.core_version = REG(R_VERSION);
    caps.ring    = (uint8_t)(REG(R_RING_CTRL) >> 31);
    caps.under   = (uint8_t)(REG(R_UNDER) >> 31);                 /* the hardware underrun record (P6) */
    caps.gain    = (uint8_t)(REG(R_GAIN_CTRL) >> 31);
    caps.cymo    = (uint8_t)(REG(R_CYMO_STATUS) & 1u);
    caps.halcyon = (uint8_t)(REG(R_HAL_CTRL) >> 31);
    caps.poly    = (uint8_t)(REG(R_POLY_ST) & 1u);
    caps.lpc     = (uint8_t)(REG(R_LPC_STATUS) & 1u);
    caps.wave    = (uint8_t)(REG(R_WAVE_ST) & 1u);
    REG_WR(R_STATS_IDX, 0u);
    { const uint32_t st = REG(R_STATS_DATA); caps.stats = (uint8_t)((st >> 16) == 0x5354u && (st & 1u)); }          /* the 'ST' signature: a bitstream without the block must not be mistaken for one */
    caps.spec    = (uint8_t)(REG(R_SPEC_ST) & 1u);
    caps.dbuf    = (uint8_t)(REG(R_DBUF_DISP) >> 31);
    caps.font    = (uint8_t)(REG(R_FONT_CTRL) >> 31);
}

int platform_version_ok(void) { return REG(R_VERSION) == P2_CORE_VERSION; }

void platform_halt_pattern(void)
{
    REG(R_STAT0) = 0xAAAAAAAAu; REG(R_STAT1) = 0x55555555u; REG(R_STAT2) = 0xAAAAAAAAu; REG(R_STAT3) = 0x55555555u;
    for (;;) { }
}

uint32_t platform_stack_peak(void)
{
    const uint32_t *lo = (const uint32_t *)((uintptr_t)&_stack_top - (uintptr_t)&_stack_size), *p = lo, *hi = (const uint32_t *)(uintptr_t)&_stack_top;
    while (p < hi && *p == 0xDEADC0DEu) p++;         /* the first disturbed word from the bottom: everything above it has been used */
    return (uint32_t)((const char *)hi - (const char *)p);
}
uint32_t platform_image_bytes(void) { return (uint32_t)(uintptr_t)&_image_end; }
uint32_t platform_free_bytes(void) { return (uint32_t)((uintptr_t)&_tag_start - (uintptr_t)&_image_end); }
