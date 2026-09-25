/* ==================================================================== HELIOS (UI display list, H1)
 * docs/HELIOS_SPEC.md section 4/9. A flat, fixed-size, per-frame display list: each named UI region
 * registers a redraw callback once and marks itself dirty from wherever it changes state today;
 * helios_flush() drains every dirty region into its real fb_*() draw call at one point per
 * main-loop pass, instead of each call site drawing immediately the instant its state changes.
 *
 * Deliberately INERT in this first slice: nothing calls helios_region_register()/helios_mark_dirty()
 * yet, so helios_flush()'s loop never has anything to do -- this proves the plumbing compiles and
 * costs nothing (one static array, one tiny loop over it) before any real screen is converted,
 * matching this project's own "prove the primitive first" discipline (RRECT_READY()/BLIT_READY()
 * existed before they had callers too). Converting a real screen's draw call sites to route through
 * this is the next step, held until H0's vblank MMIO itself is hardware-confirmed (B-239's fit is
 * not yet installed) -- there is no point tuning a flush timing point around a signal that has never
 * been proven to toggle on real hardware yet.
 *
 * vblank_active() is a plain MMIO read, not a probe: unlike RRECT_READY()/BLIT_READY() (which guard
 * a WRITE that could silently decode as a different opcode on old RTL), R_VBLANK is just a status
 * bit that reads 0 on any bitstream built without TAU_VBLANK (same convention as R_SDR_BUSY) -- safe
 * to read unconditionally, no probe needed. helios_flush() never spins or blocks on it: with
 * vblank_active() always 0 (today's installed bitstream), every dirty region simply redraws on the
 * very next main-loop pass, which is bit-for-bit today's existing immediate-mode behavior -- so
 * converting a call site to mark-dirty-then-flush cannot regress anything before H0 is proven, and
 * a full frame-accurate flush point (section 4's "begin right at vblank") is a real subsequent step,
 * not this one.
 */

#define HELIOS_MAX_REGIONS 16u

/* ---- beam-aware drawing (B-267) ------------------------------------------------------------------------------------
 * The scanout reads each row from SDRAM ONE LINE BEFORE it is shown, and moves down one row every ~41.7 us. Drawing a
 * region while the beam is inside it shows half old, half new content for a frame: a tear. A region is safe to draw when
 *   (a) the beam is in vertical blanking (nothing is being read),
 *   (b) the beam has already passed it (it shows the finished picture on the NEXT frame), or
 *   (c) it is entirely AHEAD of the beam by more than the prefetch line plus a small lead (the draw engine writes about
 *       8x faster than the beam moves, so a region that starts ahead stays ahead until it is done).
 * Anything else waits for a later pass. The video line counter comes from R_SCAN (tau_cdc_gray_bus.sv); VOFF and V_ACT
 * mirror src/fpga/core/mp3_fb.sv (VOFF 4, V_ACT 360) and must be kept in step with it. Without a TAU_BEAM bitstream
 * (R_SCAN bit 9 clear) everything is safe, i.e. exactly the old immediate behaviour. */
#define HELIOS_VOFF   4u
#define HELIOS_VACT   360u
#define HELIOS_LEAD   3u          /* lines of margin ahead of the prefetch line */
_Static_assert(HELIOS_VACT == 360u, "HELIOS_VACT mirrors mp3_fb.sv V_ACT and the 360-row framebuffer");

static uint8_t  helios_beam_ok;        /* set once at boot from R_SCAN bit 9 */
static uint32_t helios_drawn_n;        /* rows-safe checks that let a draw through */
static uint32_t helios_deferred_n;     /* rows-safe checks that made a draw wait */

static int helios_rows_safe(uint32_t y0, uint32_t y1)
{
    if (!helios_beam_ok) return 1;
    uint32_t vc = REG(R_SCAN) & 0x1FFu;
    if (vc < HELIOS_VOFF || vc >= HELIOS_VOFF + HELIOS_VACT) return 1;     /* (a) vertical blanking */
    uint32_t by = vc - HELIOS_VOFF;                                          /* row being scanned now */
    if (y1 <= by + 1u) return 1;                                             /* (b) already passed / prefetched */
    if (y0 >= by + 2u + HELIOS_LEAD) return 1;                               /* (c) safely ahead */
    return 0;
}

/* The same, counting outcomes for the Info page's BEAM row. */
static int helios_rows_safe_counted(uint32_t y0, uint32_t y1)
{
    int ok = helios_rows_safe(y0, y1);
    if (ok) helios_drawn_n++; else helios_deferred_n++;
    return ok;
}

typedef void (*helios_redraw_fn)(void);

static struct {
    helios_redraw_fn redraw;
    uint8_t dirty;
    uint16_t y0, y1;        /* rows the region redraws; y1 == 0xFFFF means "whole screen / draw immediately" */
} helios_region[HELIOS_MAX_REGIONS];

static uint8_t helios_region_n;

static uint32_t vblank_active(void) { return REG(R_VBLANK) & 1u; }

/* Setup-time only (once per region, at UI init) -- never called per frame, so the linear scan over
 * HELIOS_MAX_REGIONS this doesn't even need costs nothing worth measuring. */
static uint8_t helios_region_register_rows(helios_redraw_fn fn, uint16_t y0, uint16_t y1)
{
    uint8_t id = helios_region_n;
    if (id >= HELIOS_MAX_REGIONS) return 0xFFu;   /* out of slots: caller's redraw stays unrouted, never a crash */
    helios_region[id].redraw = fn;
    helios_region[id].dirty  = 0u;
    helios_region[id].y0 = y0; helios_region[id].y1 = y1;
    helios_region_n = (uint8_t)(id + 1u);
    return id;
}

/* A region with no stated extent (the full-screen chrome) draws immediately: a full repaint takes tens of ms against a
 * 1.7 ms blanking interval, so waiting for a safe window would only delay it; tearing on full repaints needs double
 * buffering (HELIOS_SPEC.md section 5, H2). */
static uint8_t helios_region_register(helios_redraw_fn fn) { return helios_region_register_rows(fn, 0u, 0xFFFFu); }

static void helios_mark_dirty(uint8_t id)
{
    if (id < helios_region_n) helios_region[id].dirty = 1u;
}

/* Drains every dirty region into its real draw call. Bounded by helios_region_n (never the full
 * HELIOS_MAX_REGIONS unless every slot is used), called once per main-loop pass regardless of
 * whether vblank_active() reads anything real yet -- see the file header for why that is safe. */
static void helios_flush(void)
{
    for (uint8_t i = 0; i < helios_region_n; i++) {
        if (helios_region[i].dirty) {
            if (helios_region[i].y1 != 0xFFFFu && !helios_rows_safe_counted(helios_region[i].y0, helios_region[i].y1))
                continue;                                   /* the beam is in the way: stay dirty, retry next pass */
            helios_region[i].dirty = 0u;
            helios_region[i].redraw();
        }
    }
}
