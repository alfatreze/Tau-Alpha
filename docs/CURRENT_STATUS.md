# Current engineering status

**Snapshot:** 2026-09-21 (evening). Tau **v0.3.0** is released (PSRAM in the bitstream, album-art buffer in PSRAM; two zips: TAU and TAU_DIAGNOSTIC; see `docs/SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md`). The text below is the v0.2.2 state and the SDRAM gate record, still correct for the SDRAM side. Earlier: v0.2.2 was released and installed as the base `TAU` core. The detailed history is
`docs/AUDIT_TRAIL.md`; how to continue is `docs/SESSION_HANDOFF_2026-09-21.md`. (The previous version of this file
recorded the investigation up to A-093; that record now lives in the audit trail, A-060..A-093.)

## Executive state

- **Product:** v0.2.2 = probe-free SDRAM-window bitstream (seed 2) + firmware with the in-app settings menu (Start),
  Info page, full-screen playlist and settings, the playlist buffers (13,312 B) in SDRAM behind the uncached CPU window,
  greyscale meter previews, speed list 0.85-1.20x. `dist/` equals the card. Heap gap 7,824 B (floor 6 KiB).
- **SDRAM CPU window: validated.** Root cause of the read failure (adapter re-accepting a finished Wishbone beat) fixed in
  A-093 and regression-tested; on the seed-2 RBF the whole-window coverage passes (52 address-line checks, three 1 MiB CRC rounds
  under drawing, worst access 359/350 cycles), a 32-minute soak passes (279,795,000 checks, 0 failures), real playback under the
  stress pump at R1/R2/R3 passes (0 late underruns, worst 373 cycles), the playlist survives all of it (A-126, A-128, A-129).
- **Diagnostic Build** (`TAU_DIAGNOSTIC`, not shipped): Info, Tests (window test 89 checks, read/write cycle statistics, playlist
  integrity, clear counters) and Stress (levels, timed soak, live status). It replaces the standalone probe and stress cores.
- **Not validated / deliberately open:** FLAC; stress on tracks 2-4 and other material; temperature; CL2/100 MHz margin
  (KB-021); speeds above 1.20x (decoder budget); artwork buffers still in BRAM; cached window and code in SDRAM (not started).
- **Parallel work:** PSRAM (audit series B-NNN, `docs/PSRAM_*.md`): P0-P3 done on the Pocket (10/10 starts), read-timing margin measured, P4 (CPU window at `0xA400_0000`) built and simulated and two Quartus builds running (B-018);
  its RTL, firmware and tests are committed (`25ae8b6`, `82f5f90`). Entry point: `docs/SESSION_HANDOFF_PSRAM_2026-09-21.md`. (Updated by the PSRAM session, 2026-09-21.)

## Evidence that closes the SDRAM gates (Pocket unless stated)

| Gate | Result | Record |
|---|---|---|
| Root cause and fix | adapter released one cycle late and re-issued each beat; second release cycle; 183-check matrix 0 failures | A-093 |
| Cost | uncached access about 48-50 cycles (0.8 us), worst about 360-373 | A-094, A-100, A-128 |
| Soak | 279.8 M checks, 32:00, 0 failures (seed 2); 264 M, 30:15 (seed 4) | A-097, A-128 |
| Coverage | 52 lines, 3 CRC rounds, 0 failures | A-100, A-128 |
| Contention with playback | 0 late underruns at R1-R3, 30-min in-menu soak PASS | A-102, A-128, A-129 |
| Probe-free product RBF | four/two seeds close timing (A-101, A-114); probe overlay removed by the A-113 macro split | A-101, A-112..A-114 |
| First data move | playlist in SDRAM: 5-, 240-, 256- and 188-track lists, fail-safe proven with a fault-injected build and a no-window RBF | A-105..A-112 |
| Release | v0.2.0 -> v0.2.2 installed and user-tested | A-130, A-131, A-135, A-137 |

## Memory budget (256 KiB block RAM)

Release image 160,956 B (89.3% of the image budget), heap gap 7,824 B; the Diagnostic Build gap is 4,224 B. The 24 KiB Phase 2 exit target
of the architecture note was not needed (A-096): moving the 13 KiB playlist bought the settings menu (about 4.4 KiB) and the previews. Remaining
cold BRAM data: `art_acc` (11 KiB) and its maps (2 KiB), which would add about 0.8-0.9 s per cover decode through the window (A-095).

## Where to look

- Rules, targets, procedures, PSRAM guidance: `docs/SESSION_HANDOFF_2026-09-21.md`.
- Architecture decisions: `docs/SDRAM_MEMORY_ARCHITECTURE.md` (status block at the top), `docs/PLAYLIST_SDRAM_MOVE_SPEC.md`,
  `docs/SETTINGS_ARCHITECTURE.md`, `docs/SETTINGS_RUNTIME_BUDGET.md`, `docs/MMIO_ALLOCATION.md`.
- Older issue records: `docs/issues/` (018 resolved by A-093, 019 resolved by A-088/A-091).
