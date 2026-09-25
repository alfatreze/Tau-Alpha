# Session handoff — 2026-09-25 (Blit Test root cause found and fixed; a new anomaly opened)

**Read this first**, then `docs/CURRENT_STATUS.md`. `docs/AUDIT_TRAIL.md` entries **B-186 through
B-194** are this session's detailed log — this file is the summary, that file is the evidence.
Supersedes `docs/SESSION_HANDOFF_2026-09-24_BLIT_TEST.md` (still correct as history, but its "open
hang" is now root-caused and fixed — see below).

Git: commits through `ee681b5`, not pushed. Working tree clean.

## TL;DR

1. **The whole B-166..B-191 "Blit Test hangs" investigation is root-caused and fixed.** JTAG ISSP
   (four probe instances, live reads, one traced by disassembly to an exact stuck instruction)
   found the CPU frozen on a raw CPU pointer read at `0xA0000000 + offset` — the draw engine's own
   framebuffer/guard region, which the CPU's real uncached SDRAM window (`PL_SDRAM_BASE`,
   `0xA0100000`) does not cover at all. A raw load/store there decodes to nothing and the Wishbone
   bus never gets an ACK: permanent hang, not a draw-engine or RTL problem.
2. **A/B tests (B-192) ruled out every alternative theory** the owner and I raised along the way:
   not the HarpMudd port (pre-HarpMudd firmware still hung), not the ISSP debug tooling (the real
   shipped non-ISSP bitstream still hung), not a cold-window ordering effect (still hung right
   after a proven-passing Window Test). The bug is real, pre-existing, and unconditional.
3. **Three functions shared this exact bug, all now fixed (B-193):** `bt_crumb()`/`bt_crumb_read()`
   (relocated onto `PL_SDRAM_BASE`), `blit_probe()` (rewritten to the SDRAM mailbox, since its
   sentinel must stay in the guard region — the draw engine's `fb_cmd_addr` is hard-capped at 19
   bits and can never reach `PL_SDRAM_BASE`), and `set_thumb_flat_build()` (found proactively,
   rewritten to `fb_rect()` draw-engine commands). Confirmed on hardware: Blit Test's idle screen no
   longer hangs, `BLIT_READY()` passes, `bt_begin()` completes into `bt_advance()`'s normal per-tick
   loop.
4. **A new, separate anomaly (B-194, NOT resolved):** past all three fixes, the Blit Test itself now
   runs far longer than its fixed ~48-second design duration (`BT_OPS*BT_LEVELS*BT_WIN_S` =
   `8*3*2s`) without ever completing, hanging on one instruction, or resetting. Live-monitored 5+
   minutes: JTAG chain never broke, PC kept visibly moving, but the CPU-side checkpoint (`DBGM`)
   read exactly `15` on every single sample the entire time — suggesting `bt_advance()` isn't being
   freshly re-entered as a new tick any more, even though something is still executing. **This is
   the next thing to diagnose**, and needs a new probe (see below).
5. Two real JTAG/tooling gaps found and fixed this session (B-190): loading a core through the
   Pocket's own menu silently overwrites a JTAG-loaded debug bitstream with the SD card's own
   `.rbf` (always reload the ISSP `.sof` after any core reselect); `issp_read_probe_data`'s correct
   Tcl form is positional (`issp_read_probe_data $path`), not `-instance $path` (the latter silently
   returns the string `"error"` instead of raising an exception).

## The root cause, precisely

`tau_sdram_addr_decode.sv`'s own documented map (and `PL_SDRAM_BASE` in `fw/playlist.inc`) has
always said the CPU's uncached SDRAM window starts at `0xA0100000`, a full 1 MiB above
`0xA0000000` — the first MiB is reserved for the draw engine's own framebuffer. The draw engine's
`fb_cmd_addr` (`mp3_soc.v`) is hard-capped at 19 bits (max ~1 MiB), so it can never write past that
boundary either. Three functions ignored this and used `0xA0000000 + small_offset` as a raw CPU
pointer:

- `bt_crumb()`/`bt_crumb_read()` (`fw/suite.inc`) — offset `482*2 = 0x3C4`.
- `blit_probe()` (`fw/blit_probe.inc`) — offset `992*2 = 0x7C0`.
- `set_thumb_flat_build()` (`fw/settingsui.inc`) — the same stash rows, per-pixel.

Every one of these hangs forever, unconditionally, confirmed by JTAG on real hardware, disassembly
of the exact stuck instruction, and an A/B test across firmware vintages and bitstreams (B-192).

`blit_probe()` was previously believed proven-working on hardware (B-146/B-164) — this contradiction
was never fully resolved and is worth keeping in mind, though it doesn't change that today's fix is
correct and hardware-confirmed.

## The three fixes

1. **`bt_crumb()`/`bt_crumb_read()`**: a pure CPU scratch word, no draw-engine interaction — simple
   relocation onto `PL_SDRAM_BASE + 0x10000` (64 KiB in, clear of the playlist's own reserved bytes
   and the stress pump's 1-2 MiB region). Confirmed fixed on hardware first.

2. **`blit_probe()`**: its sentinel must stay in the guard region (the draw engine writes to it via
   `OP_BLIT`, and the draw engine can never reach `PL_SDRAM_BASE`), so relocation wasn't an option.
   Rewritten to use the SDRAM mailbox (`R_SDR_ADDR`/`DATA`/`CTRL`/`RDATA`/`STATUS`) for both the
   write and the readback — the same mechanism `fw/playlist.inc`'s `pl_sdram_prove()` already uses,
   which addresses SDRAM directly and bypasses the CPU's broken Wishbone decode entirely. Two
   secondary bugs found while getting this right:
   - First attempt used an unverified partial byte-enable (`0b0011`, meant to touch only the low 16
     bits of the 32-bit mailbox transaction) — this project has no precedent anywhere for a partial
     byte-enable write, and it caused a confirmed false "BLIT ENGINE NOT DETECTED". Fixed by
     comparing the full 32-bit readback against the full 32-bit written pattern instead, which
     doesn't need to know which half of the transaction is which.
   - A genuine cross-client race: `fb_wait()` only confirms the draw engine's own dispatch FIFO
     accepted the `OP_BLIT` command, not that the underlying multi-cycle SDRAM write has physically
     landed before the CPU's separate mailbox client issues its own read on the same single-port
     arbiter. Fixed with an explicit 512-cycle settling delay (comfortably above this project's own
     documented ~380-cycle worst-case single SDRAM access) between the `OP_BLIT`'s `fb_wait()` and
     the mailbox readback.

3. **`set_thumb_flat_build()`**: found proactively (crumb stalled at 7, "about to call
   set_thumb_flat_build()") before it was ever isolated standalone. A mailbox-per-pixel rewrite
   would have cost far more than its original ~16ms budget (~21,504 individual writes, each needing
   a busy-poll round trip), so instead rewritten to issue one `fb_rect()` draw-engine command per
   RLE run-segment — the exact grouping `set_draw_thumb_soft()`'s already-proven software-fallback
   path already uses, MMIO-only, no CPU pointer into the guard region at all.

All three verified with `make test-host` (18/18) and confirmed every firmware target still builds
(`player`, `release`, `player-diagnostic`, `player-library-diagnostic`, `player-library-check`).
`dist/`'s shipped `release` ROM was rebuilt with the same fixes and committed — it also sets
`TAU_METER_THUMBS`, so the real shipped product had this bug too (any user opening the meter-preview
list would have hit it).

## Open question noted, not yet checked

`fb_rect()` silently no-ops if `FB_HELD()` is true (`UI_OVERLAY_UP && !ov_draw`), which could
plausibly be true when `set_thumb_flat_build()` runs synchronously from `bt_begin()` (the same
question B-174 raised about `bt_checkpoint()`'s own `fb_rect()`/`fb_text_clipped()` calls from this
exact calling context — proven to not hang, but whether it silently skips was never resolved
either). Doesn't cause a hang either way, but could mean thumbnails render with stale/garbage
content if it does silently skip. Worth a screenshot check on a future session.

## B-194: the new anomaly — what to do next

With all three fixes installed, the Blit Test reliably gets past every previous stuck point (crumb
reaches 15, `BLIT_READY()` true, `bt_advance()`'s tick loop running) — but never reaches
`BT_DONE`/the QR screen, even after 5+ minutes (expected: ~48 seconds total,
`BT_OPS(8)*BT_LEVELS(3)*BT_WIN_S(2s)`).

**What's known:**
- The JTAG chain stayed healthy the whole 5+ minutes — no reset (unlike one earlier attempt, which
  self-reset on its own after 1-2 minutes; not reproduced under live monitoring, so it's unclear if
  that's the same bug or a separate failure mode).
- `PCAD`'s `iADR` kept changing between reads (not a classic single-instruction frozen bus).
- `DBGM` (the `bt_crumb()` checkpoint, now correctly working per the B-193 fix) read **exactly
  `0xF` (15) on every single sample across the entire 5+ minutes** — never once caught at 12/13/14,
  which a freshly-repeating per-tick call would eventually be sampled at by chance if it were
  genuinely restarting each frame.

**Leading hypothesis, not confirmed:** something after `bt_crumb(15)`'s write is looping forever
without returning to `bt_tick()`/the main loop — most likely an unbounded `fb_wait()`-style poll
inside `bt_draw_cell()` (called `bt_ops_per_tick[bt_lvl]` times per tick — 1, 4, or 16 depending on
concurrency level) that never sees its expected completion condition. A second possibility: a bug in
the 2-second window-expiry check itself (`(int32_t)(cycles() - bt_at) < 0`) that never lets `bt_op`/
`bt_lvl` advance — though nothing in that code (a standard, widely-used idiom elsewhere in this
project) stood out as broken on inspection.

**Concrete next step:** build a fifth ISSP probe capturing `bt_op`/`bt_lvl`/`bt_at`/`bt_ops_done`
directly (all `static` variables in `fw/suite.inc` — would need new MMIO-visible copies, same
pattern as `R_DBG_MARK`, since ISSP can only watch RTL signals, not arbitrary C variables) — or,
cheaper first step: extend `bt_crumb()`'s call sites inside `bt_draw_cell()` itself (currently
uninstrumented) to narrow down *which* cell within a tick is the one that never returns. That needs
no new hardware probe at all, just new checkpoint numbers and a rebuild — the natural first move
next session, before reaching for more JTAG infrastructure.

## Card and build state at handoff

- `TAU_0_5_0_A_12` carries the latest build (all three B-193 fixes, ROM from
  `work/diagnostics/library-diagnostic-profile/tau.rom` at commit `ee681b5`). `TAU`, `TAU_DIAGNOSTIC`,
  `TAU_DEV_42` untouched throughout.
- Every intermediate ROM/cold-image pair from this session's incremental fix-and-test cycle is
  backed up under `work/diagnostics/ab-test-pre-harpmudd/backup-*` — safe to delete once B-194 is
  resolved and nobody needs to bisect back through them.
- The ISSP debug bitstream currently loaded via JTAG (not on any SD card) is
  `~/tau-local/issp-pcaddr-s2-20260924/src/fpga/output_files/ap_core.sof` on the Quartus VM — the
  PCAD+DBGM+IFPS+BLIT four-probe build, 0 negative slack, confirmed working all session. Reload it
  with `quartus_pgm -m jtag -o 'p;<path>'` after any core reselect on the Pocket (which silently
  reprograms the FPGA from the SD card's own non-ISSP `.rbf`).
- All work committed through `ee681b5`. Not pushed.
