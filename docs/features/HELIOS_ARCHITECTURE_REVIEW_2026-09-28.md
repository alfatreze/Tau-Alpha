# Helios architectural and functional review (2026-09-28)

**Status:** review, not a spec. Written after B-349/B-350/B-351 (the menu-close leftover bug and its
structural root cause) to answer a broader question: what does Helios actually do today, what will the
project need from it next, and where should it evolve to become a robust, reusable UI framework rather than
a name attached to one small mechanism. Recommendations here are proposals; nothing in this document is
built beyond what B-350/B-351 already shipped.

**Method:** read every file that currently implements UI dispatch (`fw/helios.inc`, `fw/helios_rect.h`,
`fw/player.c`'s chrome/dynamic/dispatch code, `fw/settingsui.inc`, `fw/library.inc`, `fw/fullscreen.inc`,
`fw/suite.inc`, `fw/meters_gen.h`) and cross-checked against `docs/features/HELIOS_SPEC.md` and
`docs/features/meters/METER_MODULE_SPEC.md`'s own stated designs, rather than reasoning from the spec
documents alone — several gaps below are "the spec already says to build X" not "X should be invented."

## 1. Executive summary

Helios today is a **12-line function inside a 192-line file** (`fw/helios.inc`): a dirty-flag region
registry with exactly one real user (the player chrome), plus two bolt-on mechanisms built for single
specific bugs (fullscreen's label exclusion rects, B-267's beam-safety gate). B-350/B-351 (this session)
added a fourth: `helios_view_changed()`, a shared call for the four top-level screens' close transitions.

That is a reasonable, working core. It is not yet a framework. The project has, independently and at
different times, built **at least five other component-shaped subsystems** that solve adjacent problems
with their own vocabulary, their own state machines, and no shared base:

1. Helios regions (dirty tracking + beam-safe deferred draw) — 1 user.
2. Helios view transitions (B-350/351) — 4 users, added this session.
3. The meter module's on-device contract (`docs/features/meters/METER_MODULE_SPEC.md` section 3,
   `mtr_in_t`) — **specified in full, never implemented**. What exists instead is a *parameter/preset*
   registry (`fw/meters_gen.h`, generated from `meters/*/meter.json`) for 4 of 11 real meters; the actual
   *draw* dispatch for all 11 is still one 500-line sequential `if (viz_mode == VIZ_x)` chain inside
   `ui_draw_dynamic_cold()`.
4. The Settings "page" system (`fw/settingsui.inc`) — a small declarative table (`set_menu_rows[]`,
   `set_menu_n[]`, `set_menu_title[]`, `set_menu_parent[]`) for plain menu pages, alongside **six ad hoc
   special-cased pages** (Check, Decode Sweep, Blit Test, Meter Sweep, Meter Trace, the Winamp Configurator)
   each with its own hand-written `*_draw()`/`*_input()` pair dispatched by a growing `if (set_page ==
   SET_x_PG)` chain in `set_input()`/`set_draw()` — a second, less consistent "page" mechanism sitting
   right next to the first.
5. `helios_excl[]` exclusion rects — built once, for fullscreen's corner label, generalizable but with
   exactly one caller.

And one RTL capability has **shipped with zero firmware consumer**: Helios H2 double buffering (`TAU_DBUF`,
B-340) closed timing and is on the card in the all6-combined bitstream tested this session — `grep` finds
no reference to its registers (`R_DBUF`/`dbuf_addr`) anywhere in `fw/`. The double-buffering half of
"Helios" that its own spec (section 5) describes does not exist yet.

**The core risk this review is written to name:** every one of these five subsystems was the right,
proportionate amount of engineering for the single feature that motivated it. None of them was designed to
be reused by the next feature. That is exactly the pattern that produced B-349 (a missing one-line call at
one of six copy-pasted sites) — and it will keep producing bugs shaped like B-349, in a new subsystem, every
time the project adds a screen, a meter, or a settings page, unless something explicitly closes the gap
between "works for the feature that built it" and "is a component other features can pick up."

## 2. Current capabilities, concretely

**What `fw/helios.inc` provides today (192 lines, verified by reading it):**

- `helios_region_register(fn)` / `helios_region_register_rows(fn, y0, y1)` — register a redraw callback,
  optionally scoped to a row range for beam-safety checks. **One caller**: `ui_chrome_paint()` registers
  `ui_draw_chrome` as a whole-screen region (`y1 = 0xFFFF`, "draw immediately, no beam wait").
- `helios_mark_dirty(id)` / `helios_flush()` — mark a region dirty, drain dirty regions once per main-loop
  pass. Beam-safety (`helios_rows_safe_counted`) only applies to regions with a real row range; the one
  registered region opts out of it (whole-screen repaints are always "immediate").
- `helios_rows_safe(y0, y1)` / `helios_rows_safe_counted(...)` — the B-267 beam-position rule (blanking /
  already-passed / safely-ahead), gated on `helios_beam_ok` (set once from `R_SCAN` bit 9 at boot, so it is
  a true no-op — old behaviour — on any bitstream without `TAU_BEAM`). **Only consulted by `helios_flush()`
  for row-scoped regions, which today is nobody** (the one real region is whole-screen). Every direct
  `fb_rect`/`fb_char`/meter draw call elsewhere in the codebase — which is nearly all of them — never checks
  this at all. Beam safety exists in the codebase but is not actually protecting anything drawn today.
- `helios_exclude_set(id, x, y, w, h)` / `helios_exclude_clear(id)` / `helios_fill_excl(...)` — let a fill
  skip a reserved rectangle (rect-minus-rect via `fw/helios_rect.h`, host-tested). **One caller**:
  `fw/fullscreen.inc`'s CPU%/hint label in the top-right corner. Generic and reusable in principle (any
  meter with a fixed always-on-top overlay element could use it), used once.
- `helios_view_changed()` (B-350/351, added this session) — the one shared "a top-level view transition
  happened, force a full repaint" call, replacing six independent bare-flag writes across
  `fw/settingsui.inc`, `fw/library.inc`, `fw/fullscreen.inc` (x3) and `fw/suite.inc` (x2).

**What it explicitly does not provide:** a concept of "the currently active view" as queryable state (the
four top-level screens are still four independent booleans — `set_open`, `lib_ui_open`, `ui_fullscreen`, and
implicitly "none of the above" for the player — combined ad hoc via macros like `UI_OVERLAY_UP`); any
input-routing abstraction (every overlay's `_input()` function is called from a hand-written priority chain
in `set_input()`/`poll_input()`, not through a registry); any notion of a reusable "list" or "row" widget
(the Settings table-driven pages, the library browser's own row-drawing, and the diagnostic sub-pages' own
result-list rendering are three separate implementations of "a scrollable list of rows with a selection
highlight"); and, as noted, any use of the double-buffering RTL that already exists on hardware.

## 3. Cross-cutting findings

### 3.1 Three unrelated things are all called "invalidation" today

- Helios's `dirty` bit (per-region, drives whether `redraw()` runs at all).
- `pl_ui_restore` / `helios_view_changed()` (a global "everything might be wrong, repaint the whole chrome
  and clear every meter cache" hammer).
- Per-meter redraw caches (`wave_drawn[]`, `spec_drawn[]`, `ui_bg_ready`, and each meter's own `force`
  parameter from the (unbuilt) `mtr_in_t` contract) — a *third*, per-subsystem invalidation vocabulary.

None of these three compose. A bug in any one of them (B-349 was in the middle one) is invisible to the
other two, and a future contributor has no single place to look to answer "will closing my new overlay
leave anything stale."

### 3.2 Two incompatible growth patterns for "one more page/meter"

Adding a plain Settings menu page is genuinely cheap (append to the declarative tables). Adding anything
richer — a diagnostic result page, a meter, a configurator — means: a new enum value in at least one of
`fw/settingsui.inc`'s two page-numbering schemes (there are *two* `SET_BLITTEST_PG` definitions already in
the file, on lines 35 and 37, guarded by different build macros — a real, load-bearing sign that this
enumeration has outgrown hand-maintenance), a new pair of `_draw()`/`_input()` functions, a new dispatch
line in two different `if` chains, and (for meters) six touched places per `docs/features/meters/
METER_MODULE_SPEC.md` section 2's own audit. The project has already paid for this cost repeatedly — Check,
Decode Sweep, Blit Test, Meter Sweep, Meter Trace, the Winamp Configurator, and 11 meters are each a
worked example of the same 6-8 step manual checklist.

### 3.3 The meter module's own contract was designed and shelved

`docs/features/meters/METER_MODULE_SPEC.md` section 3 already specifies exactly the abstraction this
review would otherwise recommend inventing for meters: a portable `mtr_in_t` struct (spec/wave/peaks/frame/
timing/geometry/theme roles/force flag) and a small primitive set, with three lessons already written down
from real bugs (B-234 shared-state fights, B-234 stale-cache-on-context-change, B-192 raw-pointer hangs).
**Only the config half was ever built** (M0-M2: manifest → generated parameter/preset tables, `fw/
meters_gen.h`). The draw half — routing all 11 meters through `mtr_in_t` instead of the inline `if
(viz_mode == VIZ_x)` chain — was never started. This is not a new recommendation; it is flagging that a
already-designed piece of exactly this framework has been sitting half-finished.

### 3.4 Settings' two page systems should be one

The declarative table (`set_menu_rows`/`set_menu_n`/`set_menu_title`/`set_menu_parent`) already proves the
project is willing to data-drive simple pages. The six special-cased pages are special only because they
need a full custom draw pass and multi-key input handling the table format can't express — not because they
need a fundamentally different *dispatch* mechanism. A single page-registry (function-pointer table indexed
by `set_page`, replacing both the table-driven path and the six-way `if` chain with one lookup) would let
the "plain menu" and "rich page" cases share one dispatch while keeping their draw logic exactly as
different as it needs to be.

### 3.5 H2 double buffering: built, proven, unused

`TAU_DBUF` closed timing alone (B-340) and combined with everything else in the all6-combined bitstream
tested this session. Nothing in `fw/` reads or writes its registers. This is the one item in this review
that is not a "growing pains" finding but a genuinely idle capability — the RTL half of Helios's own H2 plan
(`docs/features/HELIOS_SPEC.md` section 5) is done; the firmware half was never started, and every other
recommendation in this review (a real view/region registry) is a *precondition* for using it well (double
buffering only pays off once there is a real "frame is finished, flip now" moment to hook, which a page-by-
page ad hoc dispatch does not have and a proper view/flush cycle would).

## 4. Future needs that will exercise these gaps harder

- **More top-level views are coming, not fewer.** The now-playing redesign work already in flight
  (Figma node 163:57's pass 1, `docs/features/ARCHITECTURE_ROADMAP.md`'s parked "UI/UX redesign" item) and
  any eventual settings/library visual overhaul will each add screens; each one today means another
  hand-written entry in `UI_OVERLAY_UP`-style composite checks.
- **More meters are coming.** `docs/features/meters/HARDWARE_METER_IDEAS.md` and the Chladni/Winamp/VU
  Master additions this project has already shipped in rapid succession show the real cadence — each one
  currently costs the six-place manual checklist section 2 of the meter spec already measured.
- **Tau Omega cross-project sync** (theme files, meter presets, `tau-assets.bin`) means configuration that
  used to be a single build-time constant is becoming data loaded at runtime from another project's export
  pipeline — exactly the case the meter module's already-designed property/template/preset layering
  (`METER_MODULE_SPEC.md` sections 18-24) was built to handle, and exactly the case where an unbuilt draw
  contract (section 3) makes new meters risk a repeat of B-192's raw-pointer class of bug if a future
  contributor doesn't know the rule "modules draw only through engine primitives" because there is no module
  boundary enforcing it.
- **A future 720p path** (named in Helios's own origin request) will make every screen's draw cost roughly
  4x. A codebase where "redraw everything" is the *only* invalidation granularity (section 3.1) has no lever
  to pull when that stops being affordable — partial invalidation has to exist before it is needed, not be
  retrofitted under a performance deadline.
- **The RAM shrink and clk66 combination already shipped this session raises the diagnostic surface** (more
  Check/Sweep-style pages are a near-certainty as new hardware features need their own proof-of-correctness
  page, per this project's own consistent pattern for every RTL feature it has ever added) — each one today
  re-pays the section 3.2 cost.

## 5. Recommended evolution path

Ordered by leverage (how many existing bugs/costs it retires) versus risk (how much working code it
touches), not by document section number. None of this is started beyond what B-350/351 already shipped.

**Near-term, low-risk (extends what already exists, touches no working draw logic):**

1. **Finish the meter draw contract** (`METER_MODULE_SPEC.md` section 3, `mtr_in_t`) for at least the
   meters that already have generated config (`VIZ_WINAMP_BARS/SCOPE`, `VIZ_CHLADNI`, `VIZ_VU_MASTER` —
   4 of 11), since their config half already exists and their draw functions are already factored into
   `(x, y, w, h, bg)`-style calls, the smallest possible step to the real contract shape. This is the
   project's own already-designed next step, not a new idea.
2. **Collapse Settings' two page-dispatch mechanisms into one** (section 3.4) — a function-pointer table
   indexed by `set_page`, built alongside the next new page (there will be one) rather than as a standalone
   refactor of six working pages at once.
3. **Give `helios_excl[]` a second real caller** the next time any meter needs a persistent on-top element,
   to prove it generalizes rather than staying a fullscreen-only mechanism by accident.

**Mid-term (real structural work, needs a card and real verification time):**

4. **DONE for the invalidation seam (B-389, 2026-09-29):** `helios_view_t` (`enter`/`exit`/
   `invalidate_mask`) replaced `helios_view_changed()`'s single flag/fixed-checklist body at 6 real
   transition sites; `draw`/`input` dispatch collapse (the other half of this item's original scope)
   is NOT done — each screen still has its own open flag and main-loop call site. Not yet
   hardware-tested. See `docs/features/HELIOS_SPEC.md` section 11 and `docs/AUDIT_TRAIL.md` B-389.
5. **DONE (B-390, 2026-09-29):** all 8 remaining meters (Bars, Waterfall, Scroll, Dots, LED/Spectrum,
   VU, Oscilloscope, Phase Scope) converted to `mtr_in_t`-taking functions. Verified as correct code
   motion + zero new build warnings + `make test-host` unaffected. Hardware-confirmed (B-395,
   `alfatreze.TAU_DEV_54`): the owner reports it "seems normal all around" -- general confirmation, not
   an itemised per-meter comparison, stated at that scope on purpose. See `docs/AUDIT_TRAIL.md` B-390,
   B-395.
6. **Precondition proved (B-391, 2026-09-29), full unification still not attempted.** The stated
   precondition — "more than one view/region consumer" — did not hold (item 4 added a second
   invalidate-MASK consumer, a different mechanism, not a second REGION). Closed that gap: the meter
   box (`ui_draw_dynamic_cold()`'s beam-safety-gated draw) is now `ui_chrome_region`'s sibling, a
   real second row-ranged `helios_region_register_rows()` consumer, verified not to regress RAM
   placement (a real `COLD_FN3`-loss regression was caught and fixed by the heap-gap numbers, not by
   inspection — see B-391). Hardware-confirmed (B-395, `alfatreze.TAU_DEV_54`): the owner reports it
   "seems normal all around," including the meter box's own beam-safety/tear behaviour now routed
   through this mechanism instead of the old inline check. The three separate "redraw everything"
   mechanisms section 3.1 lists are still three; collapsing them into one is now unblocked but not
   done. See `docs/AUDIT_TRAIL.md` B-391, B-395.

   **Deliberately held here, not attempted (2026-09-29), after the precondition above was met and three
   follow-on tooling/safety gaps were closed (B-392) and hardware-confirmed (B-395):** re-examined
   whether "collapse the three mechanisms into one" is actually still the right next step, and concluded
   it is not, on its own merits, not for lack of a precondition this time. The three mechanisms are not
   actually redundant — they sit at different granularities. The per-region `dirty` bit and item 4's
   `invalidate_mask` are both region/view-level ("should this redraw run at all," "which shared caches
   does a transition touch") and could plausibly merge. The per-meter skip-caches (`wave_drawn[]`,
   `spec_drawn[]`, …) are a genuinely finer-grained concern — which individual bars/bands changed
   *within* a redraw that is already running — and forcing them into the same vocabulary as the other
   two would not remove a real bug, only relabel an intentional two-level design (region dispatch, then
   per-element skip inside it) as a flaw it is not. The concrete harm section 3.1 actually pointed at —
   an incomplete invalidation on a view transition, B-349's bug — is already closed by item 4's declared
   `invalidate_mask` contract. And the review's own original gate for this item still is not met: "once
   a view exists that does NOT need a full repaint on every transition" — every view today still
   declares `HELIOS_INV_ALL`. Unifying the vocabulary now would be a large, invasive rewrite of every
   meter's skip-cache logic in service of a benefit that is conceptual, not concrete — the opposite risk
   profile from items 4/5/6's own actual work (B-389/390/391/392), which were each small, provable, and
   closed a real gap. Recorded here so a future session does not re-litigate this: revisit item 6's full
   scope only when either (a) a new view genuinely needs partial invalidation, or (b) a real bug
   surfaces that the current split vocabulary is hiding — not simply because the precondition became
   available.

**Longer-term (depends on the above landing first):**

7. **Firmware built (B-396, 2026-09-29), NOT hardware-tested.** The RTL (`TAU_DBUF`, B-340) was already
   fit and sitting unused on the card ("nothing in `fw/` reads or writes its registers" — this section's
   own original finding). `DBUF_READY()` (a real presence bit, not `BLIT_READY()`'s functional probe) +
   `dbuf_redraw_begin()`/`dbuf_redraw_end()` bracket the `helios_pending_mask` dispatch block's own
   chrome/art handling (item 4's seam, exactly as this item anticipated — built AFTER item 4, against
   the one real transition point, not the six ad hoc sites this review already retired). Verified as a
   correct build (clean compile, zero new warnings, `make test-host` unaffected) — explicitly NOT
   verified as correct MMIO sequencing on real silicon; no real-CPU-in-the-loop simulation exists for
   this register pair yet, unlike PSRAM/MP3-poly/FLAC-LPC's own precedent before their first hardware
   run. See `docs/AUDIT_TRAIL.md` B-396 for the honest failure-mode note (a sequencing bug here would
   show as a NEW visible tear, not silence).

   **First hardware run found exactly that (B-399, 2026-09-29).** Real, owner-reported symptoms
   (`alfatreze.TAU_DEV_55`): the menu heading and Settings' active row briefly tearing then
   self-correcting during held-down navigation and rapid open/close. Root cause: `FB_HELD()`
   (`fw/player.c:448`) is checked inside every individual draw primitive, not by this bracket —
   while an overlay holds the screen, `ui_chrome_paint()` draws nothing at all, but
   `dbuf_redraw_end()` still requested a flip regardless, displaying stale back-buffer content.
   Fixed: `dbuf_redraw_begin()` now also checks `FB_HELD()` and no-ops when it is true, matching
   `ui_draw_chrome()`'s own behaviour. Installed as `alfatreze.TAU_DEV_56`, not yet re-tested. See
   `docs/AUDIT_TRAIL.md` B-399.
8. **Revisit the 720p question** once partial invalidation (item 6) exists to measure against; not
   worth estimating cost for until then.

## 6. What this review deliberately does not recommend

Not proposing a general retained-mode scene graph, automatic diffing, or a widget toolkit in the React/LVGL
sense — `docs/features/HELIOS_SPEC.md` section 1 already ruled that out for good reasons (no spare CPU/
bandwidth for a diff pass on this core, no interrupt controller, cooperative single-threaded polling
throughout) and nothing found in this review changes that constraint. The recommendation here is narrower:
make the handful of shapes the codebase already reinvents five times (view, region, page, list row,
invalidation) exist once, not replace the whole immediate-mode drawing model.

## 7. Cross-references

- `docs/features/HELIOS_SPEC.md` section 11 — the `helios_view_t` design this review's item 4 builds on.
- `docs/features/meters/METER_MODULE_SPEC.md` sections 2-3 — the meter audit and on-device contract this
  review's items 1/5 point at building, not redesigning.
- `docs/AUDIT_TRAIL.md` B-349/B-350/B-351 — the bug and the first structural fix that prompted this review.
