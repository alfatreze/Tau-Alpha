# Upstream knowledge for full Pocket SDRAM, PSRAM and FPGA audio

**Source:** `harpmudd/HarpMudd.mp3player` at v1.5.0 (`4288dc1`), read from a
scratchpad clone. Tau baseline is v1.4.0 (`7ef8f0f`).

**Scope decision (2026-09-19):** all other upstream features (lyrics, scrobble
export, AAC, playlist cap, VBR seek, more meters, etc.) are deferred until
SDRAM, PSRAM and FPGA audio support are settled. They are deliberately not
recorded here.

**Evidence labels:** **[RTL]** read in upstream source; **[HW]** upstream
reports hardware confirmation; **[DOC]** upstream prose only; **[INF]** my
inference, not upstream's claim.

## 1. SDRAM

### Facts
- The part is 512 Mbit x16 = **64 MiB**. `sdram_fb`'s address is 25 bits of
  16-bit words (2^25 x 2 B = 64 MiB). **[RTL]** `mp3_fb.sv:18` says 64 MB, while
  an older comment elsewhere says 32 MB; upstream flagged the 32 MB as stale.
  Tau's "63 MiB coverage" is consistent with 64 MiB.
- `sdram_fb` (agg23's controller) runs at **100 MHz on its own PLL output**.
  It is instantiated in `core_game.vh`, not `core_top.v`. `USE_SDRAM=1` in the
  `.qsf` compiles the `dram_*` tie-offs out; reading only `core_top.v` gives
  the wrong answer. **[RTL]**
- Framebuffer: 400x360 RGB565, 512-word stride, about 360 KB at the bottom of
  SDRAM. **[RTL]**
- Upstream's CPU has **no** read/write access to SDRAM. Its only path is a
  write-only draw-command FIFO (`RUN`/`RECT`/`CHAR`). **[DOC]**
- Upstream **never built** a general CPU SDRAM port. It is queued as the
  "1.6.0 SDRAM migration". Tau's CPU window is ahead of upstream here, so there
  is no upstream reference implementation to compare against. **[DOC]**

### Real-time constraints (from upstream's design notes)
- Scanout `FILL` has a hard deadline of one scanline, about 4,167 cycles at
  100 MHz, and **always wins arbitration**. **[DOC]**
- Audio would be a second real-time client on the controller that currently
  serves every pixel. **[DOC]**
- Upstream's planned order for moving data into SDRAM: `pl_text` (12 KB, cold,
  sequential) then `art_acc` (11 KB, load-time only) then the audio `ring` (24 KB)
  behind a BRAM staging FIFO so the decoder does not hit SDRAM per byte. It
  cannot move: the Helix `arena` (random access), the tag buffer (DMA landing
  zone the bridge writes into) and the stack. **[DOC]**
- "Do not attempt the port and the decoder in the same change." **[DOC]**

### Bridge to SDRAM write path (shipped in v1.5.0) **[RTL][HW]**
This is not Tau's CPU window; see the comparison table at the end.
- A second `data_loader` on bridge window `0x10000000` (4 MB window, 21-bit
  address, 16-bit words) delivers words already in `clk_sdram` to the draw
  engine (`fontw_en/addr/data`). The engine writes them to SDRAM between
  drawing and scanout. Instantiated in `core_game.vh`.
- The engine writes at `FONT_BASE = 25'h0100000` (word address, so byte
  **2 MiB**), about 833 KB, so roughly 2.0 to 2.8 MiB. Region 1 (1bpp) starts at
  `FONT_BASE + 65536` words.
- The CPU never reads it. It sends a glyph number in a `CHAR` command, encoded
  as glyph `0x7F` with the index in the otherwise-unused 18-bit size field, so
  no wider FIFO entry and no SoC change.
- Writes are bursts of up to 128 words in one request. **[RTL]** `mp3_fb.sv:553`

### Two bugs upstream's simulation caught (relevant to any new SDRAM client)
1. **Starvation:** a font burst stream kept "something queued" true almost
   always, so a `CHAR` started during the load did not finish until the load did.
   Fix: font bursts wait for 64 queued words whenever drawing is pending.
2. **Timing:** a newly added pipeline path took slack from +1.029 ns to
   **-1.888 ns** at 100 MHz. Fix: split into two registered stages. The last
   pixels of a row land up to two cycles after the state machine returns to
   IDLE; anything reading them must be several cycles behind.

### Collision to resolve before merging v1.5.0 **[RTL][INF]**
Upstream's font occupies SDRAM from byte 2 MiB to about 2.8 MiB. Tau's
diagnostic window is **2 to 3 MiB**, and its preflight word is word `0x00100000`
(2 MiB). Tau is on v1.4.0, so nothing collides today. Any merge of v1.5.0, or
any Tau feature that adopts the font, needs a new SDRAM map first. Suggested
next step: write the map (framebuffer, font, Tau diagnostics, future buffers)
in `docs/SDRAM_MEMORY_ARCHITECTURE.md` before touching either side.

### Alternative architecture, if the CPU read-return path stays stubborn **[INF]**
Upstream's proven pattern reads SDRAM only inside the engine (hardware fetches
rows on a command). An engine-side "fetch N words into a BRAM mailbox on
command" would let the CPU read SDRAM data without a direct return latch across
domains. It has not been built or evaluated. Mentioned only as an option.

## 2. PSRAM

- **Never used upstream.** In `core_top.v` the `cram0_*`/`cram1_*` ports are
  tied off unconditionally (unlike SDRAM, there is no `ifdef`). **[RTL]**
- Upstream's description: 32 MB total, two chips, dual die, with `bank_sel`
  picking the die. **[DOC]** I did not verify these numbers in the RTL.
- The ports are `cram*_a[21:16]` (6 address bits driven by the core) plus a
  16-bit `dq`, with `wait`, `adv_n`, `cre`, `ce0_n/ce1_n`, `oe_n`, `we_n` and
  `ub_n/lb_n`. **[RTL]**
- Upstream offers no PSRAM controller, timing, or hardware evidence. Tau's
  `docs/PSRAM_EVALUATION_PLAN.md` starts from zero prior art in this repo.
  Nothing else here changes that.

## 3. FPGA audio

### Facts **[RTL]** unless marked
- Chain: firmware `pcm_push` then `pcm_fifo` (in `mp3_soc.v`, `AW(11)` so
  2,048 samples) then `audio_l/audio_r` then `sound_i2s` (agg23's, in
  `core_game.vh`), which crosses into `clk_74a` internally.
- `pcm_fifo` and the whole SoC are in `clk_sys`, so the FIFO to `sound_i2s` hop
  adds no new CDC.
- The FIFO drains in hardware at the programmed rate via a fractional
  accumulator whose carry is the sample strobe.
- `underrun` is a **sticky** latch cleared only by `flush` or reset. Firmware
  uses it for a fade. It reads as "at least one underrun since the last
  flush", not a count. **[RTL]**
- **Start-of-track priming (v1.5.0, absent from v1.4.0):** the FIFO waits for a
  half-full cushion (about 23 ms at 44.1 kHz) before the first sample leaves.
  It is cleared only by flush/reset, and an underrun raised while unprimed does
  not set `underrun`. It fixes a click at second 0 that was measured on
  hardware as an output discontinuity of about 50. **[RTL][HW]**
  Testbench: `sim/tb_pcm_fifo.v`.
- The FIFO holds the audio path independent of decoder timing, so audio
  glitches can be a stream problem rather than starvation. Upstream's FLAC
  stutter left the FIFO full, so no underrun was ever recorded. Do not read
  "U0" as "no glitch". **[DOC]**

### Planned EQ (designed, later built as presets) **[DOC]**
- Sits between `pcm_fifo` and `audio_l/audio_r` in `mp3_soc.v`; `core_top.v`
  and `sound_i2s` unchanged, no new CDC.
- Budget: output is 48 kHz and `clk_sys` is 60 MHz, so about 1,250 clocks per
  output sample. Ten bands x two channels at 5 MACs each is about 100 MACs, retired
  by one pipelined multiplier in about 150 cycles (about 12% of the time), using one
  DSP block and no CPU cost. Doing it in firmware would consume roughly 6.6 to
  8.8 M instr/s, essentially all the free CPU budget.
- Generate coefficients offline in Python. A hand-written table cost a hardware
  round on the VU needle sine table.
- Any RTL change needs a Quartus compile and a timing-closure round.

### Audio as a future SDRAM client
Upstream identified the audio ring as the largest buffer worth moving to SDRAM
and the riskiest: it would be a second real-time client beside scanout. The
proposed mitigation is a BRAM staging FIFO so the decoder never waits on SDRAM.
No upstream design exists beyond that sentence. **[DOC]**

## 4. Block RAM ceiling (constrains all three areas)
- **300 / 308 M10K blocks (97%)** in v1.4.0/v1.5.0. Bit usage (75%) is
  misleading because blocks are allocated whole. **[DOC]**
- Small arrays: upstream forced two 8-word arrays into logic with
  `ramstyle "logic"` to hold the count at 300 (without it Quartus inferred an
  M10K, 300 to 301). **[DOC]** Expect Tau's PSRAM/audio work to need the same
  discipline, or to free blocks by moving buffers to SDRAM.
- This ceiling is why upstream's framebuffer and font live in SDRAM at all.

## 5. Path comparison (why "bridge to SDRAM" is not the CPU window)

| | Upstream bridge to SDRAM | Tau CPU window (`0xA0200000`) |
|---|---|---|
| Initiator | APF data slot through `data_loader` | CPU loads/stores |
| Reads by CPU | none (engine fetches rows on command) | required (the failing readback) |
| Where arbitration sits | draw engine sequences font vs draw vs scanout | owner-mux |
| Status | shipped, hardware-confirmed 2026-09-17 | unresolved return-latch boundary |
| Useful to Tau as | proof the SDRAM arbiter accepts a second non-scanout writer; starvation and timing lessons | n/a |

## 6. Open questions upstream does not answer
- Any CPU-side SDRAM read latency or return-path design.
- PSRAM controller timing, die selection, or bank behaviour on hardware.
- Multi-client SDRAM arbitration with audio deadlines.
- Whether the 64 MiB figure has been exercised end to end (upstream used only
  about 3 MiB; Tau's diagnostic covers up to 63 MiB).
