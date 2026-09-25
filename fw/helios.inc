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

typedef void (*helios_redraw_fn)(void);

static struct {
    helios_redraw_fn redraw;
    uint8_t dirty;
} helios_region[HELIOS_MAX_REGIONS];

static uint8_t helios_region_n;

static uint32_t vblank_active(void) { return REG(R_VBLANK) & 1u; }

/* Setup-time only (once per region, at UI init) -- never called per frame, so the linear scan over
 * HELIOS_MAX_REGIONS this doesn't even need costs nothing worth measuring. */
static uint8_t helios_region_register(helios_redraw_fn fn)
{
    uint8_t id = helios_region_n;
    if (id >= HELIOS_MAX_REGIONS) return 0xFFu;   /* out of slots: caller's redraw stays unrouted, never a crash */
    helios_region[id].redraw = fn;
    helios_region[id].dirty  = 0u;
    helios_region_n = (uint8_t)(id + 1u);
    return id;
}

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
            helios_region[i].dirty = 0u;
            helios_region[i].redraw();
        }
    }
}
