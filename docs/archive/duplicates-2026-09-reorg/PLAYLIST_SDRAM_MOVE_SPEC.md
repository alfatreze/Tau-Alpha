# Spec: move the playlist buffers behind the uncached SDRAM window (A-103)

Status: **design only, nothing built.** Evidence labels: code-review (this survey), plus
the Pocket results already recorded in A-093/A-094/A-097/A-100/A-102.

## Goal
Free 13,312 B of CPU BRAM (`pl_text` 12,288 + `pl_off` 512 + `pl_order` 512) by placing
them in SDRAM through the uncached alias, without changing playlist behaviour. `pl_dead`
(32 B), `pl_count`, `pl_pos`, `pl_truncated` and all scalars stay in BRAM. This is the
first real data move; it is deliberately the cheapest one (A-095: tens of ms per load).
`art_acc` and the artwork maps are out of scope (A-095: +0.8-0.9 s per cover decode).

## Facts this design relies on
- Alias: CPU `0xA0000000 + P` reaches physical byte `P` for `P >= 1 MiB`
  (`tau_sdram_addr_decode.sv`; firmware already uses `0xA0000000 + physical`). Below
  `0xA0100000` is the framebuffer/guard region: never touch it.
- The window supports byte, halfword and word loads/stores (A-093 partial-write matrix).
- Cost about 50 cycles per access, worst about 370; passed soak, coverage and contention.
- **With the macro off the alias decodes into MMIO** (`TAU_PHASE2_WINDOW` unset, or an
  older RBF). Any store there is a stray MMIO write. The window must therefore be proven
  before the first access, and the default must be "no window, no playlist".
- Contents of SDRAM after boot are undefined; the section is NOLOAD.
- Only the CPU touches these buffers (`playlist.inc` copies from `tagbuf` with a CPU
  loop, `player.c` reads names for drawing); APF DMA never targets them. Confirm again
  with `grep` at implementation time.

## Placement
- Physical range: **`0xA0100000..0xA0103400`** (13,312 B, 4-byte aligned), i.e. the very
  start of the CPU-owned region. Keep 0xA0200000 (A-093 test base), 8 MiB (A-100 CRC) and
  1-2 MiB stress ranges unused by the product; the stress cores are separate builds.
- `fw/link.ld`: add `MEMORY { sdram : ORIGIN = 0xA0100000, LENGTH = 0x3400 }` and
  `.sdram (NOLOAD) : { *(.sdram .sdram.*) } > sdram`. Objects use
  `__attribute__((section(".sdram")))`. NOLOAD means the ROM image is unchanged in size
  and nothing is initialised by the loader.
- Add `ASSERT` that the section fits and that `_min_heap` slack is not reduced.

## Firmware changes (guarded by `TAU_PL_SDRAM`, default off)
1. Declarations only: the three arrays get the section attribute. No call sites change:
   the compiler emits ordinary `lw/lh/lb/sw/sh/sb` to fixed addresses, which is exactly
   what the window supports. (Do not make them `volatile`; ordering is program order on
   this in-order CPU and the region is uncached. Re-check the generated code for `memcpy`
   or word-merging changes at `-O2`.)
2. `pl_sdram_ok` (BRAM, uint8_t, initially 0). Set only by `pl_sdram_init()`.
3. `pl_sdram_init()` runs once, after SDRAM init is reported ready and before the
   playlist is first loaded: the same preflight as the stress pump (mailbox write of a
   pattern to the physical word, CPU-window read of it must match; then a second pattern
   through the window and back through the mailbox). Then write/read back the first and
   last word of the section with two patterns. Only then `pl_sdram_ok = 1`.
4. `pl_load` and the playlist overlay check `pl_sdram_ok`. If 0: **fail safe by
   feature-off** (no playlist: single-file playback still works; toast
   "NO SDRAM PLAYLIST"; `pl_count = 0`). A BRAM fallback copy is rejected because it would
   keep the 13 KiB we are trying to free.
5. Optional cheap speed-ups, only if the regression shows a visible delay: copy in
   `playlist.inc:452` word-wise; keep parsing as is.
6. The existing playlist hash (`playlist.inc` near line 1013) is reused as the
   correctness check: compute it on the BRAM build and on the SDRAM build for the same
   `.m3u`; they must be equal.

## RTL / packaging
- Product RTL = A-101 seed 4 (`TAU_PHASE2_WINDOW=1` only). Raw sha256
  `ed34a6bc...90eb`. No RTL change needed. Re-run `make test-rtl` and
  `make test-rtl-sdram-wb-return` unchanged as a guard.
- New packaging: normal TAU core JSON with the new ROM and the seed-4 RBF, as a
  **separate test id** (like `tau_sdram_wst`), never overwriting the normal product on
  the card. Add `--playlist-sdram` mode to the packager only when the ROM builds.
- ROM: `fw/build.sh player-sdram-pl` (new target, defines `TAU_PL_SDRAM`); the product
  target stays byte-identical (verify SHA-256 before/after, as done for `_min_heap`).

## Expected link result
BRAM text/bss shrink by 13,312 B, so the heap gap grows from about 2.4 KiB slack to about
15.7 KiB (report the exact figure from the link map). That is the space the 7-row
settings menu needs (3.0 KiB); do not spend it on anything else in this change.

## Hardware regression matrix (Pocket, in order; each row recorded in AUDIT_TRAIL)
| # | Test | Pass |
|---|---|---|
| 1 | Negative: new ROM on an old no-window RBF (A-093 probe RBF is fine as a stand-in without window? no: use the A-080-era product RBF) | Preflight refuses, toast shown, no MMIO side effect, player otherwise works |
| 2 | Cold boot, 5-track list (the A-102 test playlist) | Same track order and names as the BRAM build; hash equal |
| 3 | 240-track list (largest historical, 10.6 KB) | All rows drawn, hash equal, load time <= 100 ms more than BRAM |
| 4 | Truncation: list larger than the buffer | `pl_truncated` set as in BRAM build |
| 5 | Shuffle on/off, next/prev across the list, dead-entry skipping | Identical sequence to BRAM build for the same seed |
| 6 | Overlay scroll while playing 320 kbps + 1400 px cover | No audible dropout, L0 underruns (use a build with the A-102 E/L counters if practical) |
| 7 | Seek, pause, cover change, playlist reload during playback | No stall, no corruption |
| 8 | Cold-boot repeat x3 | Same results |
Any mismatch, garbage name or missing track fails the gate. Record window `M` if the
HUD counters are present.

## Risks and open points
- Worst access 6 us inside the audio path: rows 6-7 measure it; A-102 says it is not a
  problem, but that ran the pump, not the overlay.
- Setup slack is +0.39 ns (seed 4); temperature is untested (open item 5 in the
  handoff). Not changed by this work.
- The A-102 HUD counters are stress-build only; for row 6, either accept audible
  monitoring plus the existing underrun flag, or port a minimal `E/L` counter (decide when
  implementing).
- Row 1 needs an RBF without the window. Confirm which card-resident RBF that is before
  scheduling it (the normal TAU RBF on the card is a candidate; check its build macros).

## Decision pending after this: scaling blit engine vs cached window
Not decided here. Since the playlist move alone frees enough for the settings menu, the
choice depends on whether `art_acc` (11 KiB) or running code from SDRAM is the next
goal; a spec comes first either way.
