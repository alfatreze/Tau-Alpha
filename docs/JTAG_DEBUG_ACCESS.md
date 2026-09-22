# JTAG debug access from the Mac (USB-Blaster + Quartus VM)

Status: **working, verified 2026-09-21** (programming only; no debug logic in any shipped bitstream yet).

## 1. Setup that works

- Cable: Terasic USB-Blaster (USB `09fb:6001`), wired to the Pocket's FPGA JTAG header
  (bottom edge, behind the rear cover, T6 screwdriver; pin 1 marked in the Analogue docs).
- macOS has no Quartus. The cable is passed through UTM into the Quartus VM
  (UTM VM settings > Input > "Share USB devices from host" on; then with the VM running, the
  toolbar USB icon > USB-Blaster > **Connect...**; "Auto connect on start" is optional).
  Do not pick "USB JTAG/serial debug unit".
- VM (see `SESSION_HANDOFF_2026-09-21.md` section 4 for ssh): needs one udev rule, already
  installed: `/etc/udev/rules.d/51-usbblaster.rules` =
  `SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", MODE="0666"`. Without it `jtagconfig` says
  "Insufficient port permissions". Installing it needs the VM sudo password (a person types
  it; the assistant does not enter passwords).
- Tools live in `/home/taualpha/intelFPGA_lite/25.1std/quartus/bin`
  (`jtagconfig`, `jtagd`, `quartus_pgm`, `quartus_stp`; add `system-console` if present).

## 2. Procedures

Check the chain (Pocket powered, a core running):

    jtagconfig -n
    # expect: USB-Blaster variant [5-3] / 02B050DD  5CE(BA4|FA4)

Reload a bitstream without touching the SD card (about 5-10 s):

    cd <stage>/src/fpga/output_files
    quartus_pgm -m jtag -o "p;ap_core.sof"

- Every Quartus build already writes `output_files/ap_core.sof` (about 2.4 MB) next to the RBF.
- A core for the same platform must already be installed on the card and running. The Pocket
  detects the heartbeat loss and repeats the whole load with the same start conditions
  (verified: the core reloads). JSON files are re-read; other state is lost.
- The `.sof` must be the same build as the RBF on the card unless a different behaviour is
  intended; the card content and the loaded bitstream then differ, so record which was used.
- Only genuine or well-behaved Blasters are advised by Analogue; observe ESD care.

## 3. What JTAG can and cannot give (decision record)

| Need | Available now | Needs a design change |
|---|---|---|
| Fast reload of a build | Yes (section 2) | - |
| Internal signals over time (SDRAM/PSRAM controller, CPU bus, video timing) | No | SignalTap in the build (`quartus_stp`) |
| Read/write a few live registers | No | In-System Sources and Probes |
| Read/write on-chip RAM while running | No | In-System Memory Content Editor |
| Read any memory (SDRAM/PSRAM, frame buffer) and write MMIO from the Mac | No | JTAG-to-Avalon master (`altera_jtag_avalon_master`, driven with `system-console`) |
| CPU text log to the Mac | No | JTAG UART in the SoC plus firmware |

The screen itself is not visible over JTAG: the frame lives in SDRAM, which is outside the
JTAG chain unless the master above is added.

## 4. Planned change (roadmap)

Owner decision 2026-09-21: add a **debug-access phase immediately before the blit engine**
(`docs/ARCHITECTURE_ROADMAP.md`, Phase F0), so the GPU/blit work can be debugged and
measured through JTAG. Scope to decide at that point: (a) SignalTap trigger set,
(b) JTAG-to-Avalon master on a slow side path (memory and frame reads), (c) optional JTAG UART.
Constraints to check first: free M10K blocks and ALMs (SignalTap sample RAM and the master
use fabric and RAM), the -1.888 ns style timing cliff on 100 MHz paths, and that it stays
behind a macro so release builds are unchanged (release stays probe-free, as in A-113).
The Diagnostic Build is the natural carrier.

## 5. SignalTap plan for the next diagnostic build (planning only, nothing built)

### 5.1 Free resources (fit of the B-018 P4 diagnostic, seed 2, the current diagnostic RBF)

| Resource | Used | Free | Consequence |
|---|---|---|---|
| M10K blocks | 300 / 308 (97%) | **8** (about 80 kbit) | Not enough to share with the product; use MLAB for sample buffers |
| MLAB memory | 0 | any LAB that is not full | Buffers can live in ALMs (one MLAB is 32 x 20 bits per LAB) |
| ALMs | 6,459 / 18,480 (35%) | about 12,000 | Room for trigger logic plus roughly 100-200 kbit of MLAB buffer |
| Registers | 8,487 | plenty | Capture pipelining is free |
| Pins | 224 / 224 | 0 | Irrelevant: SignalTap uses the dedicated JTAG pins |
| Setup slack | +1.505 ns (seed 2) | - | Margin exists for a 100 MHz tap; the older cliff was -1.888 ns |

Clocks: 60.02 MHz system, 100.04 MHz SDRAM/PSRAM, 12 MHz video, 74.25 MHz bridge.

Budget rule: **all sample buffers in MLAB** (SignalTap "RAM type = MLAB"), never M10K, so the
memory map and the 300/308 count stay unchanged. Starting size: 64 signals x 1024 samples
(65 kbit, about 100 MLABs, about 1,000 ALMs) per instance; at most two instances.

### 5.2 Instances (sample clock -> what to capture)

1. **SDRAM CPU window** (100 MHz): `tau_sdram_wb_adapter` request/ack/data, the
   controller command bus (active/read/write/precharge/refresh), scanout-owner flag,
   arbiter grant. Question answered: cycles per access and what delays the 360-cycle worst
   case (A-094/A-100 counters only give totals).
2. **PSRAM window** (100 MHz): `tau_psram_bus` request/ack, CE#/OE#/WE#/ADV#/LB#/UB#, cram_a and
   DQ direction, the read-sample strobe. Question answered: the tAADV/tOE reading behind
   the T_ACC 6/7 result (B-015) seen on a real waveform.
3. Optional third (60 MHz, only if timing allows): CPU bus (`mp3_soc` iBus/dBus valid/ready) around
   a stall, to line up with 1 and 2 using a shared trigger.

Trigger set: a CPU access into 0xA000_0000 or 0xA400_0000, a mailbox fault flag, the
`late` audio underrun, and "any ack timeout". Use segmented capture (for example 8 x 128)
so several events fit one run.

### 5.3 Build and flow

- Macro `TAU_SIGNALTAP` in the staged qsf only (`tools/psram_probe_qsf_append.txt` is the
  model): `ENABLE_SIGNALTAP ON`, `USE_SIGNALTAP_FILE tau_diag.stp`, and register the `.stp`
  in the qsf. Signals must be kept (`preserve`/`noprune`) with the same pad-register rule
  as B-008 so I/O packing does not change; check that nothing is packed differently.
- Build on the VM as usual (about 45 min, seeds 1 and 2), then `quartus_fit_summary.py`,
  and require: no negative slack, M10K still 300/308, RBF/`.sof` match.
- Run: `quartus_pgm ... ap_core.sof` (core reloads), then arm from the Mac side with
  `quartus_stp` (batch, script `tools/signaltap_*.tcl`) or the GUI capture in the VM,
  operate the Pocket, read the waveform, export CSV for the decoder tools.
- **Never ship a SignalTap RBF.** It is a diagnostic-only build, named as such
  (`TAU DIAG ST`), and the release/diagnostic zips keep the probe-free RBF.

### 5.4 Open questions before building

1. The `.sof` reload restarts the core, so a trigger armed before the reload is lost. Confirm
   the arm-after-reload sequence works with the heartbeat restart.
2. Whether the Pocket shows a video/heartbeat problem while SignalTap holds the JTAG chain
   (test with an idle instance first).
3. A first, tiny build (one instance, 32 signals) to prove the flow and fit cost before the
   full set. Recommended as the first step.

### 5.5 Proof-build files (prepared 2026-09-21, nothing built or launched)

| File | Role |
|---|---|
| `src/fpga/core/tau_signaltap_tap.sv` | 32 preserved registers `tap[n]` on `clk_sys` (stable node names); not in `ap_core.qsf` |
| `src/fpga/core/mp3_soc.v` | `ifdef TAU_SIGNALTAP` instance `g_phase2_window.u_stp_tap` (12 lines; compiled out otherwise) |
| `tools/gen_signaltap_stp.py` | writes `tau_proof.stp` (one instance, 32 taps, depth 2048, MLAB, trigger = rising edge of bit 0) |
| `tools/signaltap_proof_qsf_append.txt` | staged-qsf lines: `TAU_SIGNALTAP`, the tap file, `ENABLE_SIGNALTAP`, the `.stp` |

Tap bit map: 0 CPU beat into the SDRAM window, 1 write, 2 wb_ack, 3 unsupported, 4 bridge_req,
5 bridge_write, 6 accept, 7 done, 8-27 bridge_addr[19:0], 28 CPU ack, 29 bus error, 30 rst,
31 bridge_rdata[0]. Cost: 65,536 MLAB bits (about 100 MLABs) plus 32 registers; no M10K.

To run (needs owner approval, as for every VM build):
1. Stage a copy of the tree as `/home/taualpha/tau-local/signaltap-proof-<date>` (same as B-018
   diag), copy `tau_proof.stp` to its `src/fpga/`, append `tools/psram_window_qsf_append.txt`,
   `SEED 2` and `tools/signaltap_proof_qsf_append.txt` to its `ap_core.qsf`.
2. **Check first with synthesis only** (`quartus_map ap_core`, about 15 minutes): it must accept
   the `.stp` and resolve all 32 nodes. The `.stp` format is written without a reference file
   and is the main risk; if rejected, add the nodes once in the Quartus GUI and keep that file.
3. Full `make fpga`, then `tools/quartus_fit_summary.py` (require: RAM blocks still 300/308,
   no negative slack, ALMs about +1,100).
4. `quartus_pgm` the `.sof`, arm with `quartus_stp` (or the GUI), press a diagnostic action on
   the Pocket (window test), export the capture. Answer 5.4 questions 1 and 2.

Sequence: this proof build comes **before the blit engine** (Phase F0 in the roadmap).

### 5.6 Synthesis check result (2026-09-21, stage `signaltap-proof-20260921` on the VM)

- `quartus_map ap_core`: **Successful, 0 errors, 5 min 22 s.** The tap module is in the netlist
  (`core_top:ic|mp3_soc:u_soc|tau_signaltap_tap:g_phase2_window.u_stp_tap`, 32 registers), so the
  RTL side works and the default builds are unaffected.
- **The generated `.stp` is not usable yet.** `quartus_map` printed no SignalTap message at all
  (no `sld_signaltap`/`sld_hub` in the report), and `quartus_stp ap_core --enable` stops with
  `Internal Error: ICE, ice_instance_builder.cpp, Line: 685`. A garbage file gives a normal parse
  error and `<session/>` a "not a valid Signal Tap File" error, so the generated XML passes
  the structure check and fails while building the instance. The internal error was identical
  for 9 variants (no presentation, other clock names, M10K, no config, no trigger, one tap
  only), so the missing or wrong piece is in the instance element itself, not a detail I can guess.
- Not on the VM: no reference `.stp` anywhere, no Quartus GUI process, no `DISPLAY`.
- **Next step needs a reference file.** Options, cheapest first:
  1. Make one `.stp` in the Quartus GUI (a desktop session in the VM, or XQuartz on the Mac with
     `ssh -X`), add any 2 nodes from the staged project, save; `tools/gen_signaltap_stp.py` is then
     changed to match that file. Roughly 15 minutes of GUI work, once.
  2. Skip `.stp` for a first result: In-System Sources and Probes (`altsource_probe`) is defined
     entirely in RTL and read with `quartus_stp` Tcl. It gives live values, not waveforms.
  3. Instantiate `sld_signaltap` directly in RTL (its parameters are in
     `libraries/megafunctions/sld_signaltap.inc`); still needs an `.stp`-like description to read it.

### 5.7 Working `.stp` and synthesis numbers (2026-09-21)

- The GUI-saved reference (`tools/signaltap/reference_2taps.stp`, 2 taps, made in the VM's Quartus
  GUI after starting gdm3) fixed the format: `tools/gen_signaltap_stp.py` now produces a file that
  `quartus_stp ap_core --enable` accepts (32 taps; 2048 and 1024 samples tried).
- Node names that resolve in the post-synthesis netlist: `core_top:ic|mp3_soc:u_soc|tau_signaltap_tap:g_phase2_window.u_stp_tap|tap[n]`.
  Clock: `core_top:ic|tau_sdram_cpu_bridge:u_sdram_cpu_bridge|clk_sys` (there is no top-level `clk_sys`
  node; the same net appears inside `mp3_fb`, `tau_sdram_cpu_bridge` and `tgt_cmd`).
- **`quartus_stp --enable` caches the `.stp` settings in the project.** Run it again after every
  change of the file, or `quartus_map` synthesises the previous version (found with depth 1024/AUTO
  showing up when the file said 2048).
- Synthesis with SignalTap (32 taps, 2048 samples): Successful, 0 errors, about 7 minutes.
  Registers 8,044 -> 8,899 (+855), block memory bits 2,380,928 -> 2,446,464 (+65,536).
  `sld_ram_block_type` was **M10K** although the file said `ram_type="MLAB"`: that string is not
  honoured. AUTO also chooses M10K (1024 samples = +32,768 bits). At 2048 samples the buffer would
  use about 7 of the 8 free M10K blocks, so either find the MLAB spelling (save a GUI file with RAM
  type MLAB) or shrink the buffer (512 samples = about 2 blocks) for the proof.
- Open: the trigger condition is still the GUI default.
