# Full audit and review — 2026-09-23

Requested by the owner: "a full audit and review of all the older logic, our current plan, and if
any other feature poses risks or might benefit from rework or enhancements." Scope: RTL timing
risk patterns, firmware correctness/drift risk, SDRAM/audio invariant test coverage, and plan/docs
consistency. Conducted via three parallel research passes (RTL, firmware, SDRAM/audio) plus a
direct review of the plan documents. No code changed as part of the audit itself, except the doc
correction noted in section 4.

## 1. RTL timing risk — the B6/BAR bug (B-109/B-111) is one instance of a pattern, not a one-off

**Context:** this session found and fixed a real Quartus timing violation in `mp3_fb.sv`'s BAR
(meter column) logic — a comparison and subtraction computed combinationally off `cmd_q` (a
register that just latched a BRAM read) feeding straight into another register in the same cycle.
Fixed by retiming: compute the same logic off the *same raw BRAM read* on the *same clock edge* as
`cmd_q`, so it's already valid rather than chained after it. `docs/AUDIT_TRAIL.md` B-109/B-110/B-111.

**Finding: this is a recognizable pattern in `mp3_fb.sv`, and at least one more instance shares
the exact shape of the bug just fixed.**

Ranked by concern:

1. **`mp3_fb.sv` ~960-961 — blend write-back into `glyphbuf`, highest priority.** In `A_COPYRD`,
   `glyphbuf[...] <= blend_px(glyphbuf[...], p0_q, blt_blend_mode, blt_blend_alpha)` — a
   combinational read of the MLAB array feeds a per-channel multiply-and-clamp (`blend_ch`, run
   three times) and writes back into the *same array* on the same edge. This is the same class of
   bug as the one just fixed, arguably deeper (a multiply plus saturating clamp per channel), and
   it lands on the exact same `glyphbuf` write port already flagged as near-zero-margin since
   B-102. **This path only exists when `TAU_BLIT_BLEND` is built** — meaning the no-blend bisect
   build (B-110) never exercised it at all. **This changes the read of B-110's result:** the
   working theory recorded there was "removing blend relieved congestion" (a routing-pressure
   effect). It's at least as plausible that dropping `TAU_BLIT_BLEND` removed one input to
   `glyphbuf`'s write-data mux directly — i.e. a more direct causal fix, not a lucky side effect.
   **Not yet verified either way** — nothing in this audit re-ran `report_timing` on the *full*
   bundle asking specifically about this path's contribution. Worth doing before re-enabling
   `TAU_BLIT_BLEND` and assuming the same bisect will work a second time.
2. **`mp3_fb.sv` OP_SBLIT dispatch (~265-276, 389-390, 844-846) — same bug shape as BAR, not
   fixed by B-111.** `sblit_ext(q_w, q_sx)`/`sblit_ext(q_h, q_sy)` (multiply-by-3, shift, compare,
   clamp to 127) computed combinationally off raw `cmd_q` slices and registered into
   `char_w`/`char_rows_left` in the same cycle `cmd_q` becomes valid — structurally identical to
   the `bar_lit`/`bar_unlit` bug, on the same source register, just not caught by this session's
   builds because BAR happened to be the one Quartus reported as worst. **Recommend applying the
   same retiming fix here proactively**, since the technique and the risk are both already proven.
3. **`mp3_fb.sv` OP_CHAR dispatch (~761-763) — same shape, lower depth.** `char_base` computed via
   two compares, a subtract and a mux off raw `q_glyph`, registered same-cycle. ~3 logic levels;
   moderate risk, not urgent but the same fix would be cheap to apply at the same time as #2.
4. **`mp3_fb.sv` OP_BLIT/OP_SBLIT address adders (~798-799, 838-839) — lower priority.** Single
   25-bit adds off raw `cmd_q` fields, registered same cycle. One arithmetic op but a wide carry
   chain; worth a glance, not a rewrite priority.
5. **`eq_biquad.v` ~105-114, 134 — a variant of the pattern, already shipped in production.** An
   address computed via multiply+add feeds an asynchronous ROM lookup, whose output feeds a second
   multiply registered the same cycle. The file's own history shows the authors already split one
   multiply-from-accumulate specifically after a timing near-miss (+0.425 ns) — that fix didn't
   touch this address-then-ROM-then-multiply chain, which is arguably the deeper one. Uses
   LUT-based ROM/registers, not M10K/MLAB, so it's a variant rather than an exact match. No known
   L0 regression associated with the EQ historically (section 3) — flagged for awareness, not
   urgent, since it's already shipping without incident.

Everything else checked (`tau_sdram_wb_adapter.sv`, `tau_psram_bus.sv`, `tau_psram_async.sv`,
`tau_cdc_gray_ctr.sv`, `tau_sdram_arbiter.sv`, `tau_sdram_cpu_bridge.sv`, `sound_i2s.v`) contains no
memory arrays feeding this pattern — pure control/handshake/CDC logic, not a match.

**The `glyphbuf` write-data path itself (B-102's original finding) is still open** — B-111's fix
addressed the BAR arithmetic feeding into `char_fg`, not the CHAR/COPY fill path into `glyphbuf`
that B-102/B-109 originally flagged. Whether that path is now clean depends on candidate #1 above
being resolved by dropping blend, which isn't independently confirmed yet.

## 2. Firmware — clean overall, one real unexplained divergence worth escalating

- **BUG-001** (accented/NFD filenames) — confirmed still open in firmware; workaround is host-side
  tooling only (`tools/sync_media.py`). No change since it was logged.
- **The "45-char title clip" bug (issue 020)** — already fixed (`TITLE_MAX 80`,
  `track_artist[64]`/`track_album[64]`), docs referring to it as open are stale; all tag-parsing
  copies (`id3_text_body`, `id3v1_read`) are correctly bounds-checked against `out_size`/`cap[k]`.
  No overflow risk found anywhere in the paths reviewed.
- **No `TODO`/`FIXME`/`XXX` markers anywhere in `fw/`** — either genuinely resolved or tracked
  exclusively in `docs/AUDIT_TRAIL.md`/`docs/issues/`, not left dangling inline.
- **Build-target drift (the historical B-052 failure class) is now tooled, not just disciplined.**
  `release` and `player-library-diagnostic` compile an identical source list, differing only by
  `-D` flags — the safe pattern. `tools/check_cold_calls.py`, `tools/tau_data_slots.py`, and
  compile-time asserts equating row-height constants across list styles all exist specifically to
  prevent a repeat of B-052. Residual drift risk is low.
- **Heap/RAM headroom is healthy, not tight.** Release: 34,752 B gap vs. a 6,144 B floor (~5.7x).
  Diagnostic Build: ~29,648 B vs. a 4,096 B floor (~7.2x). The "4,096 B floor" some past entries
  cite as tight was a transient low point before Phase G moved ~24 KB into PSRAM — historical, not
  current.
- **Escalate: the B-082 boot-restore mismatch is not just UX friction.** `TAU` and
  `TAU_DIAGNOSTIC` differ on whether boot restores the last-playing album, and this was parked as
  cosmetic (B-082). But both builds call the exact same `lib_boot_restore()` through the exact same
  gate (`TAU_LIBRARY`, which is `1` in both) — **the divergence has no known mechanism in the code
  that's supposed to explain it.** This is a real, unexplained runtime difference between
  nominally-identical logic, not a build-macro asymmetry. Worth root-causing before it's dismissed
  further, since "two builds behave differently for no visible reason" is exactly the shape of bug
  that tends to resurface somewhere else once ignored.
- **B-093** (track-open blocks the UI, synchronous) — real, but already correctly scheduled as
  deferred until after the blit engine, not neglected.

## 3. SDRAM/audio invariant (L0) — coverage gap is real but currently low-exposure

- **The blit-storm Check test (PHASE_F_SPEC.md section 12.1) does not exist yet** — no
  `SR_T_`-tagged test references blit or `SDR_BUSY`/`0xBC` anywhere in `fw/suite.inc` or
  `tools/host/`. This matches what `docs/AUDIT_TRAIL.md` B-101 already states outright: deliberately
  deferred until the blit engine itself exists to generate the traffic pattern to test.
- **The SDRAM busy-cycle counter (B7, `tau_cdc_gray_ctr.sv`, MMIO 0xBC) has no firmware reader at
  all** — RTL-only, confirmed via grep. Same deliberate-deferral status.
- **The reference-renderer/pixel-diff fixtures and the `COLD_READY()`-style feature-bit fail-safe
  for the blit engine (section 12) are both still open**, distinct from the already-shipped
  `COLD_READY()` cold-code mechanism (not reused here, correctly not confused with it). The only
  fail-safe currently in place is incidental: the old `R_FB_GO` register only reads 2 of the new
  3 opcode bits, so an unrecognized `OP_BLIT` truncates to `OP_RUN` rather than hanging — lucky,
  not the designed mechanism section 12 calls for.
- **Current real-world exposure is low: zero firmware consumers issue blit commands today**, so
  the blit engine cannot yet cause a real audio-timing regression — the live risk right now is
  build-integrity (the open timing violations), not runtime L0 safety. New MMIO ranges are
  parameter-gated to be inert when their macros are off, matching the project's existing
  convention, and B-106 already demonstrated that gating is being actively verified rather than
  assumed (it found and fixed a real case where "enabled" would have been silently unreachable).
  Exposure becomes real only once firmware starts issuing blits during playback — which is exactly
  what section 12.1's test is meant to guard, and that guard doesn't exist yet.
- **Historical L0 track record is essentially clean.** ~59 underrun mentions across
  `docs/AUDIT_TRAIL.md`, the overwhelming majority reporting zero late underruns across many
  sessions/tracks/stress levels. One ambiguous early exception (L1/L2 on one track, one session)
  coincided exactly with track-restart/reload events and was never attributed to real SDRAM
  contention; a later classification fix and re-run showed zero late underruns on all tracks at
  every level. No confirmed case of a contention-caused late underrun exists in this project's
  history. The EQ specifically has no recorded L0 regression at all.

## 4. Plan/docs consistency

- **Fixed during this audit:** `docs/ARCHITECTURE_ROADMAP.md` section 4's MMIO decision described
  a planned `R_BLT_GO` register that was never actually built — B-103 reused the existing
  `R_FB_GO`'s widened opcode field instead, and `docs/MMIO_ALLOCATION.md` already had this right.
  The roadmap's decision note was stale relative to the as-built design; corrected to match.
- No other plan/implementation mismatches found in this pass. The recent B-109..B-111 sequence of
  corrections (each one fixing the previous entry's wrong assumption before acting further) is
  itself a healthy sign the documentation discipline is holding up under real pressure, not just
  in calm conditions.

## 5. Recommendations, roughly in priority order

1. **Before re-enabling `TAU_BLIT_BLEND` for another fit attempt: verify candidate #1** (does the
   blend write-back into `glyphbuf` directly explain the original violation, or was B-110's
   "congestion relief" theory right after all). A `report_timing` query on the full-blend build
   asking specifically about that path's fan-in would settle it in minutes, cheaper than another
   full Quartus fit built on an unverified assumption.
2. **Apply the same retiming fix already proven in B-111 to OP_SBLIT's dispatch (candidate #2)**,
   and optionally OP_CHAR's `char_base` (candidate #3) at the same time — same technique, same
   file, same session's context, low cost, removes a bug class that's already bitten once.
3. **Root-cause the B-082 boot-restore mismatch** rather than leave it parked as UX friction — the
   firmware audit found the two builds share identical code for this behavior, so "cosmetic, not
   blocking" doesn't currently have an explanation to back it up.
4. **No urgent action needed on the SDRAM/audio invariant gap** — it's correctly scoped as blocked
   on the blit engine's own completion, and current exposure is genuinely low. Revisit once
   firmware starts issuing real blit commands, not before.
5. **EQ's chained multiply/ROM path (candidate #5)** — low priority, flag for awareness only; no
   incident history, not blocking anything.
