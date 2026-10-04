/* ===================================================================== METER PACKS (TAU_PACKS, docs/features/meters/METER_PACKS.md)
 * Loadable meter modules. tau-packs.bin (data slot 9, tools/pack_bundle.py) carries meter packs; at boot each is verified and installed into its fixed slot in the PSRAM code window,
 * and a meter whose pack is valid is drawn by the pack instead of the built-in code. Everything is fail-safe: a missing file, a bitstream without PSRAM instruction fetch, a pack
 * with a bad header, size, ABI, slot or CRC -- the meter is simply drawn by the built-in code, as if this file did not exist. Only the active meter's working state is resident (the
 * 4 KB scratch at MTR_PACK_SCRATCH_ORG, reserved in fw/link.ld); a pack is activated when its meter becomes the one on screen. */
#include "meter_pack_core.h"

#define PACKS_SLOT_ID 9u
extern char __meter_scratch_start, __meter_scratch_end;
static mpkb_slot_t packs_slot[MTR_PACK_SLOTS];
static uint8_t  packs_state;        /* 0 not tried, 1 installed (some or all), 2 off (see packs_err) */
static uint8_t  packs_err;          /* why the whole feature is off, or 0 */
static uint8_t  packs_seen;         /* packs found in the file */
static uint8_t  packs_active = 0xFFu;   /* the slot whose working state is in the scratch right now */
static uint32_t packs_ms;
static uint32_t packs_stats[MTR_PACK_SLOTS][MTR_PACK_STATS];     /* what each pack publishes for Info */
static mtr_host_api_t packs_api[MTR_PACK_SLOTS];     /* one per slot: the same services, that meter's own setting values */
static uint32_t packs_seen_n;
static uint8_t  packs_grad;         /* the HM_GRAD flag of the draw in progress, for the pack (host table field grad) */
COLD_FN3 static uint32_t packs_cycles(void) { return cycles(); }
COLD_FN3 static int packs_psram_ready(void) { return mtr_psram_ready(); }
COLD_FN3 static void packs_bg_restore(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { ui_bg_restore(x, y, w, h); }
COLD_FN3 static int packs_bg_blend(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t a) { return ui_bg_blend(x, y, w, h, a); }
COLD_FN3 static void packs_scope_note(int ok, uint32_t x, uint32_t y) { SCOPE_NOTE(ok, x, y); }
COLD_FN3 static int packs_held(void) { return FB_HELD(); }
COLD_FN3 static int packs_afford(void) { return meter_afford(); }
COLD_FN3 static uint32_t packs_cpu_pct(void) { return ui_cpu_pct(); }
COLD_FN3 static void packs_toast(const char *m) { ui_toast_msg(m); }
COLD_FN3 static int packs_mb_write(uint32_t a, uint32_t d) { return chl_mb_write(a, d); }
COLD_FN3 static int packs_mb_read(uint32_t a, uint32_t *o) { return chl_mb_read(a, o); }
COLD_FN3 static void packs_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { fb_blit(sx, sy, dx, dy, w, h); }
COLD_FN3 static void packs_sblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t a, uint32_t b) { fb_sblit(sx, sy, dx, dy, w, h, a, b); }
COLD_FN3 static void packs_wait(void) { fb_wait(); }
COLD_FN3 static void packs_fence(void) { fb_fence(); }
COLD_FN3 static void packs_set_bases(uint32_t s, uint32_t d) { fb_set_bases(s, d); }
COLD_FN3 static void packs_set_color(uint16_t fg, uint16_t bg) { fb_set_color(fg, bg); }
COLD_FN3 static void packs_ch(uint32_t x, uint32_t y, char c, uint32_t sx, uint32_t sy) { fb_char(x, y, c, sx, sy); }
COLD_FN3 static uint32_t packs_text_clipped(uint32_t x, uint32_t y, const char *s, uint32_t sx, uint32_t sy, uint32_t w) { return fb_text_clipped(x, y, s, sx, sy, w); }
COLD_FN3 static char *packs_dec(char *p, uint32_t v) { return ui_dec(p, v); }
COLD_FN3 static uint32_t packs_cell(uint32_t size) { return FB_CELL(size); }
COLD_FN3 static int packs_bar_ready(void) { blit_probe_ensure(); return BLIT_READY(); }   /* the bar opcode exists on this bitstream (probed once) */

#define PACKS_E_NOFETCH 40u         /* cold code (PSRAM instruction fetch) is not proven: a pack could not run */
#define PACKS_E_SCRATCH 41u         /* the linked scratch is not where the ABI says */
#define PACKS_E_PSRAM   42u         /* the slot area failed its read-back probe */

COLD_FN3 static volatile uint8_t *packs_slot_data(uint32_t i) { return (volatile uint8_t *)(0xA4800000u + (MTR_PACK_SLOT_BASE - 0x24800000u) + i * MTR_PACK_SLOT_SIZE); }

COLD_FN3 static int packs_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx; (void)dst;
    return target_read_slot(PACKS_SLOT_ID, off, TAG_OFF, len);
}

COLD_FN3 static void packs_boot_load_cold(void)
{
    packs_state = 2u; packs_err = 0u;
    if (!cold_code_ok) { packs_err = PACKS_E_NOFETCH; return; }
    if ((uint32_t)(uintptr_t)&__meter_scratch_start != MTR_PACK_SCRATCH_ORG || (uint32_t)(uintptr_t)&__meter_scratch_end - MTR_PACK_SCRATCH_ORG < MTR_PACK_SCRATCH_SIZE) { packs_err = PACKS_E_SCRATCH; return; }
    if (!target_read_slot(PACKS_SLOT_ID, 0u, TAG_OFF, 1u)) { packs_err = MPK_E_NOFILE; return; }
    if (TAG_SIZE < LIB_WIN) { packs_err = MPK_E_SIZE; return; }
    /* The slot area must read back what is written to it before any code is placed there (the cold image does the same with its own region). */
    volatile uint32_t *p = (volatile uint32_t *)packs_slot_data(0);
    p[0] = 0xA5C33C5Au; p[1] = 0x5A3CC3A5u;
    if (p[0] != 0xA5C33C5Au || p[1] != 0x5A3CC3A5u) { packs_err = PACKS_E_PSRAM; return; }
    const uint32_t t0 = cycles();
    const int e = mpkb_install_all(packs_rd, 0, tagbuf, 0x00FFFFFFu, packs_slot_data(0), MTR_PACK_SCRATCH_ORG, MTR_PACK_SCRATCH_SIZE, packs_slot, &packs_seen_n);
    packs_ms = (cycles() - t0) / (CLK_HZ / 1000u);
    packs_seen = (uint8_t)packs_seen_n;
    if (e) { packs_err = (uint8_t)e; return; }
    for (uint32_t i = 0; i < MTR_PACK_SLOTS; i++) {
        mtr_host_api_t *a = &packs_api[i];
        a->abi = MTR_PACK_ABI;
        a->clk_hz = CLK_HZ;
        a->fb_rect = fb_rect;
        a->rect_clip = fig_rect_fn;
        a->bar_clip = fig_bar_fn;
        a->bar_ready = packs_bar_ready;
        a->cycles = packs_cycles;
        a->psram_ready = packs_psram_ready;
        a->accent = &ui_accent;
        a->role = th_role;
        a->force = &wviz_force;
        a->stats = packs_stats[i];
        a->hs_base = &hs_base; a->held = packs_held; a->afford = packs_afford; a->cpu_pct = packs_cpu_pct; a->toast = packs_toast; a->mb_write = packs_mb_write; a->mb_read = packs_mb_read;
        a->blit = packs_blit; a->sblit = packs_sblit; a->wait = packs_wait; a->fence = packs_fence; a->set_bases = packs_set_bases;
        a->set_color = packs_set_color; a->ch = packs_ch; a->text_clipped = packs_text_clipped; a->dec = packs_dec; a->cell = packs_cell;
        a->grad = &packs_grad;
        a->fullscreen = &ui_fullscreen;
        a->bg_restore = packs_bg_restore;
        a->bg_blend = packs_bg_blend;
        a->scope_note = packs_scope_note;
        a->params = i == 0u ? mtr_v_layered_wave : i == 1u ? mtr_v_winamp_bars : i == 2u ? mtr_v_winamp_scope : i == 3u ? mtr_v_vu_master : i == 4u ? mtr_v_chladni : mtr_v_layered_wave;   /* slot 0 = Layered Wave, 1 = Winamp Bars, 2 = Winamp Scope, 3 = VU Master, 4 = Chladni (fw/meter_pack.h mtr_pack_slot_of) */
    }
    packs_state = 1u;
}

COLD_FN3 static int packs_have(uint32_t meter_id)
{
    const int s = mtr_pack_slot_of(meter_id);
    return packs_state == 1u && s >= 0 && packs_slot[s].status == MPK_OK;
}

COLD_FN3 static uint32_t packs_tick(uint32_t meter_id, const mtr_in_t *in)
{
    const uint32_t s = (uint32_t)mtr_pack_slot_of(meter_id);
    if (packs_active != s) {                                  /* this meter just became the active one: give it a clean working state */
        mpk_activate(&packs_slot[s].info, packs_slot_data(s), (volatile uint8_t *)&__meter_scratch_start);
        packs_active = (uint8_t)s;
    }
    const uint32_t r = ((mtr_pack_entry_fn)packs_slot[s].info.entry)(in, &packs_api[s]);
    if (meter_id == MTR_PACK_METER_LAYERED_WAVE) {          /* Info > LW COST and METER DRAW read the built-in names: hand them the pack's figures */
        lw_cmd_last = packs_stats[s][0]; lw_t_cpu = packs_stats[s][1]; lw_t_unw = packs_stats[s][2]; lw_stride = (uint8_t)packs_stats[s][3];
    }
    return r;
}

/* The one hot piece: called at boot right after cold_boot_load(), it only starts the real work if cold code (the PSRAM instruction window) is proven. */
static void packs_boot_load(void)
{
    packs_state = 2u; packs_err = PACKS_E_NOFETCH;
    if (cold_code_ok) packs_boot_load_cold();
}
