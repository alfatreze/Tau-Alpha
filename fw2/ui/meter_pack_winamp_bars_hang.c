/* TEST PACK for the pack watchdog (docs/features/PACK_WATCHDOG_PLAN.md, W4): Winamp Bars that stops returning. It is a card file only (build: tools/player2/pack_meter.py winamp_bars --source winamp_bars_hang
 * --out hang.tmpk); it is not part of the firmware and not in the shipped pack bundle. It carries Winamp Bars' meter id and slot, so it replaces the Bars pack on a test card.
 *
 * The first HANG_AFTER pictures are the real Bars (the song plays, the meter draws); from then on the tick never returns:
 *   HANG_MODE 0: a plain endless loop (a pack stuck in its own code);
 *   HANG_MODE 1: an endless loop that keeps issuing engine commands through the host table (a pack hung in the middle of drawing: the engine queue is full or busy when the watchdog cuts it off).
 * -DHANG_AFTER=n -DHANG_MODE=m on the pack_meter.py command line (-D). The counter lives in the pack's scratch (.bss), which the loader clears when the pack is activated: a struck pack that is re-armed
 * starts over. */
#ifndef HANG_AFTER
#define HANG_AFTER 200
#endif
#ifndef HANG_MODE
#define HANG_MODE 0
#endif
#define mtr_pack_entry bars_entry
#include "meter_pack_winamp_bars.c"
#undef mtr_pack_entry

static uint32_t hang_ticks;

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    if (hang_ticks < HANG_AFTER) { hang_ticks++; return bars_entry(in, api); }
    g_api = api;
    for (;;) {
#if HANG_MODE == 1
        api->rect(0, 0, 1, 1, 0u);
#else
        __asm__ volatile("" ::: "memory");
#endif
    }
}
