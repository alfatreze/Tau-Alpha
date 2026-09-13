# SDRAM memory architecture decision

**Status:** Proposed for staged implementation  
**Decision gate:** Tau should not resume substantial feature growth until the
data-only SDRAM prototype passes on Pocket hardware.  Executing cold code from
SDRAM is a later, separate gate.

## Decision

Proceed with a hybrid memory architecture.

- Keep boot code, the decoder arena, audio DMA ring, stack, interrupts, and
  playback-critical code/data in the existing on-chip BRAM.
- Add a bounded CPU path to the existing external SDRAM controller without
  replacing that controller.
- Put framebuffer scanout ahead of CPU traffic and limit each initial CPU grant
  to one 32-bit access (two 16-bit SDRAM words).
- Prove the path first through diagnostic MMIO registers, then expose a cached
  data window and move cold workspaces. Do not begin with execute-in-place.
- Evaluate cold-code execution only after data migration is stable and measured.

This is an architectural capacity change, not permission to weaken the audio
reservations in `fw/link.ld` or the measured decoder arena in `fw/alloc.c`.

## Why this is now the project gate

The current firmware image and runtime allocations nearly meet inside a single
256 KiB on-chip RAM:

| Item | Current size |
|---|---:|
| `.text` | 135,508 B |
| `.rodata` | 15,816 B |
| `.data` | 764 B |
| ROM/load image | 152,088 B |
| `.bss` | 61,578 B |
| Image plus BSS, aligned heap start | 213,680 B |
| ID3/art DMA landing zone | 4 KiB |
| MP3 DMA ring | 24 KiB |
| Stack | 16 KiB |
| Remaining linker heap | 3,408 B |

The linker deliberately requires at least 1 KiB of the final gap. The rejected
settings prototype crossed that boundary. HarpMudd's later measured 9 KiB stack
would recover 7 KiB, but that is tactical headroom rather than a capacity model
for Tau's settings, richer UI, diagnostics, and future metadata features.

The current documented FPGA fit is also asymmetric:

| Resource | Used | Consequence |
|---|---:|---|
| ALMs | 5,587 / 18,480 (30%) | Logic is available for a small bridge. |
| DSPs | 11 / 66 (17%) | Not relevant to this change. |
| M10K blocks | 300 / 308 (97%) | New FIFOs must not consume M10Ks accidentally. |

These figures are from the reproducible, unmodified baseline compiled with
Quartus Prime Lite 25.1std on the Linux x86-64 build VM. The build completed in
42m 58s with zero setup/hold timing failures. The most constrained reported
hold slack was 0.025 ns in the fast 0C model, so clocking and clock-domain
crossing changes must be measured against this baseline rather than assumed to
have wide timing headroom.

The 256 KiB CPU RAM is the dominant M10K consumer. Moving variables into SDRAM
creates firmware address space immediately, but it does **not** lower the 300
M10K fit until the on-chip RAM itself can be reduced. Its inferred array depth
must currently be a power of two, so returning FPGA block RAM would require an
eventual 256-to-128 KiB reduction or a deliberate replacement of the inference
scheme. That is not part of the first prototype.

## Existing architecture

The core already has the hard part at the board boundary: a working 100 MHz
controller for the Pocket's 512 Mbit ×16 SDRAM. It is currently connected only
to `mp3_fb`.

```text
                         60 MHz                         100 MHz
VexRiscv I/D buses -> 256 KiB BRAM        mp3_fb engine -> sdram_fb -> SDRAM
                         |                     ^
                         +-> MMIO/draw FIFO ---+
```

Relevant properties found in the current sources:

- VexRiscv runs at 60 MHz; SDRAM and the framebuffer engine run at 100 MHz.
- The CPU has separate 4 KiB instruction and data caches with 32-byte lines.
  Cacheability is decided solely by CPU address bit 31.
- `mp3_soc.v` currently sends every instruction fetch and ordinary data access
  to BRAM. `0x8000_0000` is MMIO and `0xC000_0000` is an uncached BRAM alias.
- `sdram_fb.sv` is a single-master, 16-bit controller with full-page read
  bursts and bounded write bursts. It has no Wishbone port and no second port.
- `mp3_fb.sv` reads 512 words for each scanline. A fill must finish within about
  4,167 SDRAM clocks. Its internal dispatcher already gives scanout priority
  between drawing bursts.
- The 400×360 framebuffer occupies 184,320 words, or 360 KiB, starting at SDRAM
  word address zero. Reserving the first 1 MiB provides a simple guard and an
  aligned boundary for CPU-owned memory.

The generated cache RTL shows a one-way cache and downstream 32-byte line fills.
The first bridge does not need to implement a cache itself, but simulation must
verify the generated Wishbone burst/ack behaviour before cached SDRAM becomes a
linker target.

## Candidate designs

### A. Shared external arbiter plus CPU clock bridge — selected

Insert a small two-master arbiter between `mp3_fb` and the existing
`sdram_fb`, and cross CPU requests/responses with a one-outstanding-operation
handshake.

**Pros**

- Leaves the proven SDRAM command, refresh, and pin-driving state machine intact.
- Keeps CPU transactions short, so the framebuffer's added worst-case wait is
  bounded.
- Can be tested with a fake controller independently from the physical SDRAM
  implementation.
- Uses registers or explicitly forced MLAB storage rather than scarce M10Ks.
- Creates one reusable system-side interface for diagnostic MMIO, mapped data,
  and later instruction access.

**Cons**

- Adds a 60-to-100 MHz clock-domain crossing and ownership state that must be
  verified carefully.
- One 32-bit CPU access becomes two 16-bit transfers.
- A simple implementation leaves SDRAM bandwidth on the table and makes cache
  misses relatively expensive.

### B. Add a second native port to `sdram_fb` — rejected for the first pass

**Pros:** potentially more direct scheduling and burst coalescing.  
**Cons:** refresh, queued request state, streaming writes, completion signalling,
and all command priorities become multi-port concerns inside a controller already
proven on hardware. This has a much larger regression surface than an external
owner arbiter.

### C. Put CPU servicing inside `mp3_fb` — rejected

**Pros:** can reuse its existing scanline-first dispatcher.  
**Cons:** couples general system memory to the renderer, complicates tests, and
makes a future display-pipeline replacement also a CPU-memory rewrite.

### D. Replace the controller with a general Wishbone/LiteDRAM subsystem — defer

**Pros:** mature multi-master and burst concepts; potentially better throughput.  
**Cons:** highest timing, resource, integration, and hardware-validation cost.
With only eight documented M10Ks free, a generated controller/crossbar/cache
stack may not fit without first removing the existing BRAM architecture.

### E. Use only the measured 9 KiB stack — insufficient

This recovers 7 KiB and should be audited separately for Tau, including the
canary and whole-build stack-usage measurement. It can fund a minimal menu, but
does not change the long-term capacity ceiling.

## Proposed memory map

The exact decode will be frozen only after the bus simulation, but the intended
map is:

| CPU address | Use | Cache |
|---|---|---|
| `0x0000_0000–0x0003_FFFF` | Existing 256 KiB BRAM | Cached |
| `0x4000_0000–0x43FF_FFFF` | SDRAM physical address space | Cached |
| `0x4010_0000–0x43FF_FFFF` | Initial CPU-owned SDRAM region | Cached |
| `0x8000_0000` narrow page | Existing MMIO | Uncached |
| `0xA000_0000–0xA3EF_FFFF` | Optional CPU-owned SDRAM alias | Uncached |
| `0xC000_0000–0xC003_FFFF` | Existing BRAM alias for APF DMA | Uncached |

The first 1 MiB of physical SDRAM remains unavailable to the CPU even though the
framebuffer currently needs only 360 KiB. Cached and uncached windows must map to
the same physical offset explicitly; they must not depend on simply truncating
the CPU address.

The current broad `d_is_ram` decode accepts more aliases than intended. It must
be replaced with explicit BRAM, SDRAM, and MMIO selects **before Phase 2 begins**.
This is a hard gate, not parallel cleanup: a CPU access to the proposed
`0x4000_0000` SDRAM window currently aliases into BRAM through the low RAM
address bits, which could silently corrupt playback state.

## Selected staged implementation

### Phase 0 — preserve a reproducible baseline

1. Record current firmware section sizes and the documented Quartus fit.
2. Keep the shipping build and package unchanged.
3. Add no UI and change no linker reservation during the bridge work.

**Exit:** complete. Baseline `make firmware` and `make test` are established;
the original Tau package is hardware-tested and the unmodified RTL has a
successful, timing-clean Quartus 25.1std build. Build details are recorded in
`docs/FPGA_BUILD.md`.

### Phase 1 — diagnostic SDRAM access

**Current status:** the owner-locking arbiter is implemented as
`core/tau_sdram_arbiter.sv` and covered by `sim/tb_tau_sdram_arbiter.v`.
The test proves simultaneous-request framebuffer priority in both contention
directions, completion/data routing only to the selected owner, and a queued
CPU request taking the next idle slot. `core/tau_sdram_cpu_bridge.sv` and
`sim/tb_tau_sdram_cpu_bridge.v` additionally prove the asynchronous mailbox,
byte enables, 32-bit-to-two-halfword conversion, and mandatory read-burst
termination. The bridge test also deasserts `clk_sys` and `clk_sdram` resets in
both orders and proves that neither creates a phantom transaction. Both modules
are integrated into the physical controller path behind dormant diagnostic MMIO
registers; the current full Quartus verification build is the next gate.

Add two modules with deliberately small interfaces:

- `tau_sdram_cpu_bridge`: one latched 32-bit request in `clk_sys`, a
  toggle/acknowledge CDC, and a two-halfword sequencer in `clk_sdram`.
- `tau_sdram_arbiter`: owns the sole `sdram_fb` port and locks each transaction
  to either framebuffer or CPU until completion.

Initially, firmware reaches the bridge through address/data/control/status MMIO
registers. That proves read, write, byte-enable, reset, timeout, and contention
behaviour without changing the VexRiscv memory map.

Rules for the first arbiter:

- An asserted framebuffer request wins over a simultaneous CPU request.
- Once granted, an owner remains locked until its short transaction completes.
- A CPU grant is at most two SDRAM words. Larger copies are repeated grants, so
  scanout gets an arbitration point between every 32-bit access.
- CPU requests are not sent into `sdram_fb` while it is unavailable. The
  controller's one-entry request queue is not treated as a multi-master queue.
- All CPU-facing state uses registers or explicitly forced MLABs. Quartus must
  report no unexpected M10K increase.

**Exit:** walking-bit, address-alias, alternating-pattern, and CRC tests pass
across at least 1 MiB above the framebuffer guard while rendering and playing
MP3 audio.

### Phase 2 — mapped data and cold workspace migration

Connect the data Wishbone path to the same bridge and expose the cached SDRAM
window below bit 31. Start with linker `NOLOAD` storage: wait for SDRAM init,
zero the external BSS explicitly, and place only cold workspaces there.

The current ELF exposes an immediate, conservative candidate set:

| Symbol(s) | Bytes | Why it is a candidate |
|---|---:|---|
| `pl_text` | 12,288 | Playlist text/index construction, not decoder state |
| `pl_order`, `pl_off` | 1,024 | Playlist index arrays |
| `art_acc` | 11,040 | Artwork resampling accumulator |
| `art_xmap`, `art_yslot` | 2,048 | Artwork scaling maps |
| **Obvious subtotal** | **26,400 B** | About 25.8 KiB before smaller helpers |

These objects still need call-site and lifetime review; the table identifies
capacity, not an instruction to move them blindly. Keep `arena` (24,576 B), the
DMA ring, tag landing zone, PCM/audio state, stack, and time-critical decoder
state in BRAM.

**Exit:** at least 24 KiB is removed from BRAM runtime occupancy, the protected
audio reservations remain unchanged, and the previously rejected minimal
settings shell links without using that recovered space for unrelated features.

### Phase 3 — cold initialized data and cold code

Only after Phase 2 passes, add a linker section for selected read-only tables or
functions at the cached SDRAM VMA. The first loading method to prototype is a
single packaged ROM with the cold segment staged in otherwise uninitialized BRAM:

1. APF loads one contiguous ROM into BRAM as it does today.
2. `_start` copies the cold segment to initialized SDRAM before clearing BSS.
3. The staging area is then reused by normal BRAM BSS/reservations.

This retains the current package contract and requires no unbackpressured direct
APF-to-SDRAM load. Linker assertions must prove that the staging image does not
touch the active boot code or stack. If the cold payload eventually exceeds the
available staging window, a separately buffered SDRAM loader or compressed cold
segment becomes a new decision; it is not needed for the first XIP experiment.

Start with one non-critical, leaf-level UI function. Never move Helix decode,
PCM refill, target-read pumping, input polling, or audio/error recovery in the
first code experiment.

**Exit:** a cache-cold call and repeated cache-warm calls are measured during
playback; the chosen UI path remains responsive and no underrun or framebuffer
deadline regression is observed.

### Phase 4 — reconsider the on-chip RAM size

Data/code migration initially frees logical space inside the 256 KiB RAM but no
M10Ks. Consider 128 KiB BRAM only after a measured hot-set layout fits with:

- decoder arena,
- audio ring and tag landing zone,
- measured safe stack,
- boot and exception path,
- playback/refill hot code and data,
- explicit growth margin.

A 192 KiB inferred array previously failed to map cleanly. An explicit Intel RAM
primitive could be investigated as a separate alternative, but it must earn its
complexity with a real fit report. Do not combine that experiment with the first
SDRAM bridge.

## Verification plan

### RTL simulation

- Back-to-back reads and writes with every byte-enable pattern.
- Reset in idle, while waiting for a grant, and during an owned transaction.
- Independent `clk_sys` and `clk_sdram` reset deassertion in both orders, with
  no phantom operation and a successful first post-reset transaction.
- Repeated 60/100 MHz asynchronous phase relationships.
- Simultaneous framebuffer and CPU requests; framebuffer wins the boundary.
- CPU ownership followed by a framebuffer request; framebuffer wins the first
  idle slot after the accepted CPU transaction completes.
- CPU request held while the controller is unavailable or refreshing.
- Read-data routing only to the owning master.
- No owner switch until completion; no request loss or duplicate completion.
- Maximum CPU ownership bounded to two 16-bit words in Phase 1.
- Existing `tb_mp3_fb` remains unchanged and passing.

### Firmware diagnostics

- Fixed patterns: `0x00000000`, `0xFFFFFFFF`, `0xAAAAAAAA`, `0x55555555`.
- Walking ones and zeros.
- Address-as-data across row, bank, and guard boundaries.
- Byte and halfword writes preserving untouched lanes.
- CRC over at least 1 MiB, repeated after heavy framebuffer activity.
- Cached/uncached alias coherence tests before either alias is used by features.
- Per-session counters for CPU requests, maximum wait, timeouts, and failures.

### Quartus gate

- Timing closes at 60 MHz CPU, 100 MHz SDRAM, and 12 MHz video clocks.
- No unexpected M10K use; any increase from 300/308 requires an explicit review.
- Report ALM, register, MLAB, M10K, and worst-slack deltas against the baseline.

### Pocket hardware gate

- Highest-bitrate available MP3 playing continuously with each visualizer.
- Repeated artwork loads, playlist entry, seeks, pause/resume, and track changes.
- SDRAM pattern/CRC traffic running concurrently at the intended Phase 2 rate.
- Zero audio-underrun latches attributable to SDRAM traffic.
- No visible tearing, corrupt scanlines, missed frames, or stalled draw queue.
- A long soak, then a cold boot and repeat. FLAC remains outside this gate under
  the current project decision and must stay labelled unverified.

## Go/no-go criteria

Proceed from data-only SDRAM to cold-code execution only if all are true:

1. CDC and arbitration simulations pass deterministically.
2. Quartus closes timing without consuming unplanned M10Ks.
3. The Pocket stress matrix has no audio underruns or display corruption.
4. At least 24 KiB of protected BRAM address space is recovered from cold data.
5. The settings shell fits while decoder, ring, tag, stack, and heap protections
   remain intact.

Stop and reassess the controller or memory architecture if CPU transactions
cannot be bounded below the scanout margin, if the bridge costs an M10K the fit
cannot spare, or if audio/display faults appear under repeatable contention.

## Expected outcome

Phase 2 should provide roughly 24–26 KiB of near-term firmware headroom without
moving playback-critical state. That is enough to make settings and diagnostics
practical again. Phase 3 is the route to continued code growth. Phase 4 is what
could eventually return a large number of FPGA block-RAM resources; it is not an
automatic consequence of merely storing some variables in SDRAM.
