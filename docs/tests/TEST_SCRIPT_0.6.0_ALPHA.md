# User test: `alfatreze.TAU_0_6_0_A_21`

Full rewrite, 2026-09-29 — the previous version of this file covered `TAU_0_6_0_A_1/2/3/6`, all long
superseded. `A_21` carries everything those covered (blend, 192 KB RAM shrink, `clk_sys` 66.667 MHz,
Helios H2 double buffering, persist widening) plus the T2-00 ALM fix, Helios items 4-8, and a full
first-hardware-test-and-fix cycle for B-405's real hardware crossfade (B-406/407/408). Most of what's
below has never been tested on real hardware at all — this is the first pass.

## Card state — what's on it, what to test

| Core | Status | Test it? |
|---|---|---|
| `alfatreze.TAU` | release v0.5.0, unchanged | No — already released, not touched this cycle |
| `alfatreze.TAU_DIAGNOSTIC` | release v0.5.0, unchanged | No — same as above |
| `alfatreze.TAU_DEV_54` | earlier item-7 iteration, OLD pre-T2-00 bitstream | **No — superseded, safe to remove on next install** |
| `alfatreze.TAU_DEV_56` | earlier item-7 iteration, OLD pre-T2-00 bitstream | **No — superseded, safe to remove on next install** |
| `alfatreze.TAU_0_6_0_A_21` | current, everything below | **Yes — this is the whole test** |

Take a screenshot (Pocket screenshot combo) of every item marked **[shot]**. **Quit the core properly**
(not just power off) before pulling the card, and again after Part 2's persist test specifically — a
screenshot or `interact_persist.json` write can be buffered until a real Quit (`docs/CARD_INSTALL_PROCEDURE.md`).

Estimated total: ~55-70 min if you do everything. Part 1 is the highest priority — it's the first
hardware test of a whole chain of fixes made in response to your own bug reports.

---

## Part 1 — B-405/406/407/408: the real hardware crossfade and its fix cycle (~25 min)

This is genuinely new: `A_18` and earlier never had a working hardware-blend crossfade at all
(`SET_XFADE_READY()` silently fell back to the old software fade). Three real bugs were found from
your first test and fixed without needing JTAG — this is the first time any of it has run since.

### A. Settings crossfade, opened plainly (5 min)
Cold boot. **Do not open Winamp Scope yet** — the point of this section is that the crossfade should
now work from the very first Settings open, not just after Scope happens to run.
1. From the player screen, press Start to open Settings. **Pass:** no full-screen flash/glitch on
   open (the "brief flash" you reported before) — either a smooth fade, or (if the bitstream lacks
   blend) the plain colour fade, but nothing jarring. **[shot]**
2. Navigate through a few Settings pages (Appearance, Playback, Diagnostics). **Pass:** each
   transition looks clean, no leftover pixels from the previous page bleeding through.
3. Close Settings (Start or B). **Pass:** player screen redraws cleanly, no leftover Settings
   content.

### B. Library → Settings jump, repeated (5 min)
This was the specific trigger for "shows some elements of the playing screen" and the "always
happens now" persistence you reported.
4. Open the library, browse into an album (don't play anything), then press Start to jump straight
   to Settings. **Pass:** no player-screen elements (meter bars, moving content) visible during the
   transition. **[shot]**
5. Repeat this exact jump 5 times in a row (Settings → close → Library → Start → Settings...).
   **Pass:** clean every time — this is the "always happens now" check; if it's clean on repeat 5
   just as it was on repeat 1, the fix holds.

### C. Fullscreen Winamp Bars — the clamp fix (5 min)
6. Settings > Appearance > Meter > Winamp Bars. Select+Y for fullscreen. Play music loud enough to
   push bars near full height. **Pass:** bars visibly cap at a fixed height under real load instead
   of flickering/collapsing to near-empty. **[shot]** one frame at a loud moment.
   (Known limitation, not a bug: very tall bars now visibly flatten at the cap instead of reaching
   full height — the real fix needs an RTL change, not shipped yet. Confirm it caps cleanly, not
   that it reaches full height.)
7. Watch for 30 s continuous at a loud passage. **Pass:** no flicker, no bars snapping to near-zero.

### D. Winamp Scope — trail behaviour, normal and fullscreen (5 min)
8. Settings > Appearance > Meter > Winamp Scope. Watch the trail for 15 s. **Pass:** a real fading
   trail, not pixels accumulating/brightening over time. **[shot]**
9. Select+Y for fullscreen, watch 10 s, Select+Y back to normal. **Pass:** trail looks correct
   immediately after the transition both ways — no reappearing accumulation that needs "a few
   repeats" to clear (this was the specific gap B-407/408 fixed).
10. Repeat the fullscreen toggle 3-4 times in a row. **Pass:** consistently clean every time.

### E. Chladni — visibility and load consistency (5 min)
11. Settings > Appearance > Meter > Chladni, normal size. **Pass:** visible immediately, not blank.
    **[shot]**
12. Select+Y for fullscreen. **Pass:** visible, no accumulation artefact.
13. Cold boot again, go straight to Chladni (normal, then fullscreen) without visiting any other
    meter first. Repeat this cold-boot-then-Chladni sequence 3 times. **Pass:** loads correctly every
    time — this was reported as inconsistent (normal-no, fullscreen-no, normal-yes, fullscreen-yes,
    normal-no again) before the fix.

---

## Part 2 — Persist widening: theme and meter presets survive a real restart (~10 min)

The save/load code is confirmed present and wired by direct source read — this section is the actual
hardware confirmation that's never been done.
14. Set a non-default theme, a non-default accent colour, and a non-default preset on Winamp Bars
    (or Scope/Chladni — whichever has presets). **[shot]** the settings as configured.
15. **Quit the core properly** (Pocket menu, not just power off), then relaunch `A_21`. **Pass:**
    theme/colour/preset are restored, not reset to default. **[shot]**
16. Full power cycle (Pocket fully off, then on), relaunch. **Pass:** same result — confirms it
    survives a cold boot, not just a warm relaunch.

---

## Part 3 — Settings dispatch collapse (B-387): never tested on hardware (~10 min)

The six "rich" Settings pages were collapsed from three hand-written dispatch chains into one lookup
table months ago (verified on host, never run on a Pocket). Open each one and confirm it still draws
and takes input correctly — a regression here would be silent (wrong page shown, wrong keys accepted).
17. Settings > Diagnostics > Check. **Pass:** opens, runs a profile, shows results. **[shot]**
18. Settings > Diagnostics > Decode Sweep. **Pass:** opens, runs, shows per-track results.
19. Settings > Diagnostics > Blit Test. **Pass:** opens, runs through its opcodes, completes (or
    hangs the same known way it always has — see Part 5).
20. Settings > Diagnostics > Meter Sweep. **Pass:** opens, sweeps every meter, shows a QR at the end.
21. Settings > Diagnostics > Meter Trace (if present in this build). **Pass:** opens and records.
22. Settings > Appearance > Meter > Configure (the Winamp editor). **Pass:** opens, live preview
    animates, Left/Right adjusts values immediately.

For all six: **Pass also means** Start closes the menu correctly and doesn't get "eaten" by the page's
own input handling (Check's own result pages are the one deliberate exception — B closes those, not
Start, by design).

---

## Part 4 — General regression (~10 min)

23. Settings > Diagnostics > Check > STANDARD, music playing. **[shot]** **Pass:** everything PASSES
    except `Track changes` (known pre-existing failure, see Part 5 — not new, don't chase it here).
24. Settings > Diagnostics > Check > FULL (if you have time — longer, includes the blit storm and a
    soak). **[shot]** **Pass:** same as STANDARD, plus 0 late underruns on the blit storm.
25. Play MP3 then FLAC then MP3 again, several track changes in a row, normal use (not the Check
    test). **Pass:** no LOAD FAILED, no audio glitch at any transition, no visual glitch.
26. Open library, settings, playlist overlay; confirm Start closes each from any depth, Left/Right
    are Back/Forward in menus, nothing else regressed from normal daily use.

---

## Part 5 — Known open issues: observe, don't chase

These are already tracked separately and don't need new investigation — just note if you see them so
we know nothing's gotten worse:
- **`Track changes` Check failure** — still unexplained, pre-existing, expected to fail.
- **Blit Test hang** — if it still hangs the way it always has, that's the known, still-open item, not
  a new regression from this session's work.
- **Fullscreen bars capping short of full height at loud passages** — expected per Part 1 section C,
  the real fix needs an RTL change not yet built.

---

## What to send back

Every **[shot]**. For any failure: which step, what you saw, and whether a fresh cold boot reproduces
it or it was a one-off. Highest priority: Part 1 (the whole point of this test — confirms three real
fixes found from your own bug reports actually work) and Part 2 step 16 (persist across a real power
cycle, the one thing that's never been confirmed on hardware at all).
