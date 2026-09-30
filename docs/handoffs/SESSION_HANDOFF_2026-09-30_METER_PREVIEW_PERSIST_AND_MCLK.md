# Session handoff -- 2026-09-30 (later): Configure-preview fix, persistence root cause, OP_BAR RTL widen, Cymo MCLK

Read this first for anything touching meter previews, settings persistence, Winamp Bars fullscreen, or
the Cymo audio investigation. Supersedes `docs/handoffs/SESSION_HANDOFF_2026-09-30_SCOPE_BLEND_AND_CYMO.md`
for everything it covers (that doc's own Scope-trail fix, B-450, is done and hardware-confirmed -- see
below); its Cymo section 2 is superseded by section 4 here.

## 1. Configure page meter preview -- fixed, hardware-confirmed (B-452/B-453)

Bars/Scope/Chladni/VU Master all now animate correctly in Settings > Appearance > Meter > Configure.
Two independent bugs, both found and fixed this session:
- **B-452**: the once-per-frame data publish (`peak_l`/`peak_r`/`spec_lvl[]`) was gated behind
  `UI_OVERLAY_UP`, true the whole time Settings is open -- extracted into `meters_publish()`
  (`fw/player.c`), called from both its original site and `wvcfg_preview_tick()`.
- **B-453**: `chladni_tick_box()` had its own hard-coded `UI_OVERLAY_UP` check that never deferred to
  the caller's `ov_draw` override the way every other draw primitive's `FB_HELD()` does -- swapped for
  `FB_HELD()`.

Both hardware-confirmed by the owner. Investigation closed.

## 2. Theme/mode/settings persistence -- real root cause found, hardware-confirmed fixed (B-455/B-456)

**Not a firmware bug.** Firmware writes the hardware persist register file correctly, every time --
confirmed by full source trace. The real cause: `dist/Cores/alfatreze.TAU/interact.json` (the file that
tells APF which bridge addresses to persist to `interact_persist.json` on Quit) was never updated when
persist widening (B-346, an earlier session) added five new persist words. **Analogue's own docs state a
hard cap: up to 16 UI entries from interact.json can be shown -- entries past that are silently dropped
from BOTH the UI and persistence, with no error anywhere** (new skill entry, KB-079). B-455's first fix
attempt (adding all 5 new declarations) pushed the file to 17, one over the cap, so the fix didn't
actually work until B-456 trimmed it back to 14 (kept theme + mode, the two reported issues; deferred
the three meter-preset declarations, never reported broken).

**Second, independent bug found from the owner's own observation** (unwanted "Load Audio File"/"Load
Playlist" actions in Core Settings): ALL SEVEN non-firmware `data.json` slots had `parameters: "0x1"`
(bit 0, "User-reloadable") -- not just the two the owner noticed, but five more (loading artwork,
library index, cold image, assets, cover image) that should never have exposed a manual reload action
at all. Cleared on all seven (new skill entry, KB-080).

**Both hardware-confirmed by the owner** (theme/mode show in Core Settings, survive Quit+relaunch, the
unwanted reload actions are gone). Investigation closed. `tools/check_tau_package.py` now guards the
16-entry cap so this can't silently recur; `tools/tau_data_slots.py`'s own hard-coded expectations were
updated to match the cleared reload bit.

**Still deferred, real known gap:** the three meter-preset persist entries (Bars/Scope/Chladni preset
choice) have no persistence path -- dropped from interact.json to stay under the cap, not yet reported
as an issue, not prioritised. If ever wanted: needs either freeing 3 entries elsewhere in interact.json,
or accepting they stay session-only.

## 3. Winamp Bars fullscreen clamp -- real RTL fix built, VERIFY THE FIT RESULT NEXT (B-454)

The 127-row clamp (a firmware mitigation from an earlier session) is now a REAL fix: `OP_BAR`'s lit-row
count widened 7 -> 9 bits in RTL (`cmd_glyph_hi`, a new field, `src/fpga/core/mp3_fb.sv`/`mp3_soc.v`,
using 2 previously-free FIFO padding bits and 2 previously-free `R_FB_GO` bits -- CHAR/RRECT's own use
of the original 7-bit field is completely untouched). No hardware probe -- owner's explicit call, since
at this alpha stage firmware/bitstream are always paired one-to-one; `fb_bar()` sends the full 9-bit
value unconditionally.

Fully verified in simulation: `sim/tb_mp3_fb.v` gained a dedicated `push_bar()` task and a real
250-row/200-lit test case (impossible in the old 7-bit field), plus mutation hook
`BUG_IGNORE_BAR_HI` (killed, `make test-rtl-fb-mutation`). `make rtl-lint`/`test-rtl`/`test-host` all
clean.

**A real Quartus fit was launched this session (`bar-hi-b454`, both seeds, the same `glyphbuf-t200`
bundle every current alpha ships) and was STILL RUNNING when this session ended.** Check with:
```
python3 tools/vm_fit.py status bar-hi-b454
```
Once it closes clean on both seeds (all four corners positive): collect the better seed
(`python3 tools/vm_fit.py collect bar-hi-b454 --seed N`), package with
`tools/package_dev_build.py --build-flags "RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1" --rbf <collected .rbf> --rbf-sha256 <hash>`,
install, and get the owner to confirm fullscreen Winamp Bars now grow past 127 rows correctly (up to
323, `FS_FIG_H`).

## 4. Cymo 44.1 kHz -- a real, well-reasoned MCLK jitter fix built, ALSO QUEUED BEHIND THE SAME FIT (B-457)

The FIFO hand-off (`pcm_fifo` -> `sound_i2s`) is twice-proven clean (B-430/B-442, including with the
real Altera `dcfifo` model) -- not the cause. Read `sound_i2s.v`'s serializer (the one remaining
unexamined piece) and found the MCLK generator was a phase accumulator with a non-integer division
ratio (`245760/742500`, reduces to `4096/12375` -- `12375 = 3^2*5^3*11` has no factor of 2) -- a real,
physically measurable jitter source on every downstream SCLK/LRCK/DAC bit-clock edge, **invisible to RTL
simulation** (checks logical values, not real inter-edge timing), exactly the kind of gap that explains
"digital simulation predicts an ideal result, real hardware measures ~17 dB worse."

**Fix, owner-approved (build the real fix, not just measure first):** added a 5th output to the
existing shared PLL (`mf_pllbase_0002.v`, already producing clk_sys/clk_vid/clk_sdram from the same
74.25 MHz reference) at 12.288 MHz, using the SAME fractional-N (sigma-delta, noise-shaped) synthesis
already relied on for outclk_1/2's 12.000 MHz -- no new PLL resource, outputs 4-17 were unused.
Rewrote `sound_i2s.v` so the whole serializer runs synchronously inside this one clean clock domain
instead of `clk_74a`-domain edge-detection of the old jittery toggle.

Verified functionally equivalent in simulation (`python3 sim/test_cymo_i2s_rate.py` reproduces the
EXACT same SINAD/spurs as before the rewrite -- confirms no regression; simulation genuinely cannot
show the jitter improvement either way, since it uses ideal clocks). `make rtl-lint`/`test-host` clean.

**Queued behind `bar-hi-b454`** (same VM, this project never runs two Quartus compiles at once). Once
that fit is done and collected, launch this one:
```
python3 tools/vm_fit.py launch mclk-b457 --append tools/blit_g3_poly_blend_ram192_clk66_dbuf_lpc_qsf_append.txt --seed 1 --seed 2
```
**Once fit: this is NOT a proven fix yet, even if timing closes clean.** Two things still needed: (1)
check the ACTUAL achieved `outclk_4` frequency/error in the Quartus report, not assumed from the
"12.288000 MHz" parameter string; (2) a real hardware A/B recording (old phase-accumulator core vs this
one) to confirm the SINAD gap actually narrows. This is a strong, well-reasoned lead, not a confirmed
fix -- say so plainly if reporting progress on it.

## 5. Standing facts, unaffected by anything above

- Card install: always `tools/install_dev_core.py`, dry run first, then `--yes`. `tools/package_dev_build.py --build-flags` (B-448) for any package needing `RAM_192K`/`CLK66`/`SDRAM_BUSY`/`LPC_FW`.
- Correct bitstream for every alpha since `A_17`: `glyphbuf-t200` (raw `923d854b20171ada039486fa280298357368d0f0794a27843c4cc3d9c772ed01`,
  reversed `b089b82871d7f441e2d68665f18a9a130691598726cb9cd7a828fd1ee2195a7e`) -- until the `bar-hi-b454`
  fit lands, at which point THAT becomes the new correct bitstream for every subsequent alpha (both the
  OP_BAR widen and, once its own fit lands, the Cymo MCLK PLL change).
- Card currently has `alfatreze.TAU_0_6_0_A_43` (B-456's persistence fix, still on `glyphbuf-t200`) --
  a fresh alpha carrying B-454's RTL widen has NOT been packaged/installed yet, pending the fit.
- `docs/AUDIT_TRAIL.md`'s B-series ends at B-457.
- Two new skill entries this session: `analogue-pocket-dev` KB-079 (interact.json's 16-entry cap),
  KB-080 (data.json's User-reloadable bit clutters Core Settings if left at a copy-pasted default).
