# Current status (one page)

Updated 2026-09-30. **What is true right now.** What to do next is in `docs/ROADMAP.md` (the only ordered list). Why things are the way
they are is in `docs/AUDIT_TRAIL.md`. The old, long version of this file is `docs/archive/CURRENT_STATUS_history_2026-09-26.md`.
Full detail on the 2026-09-30 session: `docs/handoffs/SESSION_HANDOFF_2026-09-30_SCOPE_BLEND_AND_CYMO.md` (sections 1-3 still
correct background/history; its own "next step" for the Scope bug is superseded by B-450 below — read this page first).

## Headline: Scope trail bug fixed (B-450), Configure-page meter preview fixed (B-452), one open investigation

**Configure page meter preview: Bars/Chladni/VU Master were frozen static, only Scope played — found and
fixed.** The once-per-frame publish step feeding `peak_l`/`peak_r`/`spec_lvl[]` was gated behind
`UI_OVERLAY_UP`, which is true the whole time the Configure page (itself inside Settings) is open —
Scope was immune since it captures its own wave data independently. Extracted the publish step into
`meters_publish()` and call it from the preview tick too. Hardware-confirmed pending (installed as
`alfatreze.TAU_0_6_0_A_40`, not yet retested). Full detail: `docs/AUDIT_TRAIL.md` B-452.

**Winamp Scope "trail accumulation" bug: real root cause found and fixed, hardware confirmation pending.**
PIXHIST (read per the handoff's own next step) showed a stuck, non-decaying pixel with real contrast
against the expected background — the first decisive evidence, not another JTAG dead end
(`analogue-pocket-dev` skill KB-078). Root cause, found by reading `src/fpga/core/mp3_fb.sv` directly:
H2's automatic per-buffer addressing (`dbuf_addr()`) covers only RECT/CHAR/COPY dispatch — every true
BLIT-mode opcode (what the trail fade uses) is addressed purely through firmware-set sticky bases and
ignores which buffer is actually displayed, a documented RTL contract, not an RTL bug. The fade never
called `fb_set_bases()`, so it always updated buffer 0 while the trace bars correctly followed the real
displayed buffer — the exact bug class already found and fixed once for Chladni (B-414), a second
independent instance. Fixed in `fw/player.c` (`ui_bg_blend()`/`ui_bg_restore()`/the STRIP and PIXHIST
diagnostics), no RTL change, no new Quartus fit. Packaged and installed as `alfatreze.TAU_0_6_0_A_39`.
**Hardware-confirmed fixed** — the owner reproduced the original trigger and the trail now fades
normally. Investigation closed. Full detail: `docs/AUDIT_TRAIL.md` B-450.

**Cymo 44.1 kHz audio investigation: RTL cleared, real hardware evidence needed next.** The decisive
simulation (real Altera `dcfifo` model, not the behavioural stand-in) reproduces the ideal-hold
prediction almost exactly — this rules out the RTL/FIFO hand-off as the cause of the real hardware
recordings' ~17 dB worse SINAD. Next: the serializer (`sound_i2s.v`) or the recording/analog capture
path itself, neither examined yet. Full context: the 2026-09-30 handoff, section 2.

**A real, previously-undiscovered build-clobber bug shipped a broken alpha (`A_36`), found and fixed.**
`tools/check_heap_gap.py` rebuilding a flagged target with no flags of its own, landing at the same
output path as the real build, silently swapped in the wrong firmware variant — same reported sizes,
genuinely different bytes, a total black-screen boot with zero diagnostic signal. Fixed at the tool
level: `tools/package_dev_build.py --build-flags` now builds the firmware itself as the literal last
step before packaging. **Use it for every future alpha build with `RAM_192K`/`CLK66`/`SDRAM_BUSY`/
`LPC_FW`** — see the 2026-09-30 handoff, section 3, for the exact command.

## 0.6.0 in progress (since v0.5.0)
FLAC LPC hardware kernel: done, hardware-confirmed. T2-00 (`glyphbuf` single-write-port ALM fix): done,
fit-confirmed with real margin, hardware-confirmed — this is the `glyphbuf-t200` bitstream every current
alpha build uses. Helios items 1-2, 4, 5, 7 done. Settings hardware alpha-blend crossfade, Chladni H2
buffer tracking, theme/mode persistence: built and hardware-confirmed working (session of 2026-09-29,
`docs/AUDIT_TRAIL.md` B-405 through B-416). `cymo` branch (audio-engine research/tooling) merged into
`main` 2026-09-30. `docs/AUDIT_TRAIL.md`'s B-series currently ends at B-452.

## Released
- **v0.5.0** (2026-09-27, tagged, GitHub release published with both zips): `TAU` and `TAU_DIAGNOSTIC`. Themes (TAU/OCEAN, Dark/Light), TIM1 fast covers, MP3
  window unit in hardware (`POLY_FW=1` is the release default), Winamp/Chladni meters, full-screen menus with an action bar. Release heap gap 49,712 B.
  Changelog: `CHANGELOG.md`. Build/audit: B-331, B-332.
- v0.4.0 (2026-09-22) is the previous release.

## On the Pocket card
`alfatreze.TAU`, `alfatreze.TAU_DIAGNOSTIC` (release, v0.5.0, unchanged), `alfatreze.TAU_DEV_54`/
`alfatreze.TAU_DEV_56` (earlier item-7 iterations on the old pre-T2-00 bitstream, free to remove),
`alfatreze.TAU_0_6_0_A_40` — `glyphbuf-t200` bitstream (RBF `b089b82871d7f441e2d68665f18a9a130691598726cb9cd7a828fd1ee2195a7e`),
the B-452 Configure-page meter-preview fix on top of the B-450 Scope H2-buffer-tracking fix and every
earlier Scope-blend diagnostic (STRIP/BASES/DBUF/ALPHA/PIXHIST Info rows), the B-445 crash fix and B-446
auto-repeat fix. Installed and verified by SHA-256. Scope fix **hardware-confirmed** (B-450); the B-452
Configure-preview fix is not yet retested on hardware.

## Hardware-confirmed
- T2-00 (`glyphbuf` ALM fix), the full `all6-combined` + FLAC LPC bundle, Settings crossfade, Chladni H2
  tracking, theme/mode persistence: all confirmed on real silicon.
- FLAC LPC hardware kernel: 0 timeouts across multiple Checks + stress, microstutter A/B-confirmed fixed.
- MP3 window unit: 404,712 slots, 0 BAD; filterbank share of decode 22% at 1.0x (was 55-59%).
- Cymo: real Altera `dcfifo` simulation matches the ideal-hold prediction (27.71 dB SINAD) exactly.
- TIM1 covers load in about 90 ms on MP3 and FLAC albums.

## Known open evidence and defects
- **Winamp Scope trail accumulation**: open, see headline above and the 2026-09-30 handoff.
- **Cymo 44.1 kHz SINAD**: open, RTL cleared, serializer/analog-path not yet examined.
- **CPU LOAD reads 100%** in every state, so it cannot show headroom. Use the per-stage decode percentages instead.
- **`Track changes` Check fails** (0 of 10 done): pre-existing, unexplained.
- **Hardware wave/scope path is compiled out** (`if (0 && wave_hw)`, B-302): drawing 256 columns cost about 21x a normal meter. The software scope runs instead.
- Boot-restore mismatch between release and diagnostic builds (`docs/issues/021`), re-parked until the UI redesign.

## Uncommitted or not mine
`docs/vendor/` (confidential vendor datasheet, deliberately left out of every commit, by design). Check `git status` before assuming anything
else is stale — this project has multiple concurrent sessions.

## Sibling project
Tau Omega (`../Tau Omega/`, MIT OR Apache-2.0, Rust + Tauri) manages the card: sync, packages, diagnostics decode, screenshots. It keeps its own order in
its `docs/STATUS_HANDOFF.md`. The shared surface is `docs/CROSS_PROJECT_INTERFACE.md`; never share literal files.

## Where things are
| Need | File |
|---|---|
| Ordered plan | `docs/ROADMAP.md` |
| Latest handoff (sessions, traps, tools) | `docs/handoffs/SESSION_HANDOFF_2026-09-30_SCOPE_BLEND_AND_CYMO.md` |
| Decisions | `docs/DECISIONS.md` |
| Design references | `PHASE_F_SPEC`, `HELIOS_SPEC`, `HELIOS_ARCHITECTURE_REVIEW_2026-09-28`, `TALOS_REVIEW_2026-09-28`, `TALOS2_REIMPLEMENTATION_PLAN`, `METER_MODULE_SPEC`, `THEME_SPEC`, `MEDIA_LIBRARY_0.4_SPEC`, `PHASE_G_SPEC`, `TEST_SUITE_SPEC`, `MMIO_ALLOCATION`, `IMAGE_FORMATS`, `FLAC_LPC_KERNEL_DESIGN`, `CYMO_AUDIO_ENGINE`, `CYMO_AUDIO_ENGINE_REVIEW` |
| Card install | `docs/CARD_INSTALL_PROCEDURE.md`, `tools/install_dev_core.py` |
| Packaging with a specific firmware-flag combo | `tools/package_dev_build.py --build-flags` (B-448 — always use this, not a manual pre-build, for `RAM_192K`/`CLK66`/`SDRAM_BUSY`/`LPC_FW`) |
| Skill knowledge | `analogue-pocket-dev` skill, KB-069 (local, memory-inference), KB-077 (local, build-clobber), KB-078 (local, JTAG polling limits) |
