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

#define HELIOS_RECT_FN COLD_FN3     /* every caller (helios_fill_excl, the clip wrappers, helios_present) is cold code */
#include "helios_rect.h"

/* ---- exclusion rects: let a fill skip a reserved area instead of drawing over it ------------------------------------
 * Built for the fullscreen CPU% label (fw/fullscreen.inc): its corner glitched because the visualiser drew across it
 * every frame and the label had to repaint on top, which is a beam race (the scanout can pass through that corner
 * between the visualiser's draw and the label's repaint). A meter whose fill is plain fb_rect() calls can instead
 * never touch the reserved area in the first place via helios_fill_excl() below -- geometry only, no MMIO; the actual
 * rect-minus-rect maths is fw/helios_rect.h (host-tested, sim/test_helios_rect.py).
 *
 * Does NOT apply to a meter that fills by BLIT-replicating a computed tile (Chladni's fullscreen path, B-276): a hole
 * there needs the tile-replication geometry itself adjusted, not a post-hoc split of the destination rect, since the
 * source offsets would need to move too. That is separate, larger work; left alone here. */
#define HELIOS_MAX_EXCL 2u
static helios_rect_t helios_excl[HELIOS_MAX_EXCL];
static uint8_t       helios_excl_n;

/* Setup call, or any time the reserved area's geometry changes (e.g. the label's text width). id is 0..HELIOS_MAX_EXCL-1. */
static void helios_exclude_set(uint8_t id, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (id >= HELIOS_MAX_EXCL) return;
    helios_excl[id].x = x; helios_excl[id].y = y; helios_excl[id].w = w; helios_excl[id].h = h;
    if (id >= helios_excl_n) helios_excl_n = (uint8_t)(id + 1u);
}

static void helios_exclude_clear(uint8_t id)
{
    if (id < HELIOS_MAX_EXCL) helios_excl[id].w = 0;   /* zero width/height never overlaps anything */
}

/* Drop-in replacement for fb_rect(x, y, w, h, color): splits the fill around every active exclusion rect instead of
 * drawing over it. Sequential subtraction (each exclusion applied to every piece the previous one produced) -- fine
 * for HELIOS_MAX_EXCL this small; each piece is a plain fb_rect() call, same cost model as the direct call it replaces. */
COLD_FN3 static void helios_fill_excl(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    helios_rect_t cur[8];
    uint8_t n = 1;
    cur[0].x = (int32_t)x; cur[0].y = (int32_t)y; cur[0].w = (int32_t)w; cur[0].h = (int32_t)h;

    for (uint8_t e = 0; e < helios_excl_n; e++) {
        if (helios_excl[e].w <= 0 || helios_excl[e].h <= 0) continue;
        helios_rect_t next[8];
        uint8_t nn = 0;
        for (uint8_t i = 0; i < n; i++) {
            helios_rect_t piece[4];
            int pn = helios_rect_subtract(cur[i].x, cur[i].y, cur[i].w, cur[i].h,
                                           helios_excl[e].x, helios_excl[e].y, helios_excl[e].w, helios_excl[e].h,
                                           piece);
            for (int p = 0; p < pn && nn < 8u; p++) next[nn++] = piece[p];
        }
        for (uint8_t i = 0; i < nn; i++) cur[i] = next[i];
        n = nn;
    }
    for (uint8_t i = 0; i < n; i++)
        fb_rect((uint32_t)cur[i].x, (uint32_t)cur[i].y, (uint32_t)cur[i].w, (uint32_t)cur[i].h, color);
}

/* Clipped figure draws (fig_rect/fig_bar, fw/player.c): while fig_clip_on is set they split around every helios_excl[] area; otherwise they are the plain
 * primitives. The bar needs its own splitter because OP_BAR is "lit rows at the bottom of the rect", which survives a cut of the TOP rows exactly (fewer
 * rows, lit capped to them) but not a cut anywhere else (that case falls back to two clipped rects). */
COLD_FN3 static void helios_clip_bar_in(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg)
{
    for (uint8_t e = 0; e < helios_excl_n; e++) {
        const int32_t ex = helios_excl[e].x, ey = helios_excl[e].y, ew = helios_excl[e].w, eh = helios_excl[e].h;
        if (ew <= 0 || eh <= 0) continue;
        const int32_t x1 = (int32_t)(x + w), y1 = (int32_t)(y + h);
        if (ex >= x1 || ex + ew <= (int32_t)x || ey >= y1 || ey + eh <= (int32_t)y) continue;      /* no overlap */
        const int32_t mx0 = ex > (int32_t)x ? ex : (int32_t)x, mx1 = (ex + ew) < x1 ? (ex + ew) : x1;
        if (mx0 > (int32_t)x) helios_clip_bar_in(x, y, (uint32_t)(mx0 - (int32_t)x), h, lit, fg, bg);               /* left of the exclusion */
        if (mx1 < x1)         helios_clip_bar_in((uint32_t)mx1, y, (uint32_t)(x1 - mx1), h, lit, fg, bg);           /* right of it */
        if (ey <= (int32_t)y) {                                                                                     /* exclusion reaches the top: cut it off */
            const int32_t ny = ey + eh;
            if (ny < y1) { const uint32_t nh = (uint32_t)(y1 - ny); fb_bar((uint32_t)mx0, (uint32_t)ny, (uint32_t)(mx1 - mx0), nh, lit < nh ? lit : nh, fg, bg); }
        } else {                                                                                                    /* anywhere else: two clipped rects */
            const uint32_t unlit = h > lit ? h - lit : 0u, lt = h - unlit;
            helios_fill_excl((uint32_t)mx0, y, (uint32_t)(mx1 - mx0), unlit, bg);
            helios_fill_excl((uint32_t)mx0, y + unlit, (uint32_t)(mx1 - mx0), lt, fg);
        }
        return;
    }
    fb_bar(x, y, w, h, lit, fg, bg);
}

__attribute__((noinline)) COLD_FN3 static void fig_rect_fn(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    if (fig_clip_on) helios_fill_excl(x, y, w, h, color); else fb_rect(x, y, w, h, color);
}
COLD_FN3 static void fig_bar_fn(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg)
{
    if (fig_clip_on) helios_clip_bar_in(x, y, w, h, lit, fg, bg); else fb_bar(x, y, w, h, lit, fg, bg);
}

/* ---- Off-screen compose + present ---------------------------------------------------------------------------------
 * A meter that repaints its whole box (wipe, then every layer) shows the wipe whenever the beam crosses the box before the repaint finishes: measured
 * 83 ms for Layered Wave against a 16.7 ms frame (B-519), so no beam gate can help. Instead the figure is drawn into the idle H2 back buffer, where
 * nothing can see it, and then COPIED onto the displayed buffer with OP_BLIT, which takes about a millisecond -- much faster than the beam -- so the
 * worst case is one seam for one frame, never a flash of background. The copy skips every helios_excl[] rect, so an overlay (the fullscreen CPU% label)
 * is never overwritten and need not be repainted.
 *
 * Use only when nothing else holds the back buffer: not while an overlay is up (Settings uses buffer 1 for its crossfade), not blanked, not with a flip
 * pending. helios_offscreen_ok() says so. RECT-class draws follow R_DBUF_CPU (set it to the back buffer for the compose, put it back after the fence);
 * blit-mode draws (Chladni's SBLIT/BLIT) follow the sticky bases instead. */
#define HELIOS_PRESENT_CHUNK 120u    /* OP_BLIT stages a row in a 128-entry buffer: at most 127 columns, 120 matches chl_replicate */

COLD_FN3 static int helios_offscreen_ok(void)
{
    if (!DBUF_READY() || screen_blank || UI_OVERLAY_UP) return 0;
    return (REG(R_DBUF_DISP) & 2u) == 0u;                     /* a pending flip would swap the roles under us */
}
/* fb_fence() that keeps waiting (up to ~200 ms) until the engine has really drained; returns 0 if it never did. The compose must not switch R_DBUF_CPU
 * back while commands are still queued, or they would execute against the displayed buffer. */
COLD_FN3 static int helios_drain(void)
{
    for (uint32_t i = 0; i < 4u; i++) { fb_fence(); if (!fb_fence_timeout) return 1; }
    return 0;
}
COLD_FN3 static uint32_t helios_front_idx(void) { return REG(R_DBUF_DISP) & 1u; }
COLD_FN3 static uint32_t helios_buf_base(uint32_t idx) { return idx ? DBUF_BASE1_W : 0u; }

/* Copy the box from the back buffer to the displayed one, minus every exclusion rect. Everything composed must have executed (fb_fence) first. */
COLD_FN3 static void helios_present(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    const uint32_t front = helios_front_idx();
    helios_rect_t cur[8];
    uint8_t n = 1;
    cur[0].x = (int32_t)x; cur[0].y = (int32_t)y; cur[0].w = (int32_t)w; cur[0].h = (int32_t)h;
    for (uint8_t e = 0; e < helios_excl_n; e++) {
        if (helios_excl[e].w <= 0 || helios_excl[e].h <= 0) continue;
        helios_rect_t next[8];
        uint8_t nn = 0;
        for (uint8_t i = 0; i < n; i++) {
            helios_rect_t piece[4];
            const int pn = helios_rect_subtract(cur[i].x, cur[i].y, cur[i].w, cur[i].h,
                                                helios_excl[e].x, helios_excl[e].y, helios_excl[e].w, helios_excl[e].h, piece);
            for (int q = 0; q < pn && nn < 8u; q++) next[nn++] = piece[q];
        }
        for (uint8_t i = 0; i < nn; i++) cur[i] = next[i];
        n = nn;
    }
    fb_set_bases(helios_buf_base(front ^ 1u), helios_buf_base(front));
    for (uint8_t i = 0; i < n; i++)
        for (int32_t cx = 0; cx < cur[i].w; cx += (int32_t)HELIOS_PRESENT_CHUNK) {
            const uint32_t c = (uint32_t)(cur[i].w - cx) < HELIOS_PRESENT_CHUNK ? (uint32_t)(cur[i].w - cx) : HELIOS_PRESENT_CHUNK;
            fb_blit((uint32_t)(cur[i].x + cx), (uint32_t)cur[i].y, (uint32_t)(cur[i].x + cx), (uint32_t)cur[i].y, c, (uint32_t)cur[i].h);
        }
    fb_fence();
    fb_set_bases(0u, 0u);
}

/* ---- Wraparound list navigation ---------------------------------------------------------------------------------
 * One step of a 0-based index over `count` positions, wrapping at both ends: UP from position 0 lands on the LAST
 * position, DOWN from the last position lands back on 0. `fw/settingsui.inc`'s own choice lists and plain menu
 * rows (`set_ch_cur[]`, `set_sel[]`) already each hand-wrote this exact pair of expressions independently; this is
 * that one idea named once so a third caller (or a fourth) never needs to re-derive it. Works unchanged for a
 * SCROLL OFFSET too (not just a single selected row) -- pass `maxtop + 1` as `count` and the read-only Info/Stress-
 * Status pages wrap at the last visible window exactly the same way a selection wraps at the last row. */
static uint32_t helios_wrap_index(uint32_t idx, uint32_t count, int up)
{
    if (count == 0u) return 0u;
    return up ? (idx ? idx - 1u : count - 1u) : (idx + 1u) % count;
}

/* ---- View registry (item 4, docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md section 5;
 * design in docs/features/HELIOS_SPEC.md section 11) ----------------------------------------------
 * B-350's helios_view_changed() unified six independent "repaint everything" call sites (formerly a
 * bare `pl_ui_restore = 1u;` at each of fw/settingsui.inc, fw/library.inc, fw/fullscreen.inc x3 and
 * fw/suite.inc x2) behind one name, but its BODY stayed a single flag driving a fixed, hand-maintained
 * checklist in fw/player.c's main loop -- exactly the shape of gap that let Settings' own set_close()
 * omit it for a while (B-349). This replaces that single flag with a bitmask (helios_pending_mask) and
 * a real per-view declared contract (helios_view_t.invalidate_mask), so a future shared cache a view's
 * pixels can touch is a new named bit checked at every call site that builds a view descriptor, not a
 * silent gap in a growing if-chain.
 *
 * Every one of today's 4 top-level views (player, Settings, the library browser, the fullscreen
 * visualiser) is a full-screen affair (HELIOS_SPEC.md section 11's own finding), so every view built
 * below uses HELIOS_INV_ALL -- this slice is byte-identical in BEHAVIOUR to helios_view_changed()
 * before it, just reached through a declared contract instead of an assumed one. A view that does NOT
 * need every bit is the point where this starts saving real work (item 6, later).
 *
 * helios_view_switch() takes BOTH the outgoing and incoming view explicitly, deliberately not the
 * single-argument form HELIOS_SPEC.md section 11 sketched against an implied tracked "current view":
 * every real transition site already knows both ends (fullscreen's own toggle picks its direction from
 * ui_fullscreen's new value; every close path is always "this overlay -> the player"), and requiring
 * every future view's OPEN site to also start calling this just to keep a global tracker correct would
 * add a new way to silently regress this exact mechanism -- the same failure class the mechanism itself
 * exists to close. Revisit this choice if a future view genuinely needs one to be inferred.
 *
 * The actual invalidation ACTIONS (ui_chrome_paint(), ui_art_draw(), the wave/spec staleness arrays,
 * ...) are NOT here: they are declared later in fw/player.c (after this file's #include point), the
 * same reason B-350's pl_ui_restore flag was read and acted on there instead of in this file. This file
 * only accumulates which bits are pending; fw/player.c's main loop applies them once per pass. */
#define HELIOS_INV_CHROME   0x01u   /* ui_chrome_paint() / ui_draw_chrome() (itself picks fullscreen vs normal) */
#define HELIOS_INV_ART      0x02u   /* ui_art_draw(), if art is ready and shown */
#define HELIOS_INV_BG_STRIP 0x04u   /* the meters' background strip (columns 400..511), B-256 */
#define HELIOS_INV_WAVE     0x08u   /* wave_drawn[]/wave_pk_drawn[]/ui_wave_force */
#define HELIOS_INV_SPEC     0x10u   /* spec_drawn[] */
#define HELIOS_INV_EXCL     0x20u   /* every exclusion rect (helios_excl[]) -- whoever set it just left */
#define HELIOS_INV_ALL      0x3Fu

typedef struct {
    void (*enter)(void);      /* called once, becoming the active view; NULL if nothing extra to do */
    void (*exit)(void);       /* called once, leaving; owns its OWN cleanup, nothing more; NULL if none */
    uint8_t invalidate_mask;
} helios_view_t;

/* The generic "home" view every overlay/visualiser switches back to. No view-specific enter/exit of
 * its own -- HELIOS_INV_ALL already covers everything returning to the player needs. */
static const helios_view_t helios_player_view = { 0, 0, HELIOS_INV_ALL };

static uint8_t helios_pending_mask;   /* bits accumulated here; fw/player.c's main loop drains it */

/* THE one place a top-level view transition happens: leaves `from` (its exit(), if any), invalidates
 * the union of both views' declared bits, enters `to` (its enter(), if any). Both must be real views --
 * every call site always knows both ends (see the comment above), so this never guards against NULL. */
static void helios_view_switch(const helios_view_t *from, const helios_view_t *to)
{
    if (from->exit) from->exit();
    helios_pending_mask = (uint8_t)(helios_pending_mask | from->invalidate_mask | to->invalidate_mask);
    if (to->enter) to->enter();
}

/* B-350's original meaning, kept for call sites that are NOT a view transition -- the current view's own
 * content changed materially (a meter preset switch while already fullscreen, for example) and just
 * needs a full repaint, not an exit()/enter() pair. */
static void helios_view_changed(void) { helios_pending_mask = HELIOS_INV_ALL; }

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
    uint8_t requires_cold;  /* B-391 follow-up: see helios_region_register_rows_cold() below */
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
    helios_region[id].requires_cold = 0u;
    helios_region_n = (uint8_t)(id + 1u);
    return id;
}

/* B-391 follow-up: helios_flush() calls every region's redraw() through a stored function pointer,
 * which is invisible to tools/check_cold_calls.py (it disassembles the linked ELF for direct jal/tail
 * targets; an indirect jalr through a register has no static target for objdump to resolve at all --
 * not a fixable regex gap, a real limit of static disassembly). B-391 itself relied on an INFORMAL
 * argument for why calling a COLD_FN redraw callback from this unguarded hot function is still safe
 * (the region can only be marked dirty from a COLD_READY()-gated call site, and cold_code_ok is
 * monotonic once set) -- true, but enforced nowhere, unlike RRECT_READY()/BLIT_READY()'s own explicit
 * boundary checks. This variant makes the same fact a real, checked gate instead: a region registered
 * cold-aware is simply left dirty (never called, no crash, exactly the existing beam-unsafe retry
 * path) until COLD_READY() -- and once it is, cold_code_ok's own monotonicity means the check never
 * flips back off, so this costs nothing after the first few frames of boot. */
static uint8_t helios_region_register_rows_cold(helios_redraw_fn fn, uint16_t y0, uint16_t y1)
{
    uint8_t id = helios_region_register_rows(fn, y0, y1);
    if (id != 0xFFu) helios_region[id].requires_cold = 1u;
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
            /* cold_code_ok, not the COLD_READY() macro: that macro is defined in fw/cold.inc, included
             * well after this file (same ordering constraint fw/player.c's own comment above
             * "#include \"cold.inc\"" notes for a different symbol); the raw variable is declared much
             * earlier in fw/player.c and is visible here. */
            if (helios_region[i].requires_cold && !cold_code_ok)
                continue;                                   /* not loaded yet: stay dirty, retry next pass */
            if (helios_region[i].y1 != 0xFFFFu && !helios_rows_safe_counted(helios_region[i].y0, helios_region[i].y1))
                continue;                                   /* the beam is in the way: stay dirty, retry next pass */
            helios_region[i].dirty = 0u;
            helios_region[i].redraw();
        }
    }
}
