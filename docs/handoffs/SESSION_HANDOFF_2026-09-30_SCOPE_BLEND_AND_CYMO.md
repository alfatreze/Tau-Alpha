# Session handoff -- 2026-09-30: Scope-blend investigation, Cymo 44.1 kHz, build-clobber bug

Read this first if picking up either the Winamp Scope trail bug or the Cymo audio work. Two independent
threads, both left mid-investigation with real progress and a concrete next step each.

## 1. Winamp Scope trail "accumulation" bug -- still open, diagnostic tooling now solid

**Symptom** (owner-reported, reproduces reliably): opening then closing the Settings menu (or visiting
fullscreen/Configure) while Winamp Scope is the active meter triggers the trail to stop fading --
old trace positions accumulate into a dense, growing white mass instead of fading toward the
background gradient. `trail=0%` completely fixes it (proves the bug is genuinely in the blend path);
`trail=5%` and `trail=80%` look visually identical (proves severity doesn't scale with the configured
fade amount).

**Everything checked and ruled out, in order (all via `alfatreze.TAU_0_6_0_A_29` through `A_38`, all on
the `glyphbuf-t200` bitstream, RBF `b089b82871d7f441e2d68665f18a9a130691598726cb9cd7a828fd1ee2195a7e`):**
- STRIP (B-433/434): the gradient strip's actual SDRAM content always reads correct.
- BASES (B-434): the sticky SRC_BASE/DST_BASE fields read zero, as expected.
- DBUF (B-439): `R_DBUF_CPU`/`R_DBUF_DISP` always agree -- rules out an H2 buffer-selection mismatch
  (the exact bug class already found once for Chladni, B-414).
- ALPHA (B-440/B-441/B-444): firmware's own alpha shadow AND a live JTAG read of the hardware
  `blt_blend_alpha` register both show the CORRECT value, exactly matching `(100-trail)*256/100`, at
  three different trail settings (153/243/51 for 40/5/80%) -- rules out both a firmware computation bug
  and a hardware sticky-field-latch bug.
- The boot-time known-answer self-test (white over black at alpha=128, high contrast) passes, meaning
  the blend arithmetic itself is proven correct in hardware for at least one real case.

**Genuinely new finding this session (B-444, KB-078 in the skill):** opportunistic JTAG/ISSP polling
cannot resolve this further. Worked out the actual arithmetic: `blend_ch()`'s `>>8` approximation means
a source/destination pair differing by only 1 LSB per channel produces the IDENTICAL result for every
alpha 0-255 -- a real, documented property of the approximation, not a bug. Combined with a real decay
converging to within 1 LSB in about 4-5 frames, every live JTAG sample (and there were many, across
several sessions of probing) was mathematically guaranteed to land on an already-converged,
indistinguishable pixel. This is a real methodology dead end, not bad luck -- don't spend more JTAG time
on this specific question.

**Built instead: PIXHIST** (`fw/player.c`'s `dbg_pixel_log()`, `fw/settingsui.inc`'s Info row) -- a
firmware-only frame-by-frame decay log of one fixed on-screen pixel, read via the existing
`blend_mb_read()` SDRAM mailbox (no JTAG needed). It has been moved TWICE already chasing where the
actual accumulated mass visually sits (B-447: box top-left corner, rarely visited by the trace, always
converged -> B-449: `(x0+w/2, y+8)`, near the box's own top edge, after a real screenshot showed the
mass concentrated there). **Not yet read during a confirmed-active repro with the corrected location** --
this is the actual next step.

**A real, hardware-confirmed crash was found and fixed along the way (B-445):** `set_draw_ro()`'s fixed
`char v[40]` row-value buffer has NO bounds check. A 4-entry PIXHIST design overflowed it by 5 bytes,
crashing real Pocket silicon the instant that row rendered, reproducibly, every time. Fixed by reducing
to 3 entries. **If PIXHIST or any other Info row is extended again, count the bytes against this 40-byte
buffer BEFORE shipping it** -- nothing else in the codebase checks this at build time.

**Next step:** reproduce the accumulation (menu open/close, or fullscreen/Configure visit), let it
visibly accumulate, open Diagnostics > Info, read the new PIXHIST row (now at `(x0+w/2, y+8)`, the box's
upper region). If a half trends down toward the shown `W` (expected background) value across the 3
entries, the blend genuinely decays and the bug is elsewhere entirely (something else painting over the
correctly-faded background, worth reconsidering from scratch). If it stays near a bright, non-decaying
value with real contrast against `W`, that is the first genuinely decisive evidence of a real hardware
blend bug -- at that point, extending the `BLND` JTAG probe (already built, `blt_blend_alpha`/
`blt_blend_mode` already added, B-441/B-443) to ALSO capture `bl_fg`/`bl_bg` and comparing against a
KNOWN bright pixel address (not opportunistic) becomes the informative next move, since polling near a
KNOWN-bright address sidesteps KB-078's own limitation.

Currently installed: `alfatreze.TAU_0_6_0_A_38`.

## 2. Cymo 44.1 kHz audio quality investigation -- RTL cleared, needs the analog/serializer path next

Separate thread (branch `cymo`, merged into `main` this session, B-448 onward is `main`-only). Read
`docs/handoffs/SESSION_HANDOFF_2026-09-29_CYMO_AUDIO.md` for the full background; this section only
covers what changed since.

**Decisive result (B-442):** ran the project's own prescribed decisive simulation --
`sim/test_cymo_i2s_rate.py --altera-mf <quartus>/eda/sim_lib/altera_mf.v`, swapping Intel's REAL `dcfifo`
timing model in for the behavioural one. Result: SINAD 27.71 dB, spurs matching the ideal-hold
prediction essentially exactly, 0 underruns -- **this conclusively rules out the real Altera `dcfifo`'s
own timing as the cause** of the ~17 dB-worse real hardware recordings (10.8 dB SINAD, flat click-like
spurs, 3.7 dB low level). The RTL logic (both the model AND now the real vendor timing) is clean.

**Next step, per the handoff's own decision tree:** look after the serialiser (`sound_i2s.v`'s own
output stage) or at the recording/analog path itself (the owner's capture chain) -- not the RTL/FIFO
hand-off, which is now twice-proven clean. Nobody has looked at `sound_i2s.v`'s serializer logic itself
yet, nor re-examined whether the capture setup (gain staging, cable, interface) could itself be
introducing the discrepancy.

## 3. A real, previously-undiscovered build-tooling bug -- fixed, worth knowing about for ANY future alpha

**This bit hard once already (B-448) and could recur if the lesson isn't remembered.** `tools/
check_heap_gap.py` rebuilds every tracked firmware target with NO env flags, for its own legitimate
purpose (checking the plain/default variant's heap gap). But `player-library-diagnostic-profile`'s
output path is not redirected by `RAM_192K`/`CLK66`/`SDRAM_BUSY`/`LPC_FW` the way `release`'s path is --
so a manually-flagged build (`RAM_192K=1 CLK66=1 SDRAM_BUSY=1 LPC_FW=1 bash fw/build.sh
player-library-diagnostic-profile`) and `check_heap_gap.py`'s own unflagged rebuild land at the IDENTICAL
file path. Running `check_heap_gap.py` (or anything else that rebuilds the same target) AFTER building
the real flagged firmware, but BEFORE packaging it, silently ships the wrong variant -- same reported
sizes, genuinely different bytes, no error anywhere. Paired with a bitstream built for those macros, the
symptom is a total black-screen boot with zero diagnostic signal (a real B-130-class failure).

**Fixed at the tool level:** `tools/package_dev_build.py` now takes `--build-flags` (comma-separated
`KEY=VAL`), which makes it build the firmware itself, in-process, as the literal last step before
reading and packaging the ROM -- eliminating the whole "hope nothing ran a build in between" risk by
construction. **Use it for every future alpha build involving these flags:**
```
python3 tools/package_dev_build.py --semver 0.6.0-alpha.NN \
    --build-flags "RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1" \
    --rbf work/diagnostics/glyphbuf-t200/ap_core_s2.rbf \
    --rbf-sha256 923d854b20171ada039486fa280298357368d0f0794a27843c4cc3d9c772ed01
```
Omitting `--build-flags` keeps the old behaviour (reuse whatever is already at the path) -- still correct
for `make_release.py`'s own `--release-diagnostic` call, which never uses these flags. Full writeup,
including the exact diagnosis technique (a clean rebuild's hash disagreeing with the shipped one, then
proving the clean rebuild itself is deterministic by repeating it) is `docs/AUDIT_TRAIL.md` B-448, and
`analogue-pocket-dev` skill KB-077 (local, hardware-validated) for the general, project-independent
lesson.

## Standing facts, unaffected by anything above
- The correct bitstream for every `0.6.0-alpha.N` core since `A_17` is `glyphbuf-t200`
  (raw `923d854b20171ada039486fa280298357368d0f0794a27843c4cc3d9c772ed01`, reverses to
  `b089b82871d7f441e2d68665f18a9a130691598726cb9cd7a828fd1ee2195a7e`). Always double-check a fresh
  `package_dev_build.py` run prints this exact RBF hash before installing.
- Card writes: always `tools/install_dev_core.py`, dry run first. Two real card disconnects this
  session were traced to a loose microSD-in-SD-adapter seat, not filesystem corruption -- reseat fully
  before any write if a `Device not configured`/timeout error appears.
- `docs/features/CYMO_AUDIO_ENGINE.md` / `CYMO_AUDIO_ENGINE_REVIEW.md` are the Cymo plan documents,
  merged into `main` this session (B-448 commit range). Scope is pitch-preserving tempo only; everything
  else (gapless, ReplayGain, EQ rework, Bluetooth cart) stays parked per the owner's own decision.
