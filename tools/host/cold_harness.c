/* Runs fw/cold_core.h under tools/rv32sim.py against a cold image file (the simulator's data file). Prints the load result and a hash of the copied bytes. */
#include "hostio.h"
#include "../../fw/cold_core.h"

#ifndef WANT_SIZE
#define WANT_SIZE 0u
#endif
#ifndef WANT_ID
#define WANT_ID 0u
#endif

static uint8_t win[LIB_WIN] __attribute__((aligned(16)));
static uint8_t dst[1u << 16] __attribute__((aligned(16)));

static int rd(void *ctx, uint32_t off, uint8_t *d, uint32_t len)
{
    (void)ctx;
    if (off + len > hfilesize()) return 0;
    return hread(off, d, len) == len;
}

int main(void)
{
    uint32_t size = WANT_SIZE ? WANT_SIZE : 0u;
    /* the header carries what the ROM would compare with; the test passes the truth through the two macros */
    int e = cold_load(rd, 0, win, dst, sizeof(dst), size, WANT_ID);
    hputs("COLD E"); hputu((uint32_t)e); hnl();
    if (!e) {
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < size; i++) { h ^= dst[i]; h *= 16777619u; }
        hputs("HASH "); hputx(h); hnl();
    }
    return 0;
}
