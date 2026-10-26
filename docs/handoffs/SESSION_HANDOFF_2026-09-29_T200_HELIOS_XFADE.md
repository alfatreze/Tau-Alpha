# Session handoff — 2026-09-29 (part 2): T2-00 hardware-confirmed, Helios items 4-8 done/held, real alpha-blend crossfade built

**Read this first.** Supersedes `docs/handoffs/SESSION_HANDOFF_2026-09-29_LPC_HW_AND_HELIOS.md` for
everything below (that doc's own "next step" — the Talos `glyphbuf` fix — is what this session did
first). This session is very long; read `docs/AUDIT_TRAIL.md` entries B-388 through B-405 for full
detail on any one item below. Card state and immediate next actions are in section 6 — read that even
if you skip everything else.

## 1. T2-00: Talos `glyphbuf` ALM fix — built, fit-confirmed, hardware-confirmed (B-388, B-398, B-401)

The other session's own designed fix (`docs/research/TALOS_REVIEW_2026-09-28.md` section 1a): merged
`mp3_fb.sv`'s six `glyphbuf` write sites into one shared write port (`gb_we`/`gb_addr`/`gb_data`, set via
blocking assignment, one non-blocking write fans out to two MLAB copies `glyphbuf_a`/`glyphbuf_b`).
Passed blend/mutation/reference-renderer tests unchanged. VM fit (`glyphbuf-t200`, the
`TAU_BLIT_BLEND`+`TAU_LPC` bundle that previously overflowed at 111% with no fit at all): **both seeds
Successful, all four corners positive** (seed 2 selected: RAM 240/308 = 78%, DSP 19/66 = 29%, real
margin not a bare pass). Installed and hardware-confirmed on a quick general pass (B-401). **This
resolves the ALM pressure that motivated `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md`** — recommend
re-evaluating whether that plan is still warranted before starting it, rather than treating it as
still-scheduled work.

**Bonus, not yet specifically re-verified**: `TAU_BLIT_BLEND` was previously *shelved* for a real
-2.972 ns timing failure traced to this exact same congested write network. The T2-00-fixed bundle
includes it and closed clean — T2-00 likely fixed blend's own old timing problem as a side effect. The
Winamp Scope trail effect (which uses hardware blend) hasn't been specifically re-checked on this
combined bitstream.

## 2. Helios items 4-8: built, bugs found on hardware and fixed, items 5/6/8 done, item 6's full scope deliberately held

- **Item 4 (`helios_view_t` registry, B-389)**: done. `helios_view_switch(from, to)` — deliberately
  two-argument, not the design doc's single-arg sketch (every real call site already knows both ends;
  a tracked global "current view" would recreate B-349's own silent-omission risk).
- **Item 5 (8 legacy meters into `mtr_in_t`, B-390)**: done, hardware-confirmed generally (B-395,
  "seems normal all around" — not an itemised per-meter sweep).
- **Item 6 (partial-invalidation vocabulary)**: precondition proved (a second real Helios region,
  the meter box, B-391) but the FULL unification (folding the three separate invalidation mechanisms
  into one) is **deliberately held, not attempted** — see `docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md`
  item 6's own closing note for the full reasoning (different granularities, the real bug already fixed
  by item 4, the review's own gate still not met). Don't re-litigate this without a new driving reason.
- **Item 7 (H2 double buffering, B-396/399/402)**: built, TWO real bugs found and fixed. First
  (B-399, owner-reported on hardware): `dbuf_redraw_begin()` didn't check `FB_HELD()`, so a chrome
  invalidation firing while an overlay held the screen would still flip onto stale back-buffer
  content. Second (B-402, found by inspection while scoping an unrelated feature): same bug shape for
  `screen_blank`, a DIFFERENT condition FB_HELD() doesn't cover. Both fixed. Installed as
  `alfatreze.TAU_0_6_0_A_18`, hardware-confirmed on a quick pass ("seems good so far").
- **Item 8 (persist widening)**: turned out to be **already fully built** (B-346, before this
  session) and unconditionally present in the current RTL/firmware tree (no macro, no version bump) —
  found by B-403 while the ROADMAP/CURRENT_STATUS docs still said "needs a fit." Corrected. One thing
  still not confirmed: theme + meter-preset choices actually surviving a real Quit+relaunch on hardware.

**Lesson repeated twice this session (B-403, B-404): don't trust `docs/ROADMAP.md`/`docs/CURRENT_STATUS.md`
at face value for "is X done" without checking the actual code first.** Both persist widening (item 8)
and alpha blend firmware use (see section 3) were reported as "not started" in the stale docs while
already being fully shipped in the tree. Check `git grep`/read the actual function before scoping "what's
left."

## 3. Alpha blend firmware use — corrected scoping (B-404), then genuinely upgraded (B-405)

Owner correctly pushed back on a stale claim ("alpha blend in firmware — not started"): it was already
used for the Winamp Scope's trail (`ui_bg_blend()`, real hardware blend) and Settings' page-transition
fade (`set_fade_k[]`, but that one is a SEPARATE software colour-interpolation trick, not the hardware
blend opcode — B-334's own comment says so explicitly: "there is no blend in this").

**B-405: built a genuine hardware crossfade for Settings, replacing (with a fallback) the
colour-interpolation trick.** Render the new page once, fully, into whichever H2 buffer is NOT
displayed (pure scratch use — safe because `FB_HELD()` already blocks H2's own front/back logic while
Settings is open, B-402); blend-composite it onto the displayed buffer over the same eased curve, using
the sticky `SRC_BASE`(field 0)/`DST_BASE`(field 2) registers — **the first time any code in this
codebase has ever set those two fields**. Four real bugs traced and fixed BEFORE building (not found
afterward): `FB_HELD()` would have eaten every composite draw (the step runs outside `set_draw_now()`'s
own `ov_draw` bracket); `fb_blit()`'s 127-word chunking limit; the final step needs an exact copy, not
a 255/256 blend; an abandoned mid-flight fade could leak the sticky bases to whatever draws next. Full
detail: `docs/AUDIT_TRAIL.md` B-405.

**Built, verified as a correct build (make test-host, -Wall, heap-gap baseline, no new cold-call
issues), NOT YET HARDWARE-TESTED AT ALL.** This is the least-precedented firmware this session wrote —
first use of sticky SRC_BASE/DST_BASE, combined with H2 redirection and blend, all three together for
the first time; each piece alone is hardware-proven, this exact combination is not. Packaged as
`alfatreze.TAU_0_6_0_A_19`; **not installed** — the SD card was not mounted when it was ready to go on.
Real risks worth watching on first boot, worst first: (a) a full-screen glitch/flash on ANY Settings
page open (would mean a real ordering bug — the one to worry about); (b) the fade just not visibly
happening (benign, means `SET_XFADE_READY()` was false); (c) a corrupted/offset composite (columns or
rows wrong).

## 4. Power-saving-mode scoping (no code — pure research, informed the alpha-blend work above)

Owner asked to scope a power-saving mode (screen off after a timer, stop meters, wake on any key).
Loaded the `analogue-pocket-dev` skill first. Found: **this feature already exists and ships** —
`fw/player.c`'s SCREEN BLANK (Settings > Appearance: NEVER/1/5/10/30 MIN, wakes on any key, stops all
meter drawing too). **Confirmed via three independent sources (the skill's docs/KB, this project's own
prior code comment, and the platform's own command list) that a core cannot reach the Pocket's LCD
backlight at all** — "screen off" from a core can only ever mean "draw black," never real backlight
power savings. Dynamic CPU/PLL clock scaling: the skill's KB-015 flags PLL reconfiguration as
unreliable on this hardware (community-reported); even ignoring that risk, decode must run at full rate
while music plays, so there's no real idle window to exploit for that specific use case anyway.
**Recommendation given: nothing to build for real power savings — it's a platform limit, not a gap.**
This is what led to noticing the `screen_blank`/`dbuf_redraw_begin()` interaction bug fixed in B-402.

## 5. Cross-session process note

Task notifications on resume showed two stale Monitor tasks (`bmmpbizr1`, `bynpa8yz0` — the original
T2-00 VM-fit watchers) marked "stopped" with no completion record, meaning the process was interrupted
mid-session (not a clean end) sometime after T2-00 itself had already finished and moved on to the
Helios/persist/blend work. Nothing was lost — this handoff and `docs/AUDIT_TRAIL.md` B-388 through
B-405 cover everything that happened. No orphaned VM fits or card operations need cleanup as far as
this session's own records show.

## 6. Card state and immediate next actions

**Cores on the card** (last confirmed, before it was unmounted): `alfatreze.TAU`,
`alfatreze.TAU_DIAGNOSTIC` (release v0.5.0, unchanged), `alfatreze.TAU_DEV_54`/`alfatreze.TAU_DEV_56`
(earlier item-7 iterations on the OLD pre-T2-00 bitstream, free to remove once `A_19` is confirmed),
`alfatreze.TAU_0_6_0_A_18` (T2-00 + persist widening + Helios 4-8 minus the B-405 crossfade, the last
one actually confirmed on hardware — "seems good so far").

**Packaged, NOT installed**: `alfatreze.TAU_0_6_0_A_19` at `work/diagnostics/tau-0_6_0_a_19/pocket`
(B-405's real alpha-blend crossfade). Install with:
```
python3 tools/install_dev_core.py work/diagnostics/tau-0_6_0_a_19/pocket \
    --carry-from alfatreze.TAU_0_6_0_A_18 --remove alfatreze.TAU_0_6_0_A_18 --yes
```
(dry-run first without `--yes` per the standing procedure). **Do this as soon as the card is mounted.**

**After install, the real next steps in order:**
1. First boot of `A_19` — watch specifically for a full-screen glitch/flash on any Settings page open
   (the one bug class that would mean the crossfade has a real ordering error, not just "doesn't show").
2. If the crossfade looks right: open Settings, change the theme and a meter preset, **Quit the core**
   (not just power off), relaunch, confirm both are remembered — closes item 8 for real (B-403's own
   remaining check).
3. Re-check the Winamp Scope's trail effect specifically (section 1's "bonus" note) — confirm it shows
   an actual fading trail, not the plain-erase fallback.
4. Decide on the Talos 2 rewrite plan (section 1) — recommend declining/deprioritizing given T2-00's
   result, but it's the owner's call.
5. Remaining, lower-priority open items: the `Track changes` Check failure (still unexplained, still
   failing), two small Talos correctness bugs (`OP_BAR`'s 7-bit lit-row wrap above 127 rows,
   `fb_wait()` used as "engine finished" at 3 call sites when it only means "FIFO not full"), a
   hardware-vs-software sample-exact FLAC LPC Check comparison, and the Settings page-dispatch
   collapse (B-387) — built long ago, still never hardware-tested.
