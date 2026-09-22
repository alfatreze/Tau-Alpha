# Phase G: cold code and data out of on-chip RAM (specification)

**Status:** design only (2026-09-21, audit B-045). No RTL, firmware, card or VM change. Builds on `docs/ARCHITECTURE_ROADMAP.md` phase G and `docs/PSRAM_IMPLEMENTATION_PLAN.md`.
Labels: **[HW]** Pocket result, **[RTL]** read in source, **[SRC]** read in firmware, **[EST]** estimate to measure.

## 1. Problem
The CPU has 256 KiB of on-chip RAM (M10K 300 of 308 used [HW]) that holds **code and data**. The library build leaves a heap gap of 6,976 B against a 6,144 B floor, and the parked library items (`docs/MEDIA_LIBRARY_0.4_SPEC.md` section 14),
the blit engine and audio work all need RAM. On-chip RAM cannot grow; something must leave it. PSRAM (32 MiB, 32/26 cycles per uncached read/write [HW, KB-040]) is the only large, proven memory a CPU can reach.

## 2. What is in RAM now (library build, `fw.elf`, [SRC] measured 2026-09-21)
Code 147.8 KB, read-only data 19.1 KB, bss 62 KB (of which arena 24 KB, art coordinate maps 2 KB, PCM and rings), heap gap 7 KB.
| Code group (approximate, inlining attributes shared code to callers) | Bytes | Temperature |
|---|---|---|
| MP3 decoder (Helix kernels) | 29,660 | hot (audio) |
| FLAC decoder | 6,192 | hot (audio) |
| Main loop, `poll_input`, `ui_draw_dynamic`, refill, `load_track`, misc | ~50,000 | hot except `load_track` (runs between tracks, audio silent) |
| UI drawing (`ui_*`, `fb_*`) | 28,596 | mixed: the per-frame parts are hot, splash/idle/toast/loader/chrome are cold |
| Art decode (picojpeg glue, accumulator) | 10,572 | cold (track change) |
| Library (`lib_*`) | 8,884 | cold (browse, boot load) |
| Playlist (`pl_*`) | 8,332 | cold |
| Settings menu (`set_*`) | 4,668 | cold |
| Read-only data: Huffman tables 8.5 KB (hot), meter thumbnails 4.5 KB, help text, strings | 19,070 | thumbnails and text cold |
**Cold total [EST]: 35-45 KB of code and about 6 KB of data.** Hot code (decoders, per-frame UI, main loop) must stay in on-chip RAM.

## 3. Options
| | A. Overlays (firmware only) | B. Execute cold code from PSRAM through the I-cache (RTL) | C. Cold data to PSRAM (firmware only) |
|---|---|---|---|
| Idea | Copy a cold code group from PSRAM into a fixed RAM region on demand | Give the instruction bus a read path to PSRAM; code runs from PSRAM lines cached in the 4 KiB I-cache | Read-only tables and text read through the uncached window |
| Frees [EST] | sum(cold groups) minus the largest overlay, about 11-15 KB | all cold code, 35-45 KB | 5-6 KB (thumbnails 4.5 KB, help text, strings) |
| RTL / Quartus | none | one RTL change, two-three builds, full gates | none |
| Risk | I-cache coherence after each copy (needs a `fence.i` / cache flush), overlay-call trampolines, call graph discipline | new client on the PSRAM port, arbitration with the data window, timing, fetch-miss latency | low; already proven pattern (art buffer) |
| Test | rv32sim harness | simulation with the real CPU + PSRAM chip model, then hardware | rv32sim harness + Pocket |
**Recommendation:** C first (cheap and it builds the cold-image machinery B also needs), then B as the main step; skip A unless B is rejected. A frees about a third of B's memory and adds a maintenance burden (call-graph and coherence rules) that B does not have.

## 4. Design of B (the target)
### 4.1 Address map and linking
* New **instruction alias** `0x2400_0000..0x25FF_FFFF` (32 MiB; PSRAM byte offset = address - 0x2400_0000) for PSRAM, decoded **on the instruction bus only** [RTL: `iBus` is a Wishbone with CTI/BTE bursts into BRAM port A today, dBus has priority]. Bit 31 is clear, so calls from RAM (`0x0000_0000`..) reach it with the normal `auipc+jalr` sequence
  (the existing data window at `0xA400_0000` is out of ±2 GiB range of code and would fail relocation). The **data** window `0xA400_0000` stays uncached and data-only; nothing reads code as data.
* `fw/link.ld` gets a `.cold` output section: VMA in the alias at PSRAM offset 0x80_0000 (`0x2480_0000`; the first draft said `0x2410_0000`, which collides with the library index area 0x01_0000-0x41_0000, so it moved; cold data uses the same offset through the data window, `0xA480_0000`), LMA = a separate file. Functions are placed with an attribute (`COLD`) or by file (`library.inc`, `settingsui.inc`, `playlist.inc`, `art.inc` glue) via section names. `objcopy -j .cold -O binary` gives `tau-cold.bin`.
* Hot to cold and cold to hot calls are ordinary calls. No function pointers into cold code from hot interrupt-like paths (there are no interrupts; the main loop is polled).
### 4.2 Loading and fail-safe
* `tau-cold.bin` is a **data slot** (id 6, deferload, optional, filename `tau-cold.bin`), read at boot with the same 4 KiB windows as the library index, copied into PSRAM at its load address, with a header (magic, size, CRC32, build id equal to the ROM's build id) checked **before** the first call into it.
* Fail-safe (the project's pattern, KB-039): PSRAM proof first; a missing, stale or corrupt cold image switches the **cold features off** (menus, library, art) and the player still plays a single file/playlist through hot code. Entry points into cold code go through one-instruction checked stubs (`if (!cold_ready) return`), never a raw call.
* Version lock: ROM and cold image carry the same build id; a mismatch is refused (E-code on the Info page).
### 4.3 RTL (the only hardware change)
* iBus decode in `mp3_soc.v`: `i_is_psram = iADR[31:25] == 7'b0010010` (0x24-0x25 range) -> a **line-fill adapter** `tau_psram_ifill.sv`: accepts a Wishbone burst (8 x 32-bit, CTI incrementing, `WE=0`), issues eight single 32-bit reads on the existing PSRAM controller port (one outstanding request, held response, as the data window), acknowledges each beat, ends the burst. Read-only: writes are refused (ERR) so a stray store cannot corrupt code.
* Arbitration: the PSRAM port already serves the data window and the mailbox. Priority: mailbox (test only) > dBus window > iBus fill; an iBus fill waits for a data access in flight (the CPU stalls either way). One request at a time, so no deadlock: the CPU pipeline cannot wait on the iBus for something the dBus holds.
* Cost model [EST]: line fill 8 words x 32 cycles + overhead about 270 cycles (4.5 us at 60 MHz); the I-cache is 4 KiB (128 lines), so a cold path of 22 KB executed once costs about 700 fills = 3 ms, then hits. Loops smaller than the cache run at full speed.
* Counters (MMIO, next free offsets in `docs/MMIO_ALLOCATION.md`): PSRAM instruction fills and fill cycles, so the Info page can show what cold code costs (evidence rule).
* Timing risk: a new client on the memory path can cost the ~1.9 ns cliff seen in earlier fits (roadmap, upstream lesson); register the adapter, multi-seed the build.
### 4.4 What must not move
The MP3/FLAC kernels and tables, the sample push and FIFO refill, `poll_input`, `ui_draw_dynamic` and its per-frame helpers, anything that runs while the PCM FIFO can starve. A rule for the linker map: `COLD` is opt-in, and a build check lists every cold function reachable from the main loop's per-iteration path (must be empty).

## 5. Phases and exit criteria
| Phase | Deliverable | Needs | Exit |
|---|---|---|---|
| **G0 Measure** | Cold/hot classification with real call counts (rv32sim instruction profile of a play + browse + settings + track-change session); cold set and gain table; the exact PSRAM map. | none | table in this file, reviewed by the owner |
| **G1 Cold data + image machinery** | Data slot 6, `tau-cold.bin` builder (`tools/pack_cold.py`), boot loader with CRC and build-id check and fail-safe, `COLD_DATA` placement for meter thumbnails, help text and cold strings; Info row COLD; host tests (harness runs the loader over good and corrupted images); packaged as a numbered core. | none (firmware only) | frees 5-6 KB [EST]; Pocket run: Info `COLD OK`, thumbnails and help identical, negative cases switch the feature off |
| **G2 RTL: instruction fetch from PSRAM** | `tau_psram_ifill.sv`, iBus decode, counters, testbenches (adapter unit, real-CPU firmware running a function from the alias, injected faults, mutation checks like the existing PSRAM suites), `make test` additions. | RTL sim (no VM) | all simulations pass; `make test` passes |
| **G3 Bring-up build** | Quartus builds (seeds 1-4 by the pre-set rule), Diagnostic Build page "Cold code" (fetch test, soak of code executed from PSRAM with the stress pump), then the SDRAM-unchanged gate and the PSRAM P4 gates on the new bitstream. | VM launch approval | timing closed, all earlier gates re-pass, cold test soak 0 failures, late underruns 0 |
| **G4 Move cold code** | `COLD` on library, settings, playlist, help, idle/splash, art decode glue in steps; each step measured (heap gap, load times, underruns). | none | heap gap and load-time table; no audio regression |
| **G5 Release integration** | Two-zip release with the cold image; README/CHANGELOG; removal of the parked-item RAM constraint (resume by second, Diagnostic library page, fixtures, palette text colours). | owner | v0.4 |
Owner approvals: every VM launch (G3) and every SD-card write; RTL changes get the same gate discipline as the SDRAM and PSRAM work (multi-seed, regression against the shipped product bitstream, see B-021).

## 6. Predictions to record before hardware [EST]
G1: heap gap +5-6 KB; menus draw identically; boot +10-15 ms for the cold image copy. G2/G3: cold code runs bit-identically from PSRAM; first-use cost 3-5 ms per 22 KB path; steady state indistinguishable; no change to worst window access on SDRAM (373 cycles) and none to audio. G4: heap gap up by 35-45 KB in total.

## 7. Open decisions (owner)
1. Approve the recommended order C then B (skip overlays).
2. Instruction alias address `0x2400_0000` and cold base at PSRAM offset 0x80_0000 (`0x2480_0000` as code, `0xA480_0000` as data; 1 MiB reserved). Applied as proposed while the owner had not objected.
3. Slot 6 as a separate `tau-cold.bin` file (proposal) versus appending the cold image to `tau.rom` (rejected: the ROM must fit RAM at load).
4. Whether G2's counters go into this RTL change (recommended; they cost a few registers and give the evidence).
5. Whether to bundle any other RTL (the Phase B SDRAM busy-cycle counter is already planned for "the next build"): recommended yes, as a separate, independently testable block, but never two new bus clients in one change (roadmap rule).

## 8. Progress
* **Decisions 1-5 applied as recommended** (owner said to move on without changing them): order C then B, no overlays; alias/base as corrected above; separate slot 6 file; fetch counters in the RTL change; the SDRAM busy-cycle counter bundled as an independent block.
* **G0 (static part done, B-046):** classification by function group from the ELF; the runtime call-count profile is still to do (needs the CPU simulator with the real main loop, or Pocket counters from G2).
* **G1 built (B-046):** see `docs/AUDIT_TRAIL.md` B-046. `TAU_COLD=1` (library build): `.cold_data` at `0xA4800000`, `tau-cold.bin` (data slot 6) with 20-byte header (magic, version, size, CRC32, layout id), `tools/pack_cold.py` (builds the image and patches the layout id into the ROM), `fw/cold_core.h` (portable loader, host-tested with 12 cases), `fw/cold.inc` (boot load, PSRAM proof, feature-off fail-safe), meter previews and both help texts moved. Heap gap 6,976 -> 10,960 B (+3,984 B; the image is 5,136 B).
* **G2 built and simulated (B-047):** `mp3_soc.v` parameter `PSRAM_IFETCH_ENABLE` (default 0, identical netlist when off): instruction alias `0x2400_0000..0x25FF_FFFF`, second `tau_psram_bus` (read-only, classic beats; a cache line fill is eight consecutive beats), two-client arbiter on the existing PSRAM port (data window wins ties, a granted transaction is never interrupted, `IFETCH_GAP` idle cycles after each transaction), MMIO `0xB0 IF_N`, `0xB4 IF_CYC`, `0xB8 IF_CFG`. Real-CPU simulation `make test-rtl-psram-ifetch` (firmware `sim/fw_ifetch`): cold image of 12,152 B copied through the data window and read back; first cold call = exactly 8 beats (one line fill); a call from the cache = 0 new fills; a 3,000-instruction (12 KB) function refills on every run (3,008 beats each, 95,020 cycles = 31.6 cycles per instruction word, i.e. about 253 cycles per 8-word line); a call from cold code back into BRAM; 500 data-window loads interleaved with cold code (contention) correct; no DQ contention, no guard access, chip model 0 errors; the same test with the feature off reports NOFEATURE. Mutant `IFETCH_GAP=0` survives (the controller and wrappers tolerate back-to-back requests in simulation, the gap is kept as margin, see KB-024).
* **G3-prep firmware built (B-049):** the cold image now carries data, padding to 32 bytes and code (`.cold_text` linked at `0x2480_0000 + aligned data size`; `_cold_img_size` from the linker; `tools/pack_cold.py` extracts both sections and puts the layout id of both into the header and the ROM). `fw/cold.inc`: `COLD_TEXT` attribute, boot check of the bitstream feature bit (`IF_CFG`) and a probe call before cold code is enabled (`COLD_READY()`), errors E17/E18 shown on Info. Diagnostic Build page **Tests > COLD CODE TEST** (correctness of calls into and out of cold code, a cached call fetches nothing, 12 KB function refilled each run, cycles per instruction word). Target `fw/build.sh player-cold-diagnostic`, packager `--diagnostic --cold`.
* **G4 step 1 built (B-069):** `TAU_G4` (default off; on in `player-library-diagnostic` and `player-library-check`, override `G4=0`). `COLD_FN` (= `COLD_TEXT` when G4 and cold code are on) marks 25 library functions and 29 settings-menu functions (draw, choice lists, Info values, toggles, the Diagnostic tests; persistence, `set_input`, `set_close`, the per-iteration ticks and the soak stay hot). Entry gating: `lib_boot_load` runs only if `COLD_READY()` (else the library is off with E18); the menu opens only if `COLD_READY()`; every other entry is behind `lib_state == LIB_ST_OK`, `lib_src`, `lib_ui_open` or `set_open`. `tools/check_cold_calls.py fw.elf` lists every hot-to-cold call (19 today) for review. Result: heap gap **4,096 -> 21,584 B** (diagnostic) and **11,424 -> 25,920 B** (release-style), ROM 175 -> 157 KB; cold image 32 -> 51 KB (44,400 B code).
* **G4 steps 2-3 built (B-070):** `TAU_G4 >= 2` (default 2 in the library targets; `G4=1` reproduces step 1). Also cold: the playlist loader (`pl_load` with its parse, read, hash and by-hash reopen helpers) and the playlist overlay drawing (`pl_ui_*`), and the cover-art glue (`art_decode`, `art_sig_of`, `art_find_apic`, `art_find_flac_picture`, `art_flush_row`). Kept hot on purpose: the track-open path (`pl_open_*`, `pl_cmd`, used by every load, also by the library), `pl_name_read`, `pl_skip`/`pl_play_*`, `read_track_head`/`load_track` (every track), the boot loading bar, the idle screen (it is the failure display when the cold image is missing) and `art_need_bytes` (the picojpeg callback runs many times per cover). Gates added: `pl_load` (both call sites), `art_sig_of`/`art_decode` in `load_track`, and a toast when Start finds no cold image. Heap gap now **30,016 B** (diagnostic) and **34,240 B** (release-style). Fail-safe with no cold image: no playlist and no library (single files still play), no cover art, no menus (Start shows "MENU OFF: NO COLD IMAGE"). What is deliberately not moved: `ui_draw_dynamic` (per frame; waits for the blit engine).
