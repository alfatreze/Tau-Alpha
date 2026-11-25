# Session handoff, 2026-09-26 (late): alpha.29 on the card and confirmed good; MP3 window-unit firmware in progress, paused mid-analysis

Supersedes `docs/SESSION_HANDOFF_2026-09-26_ALPHA22.md` for everything below (that file is still right for the earlier decisions and traps it covers -- the wave-meter corruption saga it was written before is now fully resolved, see below). Full detail per step: `docs/AUDIT_TRAIL.md` B-296 through B-306 (this session's arc) and B-307 (started, not finished -- see part 3).

## 1. On the Pocket card now

`TAU`, `TAU_DIAGNOSTIC` (both v0.4.0 release, untouched) and **`TAU_0_5_0_A_29`**: bitstream `wave-b283` seed 2 (rbf_r `92189ce5...`), firmware ROM `5af955c4...`. **Hardware-confirmed good** (fresh screenshots: Appearance, Library, an 11-track Audio Test Suite list) -- the chrome redesign (28px navy header/action bar matching the real Figma geometry, black content area, no overlap even on long lists) all work. This is the accumulation of a full day's fixes; see part 2.

## 2. What happened today, in order (the short version -- full detail in AUDIT_TRAIL B-296..B-306)

1. **alpha.23 (wave-b283, first hardware run of the hardware wave/scope block) showed real corruption** on Winamp Scope: whole-frame tearing, and real audible audio jitter. Reverted the card to alpha.22 while investigating (survived a real card-disconnect mid-backup, no data lost).
2. **Root cause 1 (found, fixed): a one-sided clamp bug.** `wviz_scope_tick()`'s hardware-wave-path `lo`/`hi` clamp only checked one direction each -- a DC-biased capture window could escape the meter's box, and the final rect math cast a negative coordinate to `uint32_t`, wrapping to a garbage draw address. Fixed (both bounds, both variables, plus a defensive signed-space clamp at the draw site).
3. **Root cause 2 (found, fixed): a real cost regression, not a bug.** The hardware wave path draws up to 256 columns x up to 3 `fb_rect()` calls -- about 21x `wviz_bars_tick()`'s own baseline (measured: `DRAW STALL` 34,663 ms cumulative, `CPU LOAD` 100%). This is what was actually causing the audio jitter. **Stopgap applied**: `wviz_scope_tick()`'s hardware branch is compiled out (`if (0 && wave_hw)`) -- the software path (already carrying the clamp fix) is the only one that runs now, on every bitstream. The real fix (batching the hardware path's draws) is NOT done; the hardware branch's source is kept, commented, ready to re-enable once batched.
4. **Instrumentation built from this incident** (owner: "make sure we don't run into performance issues" building meters): `tools/meter_cost_estimate.py`, a static source-level draw-command budget check wired into `make test-host` (strips comments and `if (0 ...)` dead branches before counting, so it stays a real signal, not permanently red). And the meter-cost Check sweep (`mw_*`, Settings > Diagnostics > Meter Sweep) -- cycles every selectable meter for 10s each while music keeps playing, recording real SDRAM busy%, draw-stall, CPU%, underruns, into one QR. Matches `docs/METER_MODULE_SPEC.md` section 15's own design almost exactly (confirmed by reading it after building this).
5. **A logged recommendation, not built**: two-tier graceful degradation for the meter-module spec (self-scaling first, hard fallback to the cheapest meter as the backstop) -- `docs/AUDIT_TRAIL.md` B-300, for the meter-module spec's own owner (a different session) to fold in; `docs/METER_MODULE_SPEC.md` itself was NOT edited (not this session's file, per the two-sessions rule).
6. **Info page QR export**, **QR titles on every QR-producing feature** (Check's own QR and Decode Sweep's were missing one), all built and hardware-confirmed.
7. **Chrome redesign**: full-bleed header/action-bar/content-black layout, built first from a screenshot guess (wrong: guessed a 44px header), then corrected once Figma MCP access was fixed mid-session (see part 4) to the real 28px header from `get_design_context` on node 196:1840 ("Menu Layout"). Multiple real bugs found and fixed along the way -- see B-306 in the audit trail for the full list (a stray unbalanced `#endif`, an `ov_draw` restore skip, a hint-text regression, the Info-page row/action-bar overlap and its "not always visible" partial-refresh cause).
8. **Meter Sweep's mode option** converted to Check's own row-select convention (`KEY_LEFT`/`KEY_RIGHT` change the highlighted row, not a separate key).
9. **Homepage/now-playing redesign** (node 194:1630, "now-playing-no-art-first draft") -- design pulled and read (128x128 art, genre badge, real visualizer geometry, a transport bar with icon+label pairs), explicitly **parked for its own calmer session**, not attempted. Icons: confirmed no external assets are needed -- this codebase draws every icon as procedural rectangle shapes for a hard BRAM-budget reason (documented in the code itself), and new icons should follow the same convention.

## 3. In progress, PAUSED mid-analysis: MP3 window-unit firmware (B-292's own "next" item)

The RTL (`tau_mp3_poly.sv`, `TAU_POLY`) is built, bit-exact in simulation, committed, NOT fitted to a real bitstream yet (though `poly-b298`, a fit combining it with everything else, closed clean on both seeds back at 11:49 today and was never collected -- see part 5). Firmware is `docs/MP3_FILTERBANK_KERNEL_DESIGN.md` section 6 step 4, explicitly scoped as: `hw_poly` probe, redirect FDCT32's 32 unique output writes to `POLY_PUSH`, read 32 PCM words back, per-slot software fallback, a Check test.

**Done this session:**
- `hw_poly` boot probe added (`fw/player.c`): `R_POLY_CTL/PUSH/IDX/OUT/ST` register defines (0x100-0x110, matching `docs/MMIO_ALLOCATION.md`), `hw_poly = (uint8_t)(REG(R_POLY_ST) & 1u);` at boot, same pattern as `wave_hw`/`spec_hw`. Zero behavior change (nothing reads `hw_poly` yet). Builds clean, `make test-host` passes. **Not committed.**

**Found, NOT yet acted on -- the real technical crux of the remaining work:**

The redirect needs to happen inside `Subband()` in `third_party/libhelix-mp3/real/subband.c` (the shared MP3 decode path every track runs through):
```c
FDCT32(mi->outBuf[0][b], sbi->vbuf + 0*32, sbi->vindex, (b & 0x01), mi->gb[0]);
FDCT32(mi->outBuf[1][b], sbi->vbuf + 1*32, sbi->vindex, (b & 0x01), mi->gb[1]);
PolyphaseStereo(pcmBuf, sbi->vbuf + sbi->vindex + VBUF_LENGTH * (b & 0x01), polyCoef);   // <-- this is what gets replaced when hw_poly is on
```
`FDCT32` writes each of its 32 unique output values twice (`d[0] = d[8] = s;`, 33 occurrences in `third_party/libhelix-mp3/real/dct32.c`, confirmed textually identical by grep) at addresses that shift per-call based on `offset`(vindex 0-7)/`oddBlock` -- **re-deriving that address arithmetic externally to read the values back after the call is real, easy-to-get-subtly-wrong work** (traced the first few store addresses; the pattern is a mix of a special first-sample address and a uniform `+64`-stride loop, but a full independent re-derivation was judged too risky to attempt blind).

**The already-proven, safer alternative** (this is where analysis stopped): `tools/gen_mp3_poly_rom.py` already solves the *identical* problem for host tooling -- at build time it regex-patches a **scratch copy** of `dct32.c`'s text (never the real file), replacing `d[0] = d[8] = s;` with a `WRLOG` macro that also appends to a `wlog[]`/`wn` log, in call order. `sim/mp3_poly_model.c` already consumes that log correctly, including one real gotcha: **FDCT32 logs 33 values via this hook, not 32 -- index 17 must be discarded** (`if (k == 17) continue;`) to get the true 32 unique push-order values. This exact mechanism, made permanent and macro-gated (default off, so it's byte-identical when off -- captured a baseline hash for exactly this proof: `dist/Assets/tau/common/tau.rom` = `2340b94aedc494bc...`, `tau-cold.bin` = `c9bb090b45b2ea09...`, BEFORE any `dct32.c` edit) is the recommended next step:

1. Add a macro (name it `TAU_POLY_FW`, deliberately distinct from the RTL's `TAU_POLY` macro, so firmware capture and RTL presence can be tested independently) to `third_party/libhelix-mp3/real/dct32.c`: `#ifndef TAU_POLY_FW / #define TAU_POLY_FW 0 / #endif`, a small `#if TAU_POLY_FW` block declaring `extern int tau_poly_wlog[33]; extern int tau_poly_wn;`, then `replace_all` every (all 33, confirmed textually identical) `d[0] = d[8] = s;` with `d[0] = d[8] = s;` plus a call to a macro that's a no-op when `TAU_POLY_FW` is 0.
2. **Before touching `Subband()` at all**: verify this produces the exact same 33-values-then-skip-17 sequence as the already-proven host `wlog`/`mp3_poly_model.c` path, ideally by extending `sim/mp3_poly_probe.c` or a new host harness to cross-check the *permanent* macro against the *scratch-copy* one on the same random inputs -- they should be identical by construction, but prove it before trusting it in the shared decode path.
3. **Verify byte-identity with `TAU_POLY_FW` off** (or undefined) against the captured baseline hashes above, on `release` at minimum -- the same discipline used for M0 (B-294) and every other refactor-shaped change this session.
4. Only then touch `Subband()`'s stereo branch: reset `tau_poly_wn=0` before each `FDCT32` call, build the 32-value push order per channel (apply the skip-17 rule), and when `hw_poly` is true: push 64 words (channel 0's 32, then channel 1's) to `R_POLY_PUSH`, `R_POLY_CTL` go, poll `R_POLY_ST` busy, read 32 PCM words via `R_POLY_IDX`/`R_POLY_OUT` into `pcmBuf` in Helix's own interleave (`{R sample, L sample}` per word) -- replacing the `PolyphaseStereo(...)` call only, not the `FDCT32` calls or the `vindex` bookkeeping, which must stay exactly as today regardless of hw/sw choice (the vbuf history is still needed for future slots either way).
5. Fallback to the real, unmodified `PolyphaseStereo`/`PolyphaseMono` when `!hw_poly`, mono (design is stereo-only), or a busy-timeout.
6. A Check test comparing hardware and software PCM for the same input (the design doc's own stated gate).
7. Only after firmware is real and host-verified: collect+package the already-closed `poly-b298` fit (seed 1, better margin, see part 5) and do a real hardware run.

**Why this got paused here, not rushed further:** this is genuinely correctness-critical surgery on the one decode path every MP3 track in the whole project depends on. The session had already found and fixed several real regressions from moving fast on less critical code (the settings chrome, the meter sweep) -- the same discipline matters more here, not less.

## 4. Figma access (fixed mid-session, worth remembering)

The Figma MCP connector was stuck on a stale cached OAuth identity (`alfatreze@gmail.com`) that didn't have edit access to the Tau design file, even after the owner reconnected the *browser's* Figma login and even after toggling the session connector off/on (neither refreshes a `connected`-but-wrong-account state; `reconnect_session_connector` only re-dials a *failed* connector). The actual fix was the owner sharing the file directly with the cached identity. If this recurs: check `mcp__figma__whoami` first, and know that different Claude sessions can be stuon different cached identities simultaneously (confirmed: another session showed a completely different email, `abel.santos@timwetech.com`, while this one stayed on `alfatreze@gmail.com` throughout).

Figma file: `rT7ux8u0R7SS3D34J4Idsq` ("Tauᵅ"). Nodes read so far: `196:1840` ("Menu Layout", used for the chrome redesign), `194:1630` ("now-playing-no-art-first draft", read but not implemented -- the parked homepage redesign).

## 5. Pending Quartus work

**`poly-b298`** (MP3 window unit on top of the full `wave-b283` bundle): closed clean on both seeds at 11:49 today, **never collected**. Seed 1 has the better worst-case margin (+0.133 ns vs seed 2's +0.108 ns). RAM is now 304/308 -- only 4 free blocks, deliberately not spent yet (nothing to test with no firmware). Collect with `python3 tools/vm_fit.py collect poly-b298 --seed 1` once the firmware above is real and host-verified.

## 6. Git state

Local `main` was already ahead of `origin/main` before this session (unpushed). This session added a large diff on top, uncommitted:
- **Mine**: `CLAUDE.md`, `Makefile`, `docs/AUDIT_TRAIL.md`, `fw/fullscreen.inc`, `fw/helios.inc`, `fw/player.c`, `fw/settingsui.inc`, `fw/suite.inc`, `fw/suite_core.h`, `sim/test_helios_beam.py`, `sim/test_suite.py`, `tools/decode_tau_suite.py`, `tools/ui_snapshot_renderer.py`, plus new files `fw/helios_rect.h`, `fw/info_export.inc`, `fw/meter_gen_enum.h`, `fw/meter_gen_names.h`, `fw/meter_gen_order.h`, `meters/`, `sim/test_helios_rect.py`, `tools/gen_meters.py`, `tools/host/helios_rect_harness.c`.
- **NOT mine, do not overwrite or commit** (the other session's, per the standing two-sessions rule): `docs/CARD_INSTALL_PROCEDURE.md`, `docs/CROSS_PROJECT_INTERFACE.md`, `docs/CURRENT_STATUS.md`, `docs/MEDIA_LIBRARY_0.4_SPEC.md`, `tools/sync_media.py`, `docs/CHLADNI_METER_SPEC.md`, `docs/DECISIONS.md`, `docs/IMAGE_FORMATS.md`, `docs/METER_MODULE_SPEC.md`, `docs/THEME_SPEC.md`, `sim/test_tau_image.py`, `tools/lab/`, `docs/vendor/`.
- `dist/Assets/tau/common/{tau.rom,tau-cold.bin}` modified (the plain `release` build, expected).
- Next free audit id: **B-307** (started in this handoff, not yet closed out with a real code change).

## 7. Tools reminder

- Card install: `tools/install_dev_core.py <pkg> --carry-from OLD --remove OLD --yes` (dry run first; it refuses a `--carry-from` source that isn't actually on the card -- caught a real mistake this session where alpha.28 was packaged but never installed).
- Fit: `python3 tools/vm_fit.py status/collect NAME --seed N`.
- Package: `python3 tools/package_dev_build.py --semver 0.5.0-alpha.N --cover-slot --rbf PATH --rbf-sha256 HASH`.
- Meter cost budget check: `python3 tools/meter_cost_estimate.py` (wired into `make test-host`).

## 8. UPDATE (later, same day): the MP3 firmware was recovered and finished -- read this, part 3 above is OUT OF DATE
The previous session continued past this note and was cut off mid-edit. It has been reviewed, its bugs fixed, verified end to end on the host and packaged: see `docs/AUDIT_TRAIL.md` B-308. Short version: firmware redirect is built behind `POLY_FW=1` (default off, release byte-identical to the pre-edit baseline), a first-8-slots self-check protects the first hardware run, `Info > MP3 WINDOW` shows it, `poly-b298` seed 1 was collected and **`alfatreze.TAU_0_5_0_A_30` is packaged, NOT installed**. Next: owner approves the install, reads Info > MP3 WINDOW and the CPU load, and we decide whether to make `POLY_FW=1` the default for the diagnostic build. Still uncommitted (the file list in part 6 plus `fw/mp3_poly_hw.h/.inc`, `sim/mp3_poly_*`, `sim/test_mp3_poly_*`, `sim/mp3_poly_model_core.h`, the three Helix files, `fw/build.sh`).

