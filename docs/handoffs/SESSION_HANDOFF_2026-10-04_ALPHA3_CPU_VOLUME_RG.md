# Session handoff, 2026-10-04 (evening): v0.6.0-alpha.3 published, CPU headroom work, dB volume, ReplayGain (read this first)

Supersedes `SESSION_HANDOFF_2026-10-04_FLAC_RAM_CLUT.md` as the entry point (still correct for what it covers; the barcode session's `SESSION_HANDOFF_2026-10-04_BARCODE_TPG.md` is also still correct). Audit trail: B-576..B-599 in `docs/AUDIT_TRAIL.md`.

## 1. State at a glance

- **Released:** `v0.6.0-alpha.3` (GitHub pre-release, both zips, tag pushed). alpha.2 and alpha.1 releases stay; alpha.1 carries a "superseded, normal core does not boot" notice. Bitstream is `clut-rtl-b576` seed 2 (rbf_r `ed977c71...`): CLUT RTL fix merged and hardware-confirmed (DEV 86).
- **On the card:** `TAU`, `TAU_DIAGNOSTIC` (both alpha.3, smoke-tested, no audio issues), **`TAU_DEV_93`** (main as of B-599, the *tempo* build: dB volume + ReplayGain + tempo setting; media with ReplayGain tags on five test tracks; NOT yet run), `TAU_DEV_BARCODE_04` (the owner's barcode test core, left alone), `TAU_DEV_METER_01/03/14` (the owner's meter worktree: never touch).
- **`main`:** pushed through the alpha.3 work; the dB volume (B-598) and ReplayGain (B-599) commits and this documentation commit are pushed with it. Branches left: `clut-rtl` (merged; worktree `../tau-alpha-clut-rtl` can be removed), `meter-builder` and `test/720` (other sessions' work). `barcode-study` was merged and deleted.

## 2. What was done this session (all hardware-confirmed unless marked)

1. **CLUT RTL fix** (`tau_clut_wr.sv`): fit closed both seeds, DEV 86 covers and meter previews correct (B-576..B-578). **Release rebuilt and published as alpha.2 then alpha.3.**
2. **Four-times-repeated black screen root-caused (B-581, B-582):** nothing checked firmware against bitstream before shipping. The ROM now carries `TAUFWPAIR:<versions>`; `tools/check_fw_bitstream_pair.py` gates `install_dev_core.py`, `package_dev_build.py` and `make_release.py`; `make_release.py` sets `RAM_192K=1 CLK66=1 SDRAM_BUSY=1` itself; `check_heap_gap.py` builds into `work/heapcheck` (`TAU_BUILD_OUT`) and no longer overwrites `dist/` (B-585).
3. **CPU headroom, MP3 at 1.00x stereo: busy 55% to 44%.** New Check records `SR_T_LOAD` (24, window CPU load and the Subband software/hardware split) and `SR_T_LOAD2` (25, decode / meter feed / push / UI / FIFO wait). Findings: the hardware window handoff cost 5,300 cycles a slot because the CPU polled the unit's ~4,400-clock compute; **pipelined** (start/finish, next slot's DCT overlaps) busy 55 to 47 (B-587/B-588, DEV 88); **burst push** (one FIFO status read per burst) push 11 to 7, busy 44 (B-592/B-593, DEV 91). Both host-proven stream-identical and heard clean. Remaining non-decode: audio MMIO write per pair, about 5 points of bookkeeping, and meter drawing (not measured here: the Check page covers the meters; the Meter Sweep measures it, the meter worktree's area).
4. **Stress-pump noise fixed (B-590, confirmed clean on DEV 91):** the pump range overlapped display buffer 1 of the H2 double buffer (since 2026-09-27). Moved to 3..4 MiB; `sim/test_sdram_map_overlap.py` guards it.
5. **Barcode study merged** (B-594/B-595): TPG pixel-grid report codes. Check tags 26 and 27 are taken; **the next free tag for any branch is 28.**
6. **Cymo C1 done: dB-tapered volume (B-598, owner chose pure dB, 60 dB range):** 100 positions, 0.6 dB each, 0 = mute; 5 ms click-free ramp; step 3, default 94 (matches the old default loudness). **Existing saved volumes now sound quieter** (same number, different loudness). Host-tested incl. mutants; **not yet heard on a Pocket** (DEV 92 was never installed; it is inside DEV 93).
7. **Cymo C6 first half: ReplayGain (B-599):** ID3v2 TXXX and FLAC Vorbis tags, Settings > Audio > REPLAYGAIN Off/Track/Album (default Off; saved in the `SW_POL` word, bits 4-5), attenuate-only fold into the volume target, parsing in cold code (release heap gap 16,672 to 16,128 B), Info > REPLAYGAIN row. Host-tested against real mutagen tags and the real `flac_open()`. **Not yet run on a Pocket.**

## 3. Next steps, in order

1. **Test DEV 93 on a Pocket** (the owner): REPLAYGAIN Off/Track/Album on the Nausicaa Image Album (track 08 Battle track -9 / album -5; 03 Mehve -2 / -5; 11 Bird Person +4 / -5, positive; 01 track -6 with no album tag) and the Test Album FLAC `01 MacCunn FLAC 44k` (-7 / -3): loudness should differ audibly, the Info row should read `TRACK -9.0 DB` etc., a positive gain should do nothing at volume 100. Volume: no clicks on a change or hold, even steps, mute clean, default level near the old one. Tempo setting still works. Then merge into a release (alpha.4), or cut `-alpha.4` only after a smoke test as before.
2. **Cymo remaining:** gapless playback (C6 second half, firmware + tool), the RTL output stage (C3: dither, soft clipper, 24-bit), graphic EQ (C5), tempo improvements (parked: cap at 1.75x, headroom guard, pause shortening, persistence). The resampler is still Diagnostic-Build-only.
3. **Open defects:** `Track changes` Check failure (0 of 10 done on a queue of 11), accented filenames (BUG-001), heap-peak reads 0 on one build (not investigated).
4. **Meter worktree** (`meter-builder`): measures meter drawing cost; its merge will conflict only in docs/build files (trial merge, 2026-10-04) and must use Check tag 28 or later.

## 4. Traps and procedures learned (keep)

- Use `tools/install_dev_core.py` (dry run first); it now also refuses a mismatched pairing. Never hand-build with `--build-flags` containing spaces (commas only).
- `check_heap_gap.py` and `make test-host` no longer touch `dist/`; if `dist/` shows modified after a build, restore with `git checkout -- dist`.
- Parallel sessions: the meter worktree (`TAU_DEV_METER_*`) and a barcode core exist on the card; leave them.
- `fw/player.c` is edited by the meter worktree too: keep main-side CPU work to measurement (cycle counters) unless coordinated.
- Local skill entries added: KB-107 (diagnostic pump vs display buffers), KB-108 (pipeline a hardware helper), KB-109 (ROM version marker), KB-110 (burst FIFO push).
