# 016 — Phase 2 CPU-window diagnostic stalls on first transaction

**Status:** Open — Pocket hardware finding; isolate before any SDRAM migration.

## Observation

On 2026-09-16, the separately packaged `TAU CPU SDRAM Diagnostic` booted on
Pocket and displayed `PHASE 2 UNCACHED WINDOW`, `SAFE REGION 2-3 MIB`, and
`CPU LOAD STORE LANES` at 31.8°C / 60 Hz / sync ok. It remained on `FIXED
PATTERNS` for more than 30 seconds and did not reach PASS or FAIL.

This identifies the boundary tightly: firmware loading, framebuffer commands,
the standalone diagnostic package, and the pre-transaction CPU execution all
work. The first mapped uncached transaction does not complete back to the CPU.
It is not evidence of SDRAM data corruption, and it does not permit any Phase 2
data migration.

## Known evidence

- **Pocket:** boot/UI checkpoint then first-transaction stall.
- **Pocket preflight:** mailbox write/read through the *new shared mux* at the
  same physical 2 MiB location passed; `MAILBOX OK CPU WINDOW NEXT` displayed
  before the same `FIXED PATTERNS` stall.
- **Quartus:** the enabled RBF is A-056, SHA-256
  `0d01f61409f42aec6f372166f8e81f17aa1c3b0482361a03ccba3465c942217e`.
- **Simulation:** adapter, mux, and stand-in bridge path pass, but that model
  does not instantiate the actual CDC bridge plus SDRAM controller/arbiter.
- **Composed simulation:** the real adapter, mux, CDC bridge, and arbiter pass
  across 60/100 MHz with recurring framebuffer traffic. This strengthens the
  block-level result but still substitutes a CPU master and controller model.
- **Prior Pocket:** the Phase 1 mailbox diagnostic passed, proving its older
  direct mailbox-to-CDC-bridge path, not this new WB-adapter/mux composition.

## Investigation order

1. Compare the isolated VM source snapshot used for A-056 with the current
   local adapter/mux/top-level source. Do not assume a later local fix is in
   the RBF solely because their visible interfaces match.
2. Capture/derive the actual VexRiscv `CTI`, `SEL`, request, adapter-accept,
   bridge-done, and ACK progression for the first I/O-mapped CPU operation.
   The existing testbench assumes a classic request and does not instantiate
   the generated VexRiscv master.
3. Extend simulation to compose the actual `tau_sdram_cpu_bridge` and the
   arbiter handshake, including the initial framebuffer traffic state.
4. Make the smallest evidence-backed RTL correction, fresh-build an isolated
   macro-enabled RBF, repeat the same dedicated Pocket package test.

## Safety

The diagnostic's intended write region remains 2–3 MiB. No normal TAU package,
firmware, cached alias, or cold workspace may use mapped SDRAM while this issue
is open.

## Result update

The file hashes for `mp3_soc.v`, `core_game.vh`, `tau_sdram_wb_adapter.sv`,
`tau_sdram_bridge_mux.sv`, and `tau_sdram_cpu_bridge.sv` exactly match between
the A-056 VM source snapshot and the local checkout. The initial package used
the intended RTL. A ROM-only mailbox preflight is now the next minimal test; it
does not require Quartus because the bitstream and bridge wiring are unchanged.

**Preflight result update:** The updated ROM reached `MAILBOX OK CPU WINDOW
NEXT` and then stalled again at `FIXED PATTERNS`. The shared mux, existing CDC
bridge, arbiter, controller, and physical 2 MiB address are therefore accepted
as working on Pocket. The remaining boundary is VexRiscv request semantics,
the Wishbone adapter, or their completion/ACK integration—not SDRAM content or
the mailbox route. The next build must expose or simulate that exact boundary.

**Simulation update:** `tb_tau_sdram_composed_path.v` now covers the actual
adapter/mux/CDC-bridge/arbiter composition under periodic framebuffer traffic
and passes. A first attempt to execute the diagnostic through the generated
VexRiscv in a minimal instruction-memory harness timed out before reaching the
target request: the harness did not faithfully model its cached instruction
bus. That experiment was discarded, its temporary artifacts removed, and it
is not counted as VexRiscv evidence. A future generated-CPU test must model
the instruction bus correctly or observe the request in hardware.

**Second generated-CPU attempt:** A more complete temporary harness added the
cycle counter and target-command MMIO behaviour that the diagnostic startup
needs. Icarus still did not reach a useful request trace within the bounded
runtime because full generated-Vex simulation is prohibitively slow here. It
was removed along with the temporary artifacts and is **inconclusive**, not a
simulation pass or failure of the RTL. This is why the next gate is a minimal
on-Pocket, hardware-only progress probe rather than further speculative CPU
simulation.

**Next probe build:** `tau_sdram_cpu_window_probe.sv` latches the first CPU
request's CTI/SEL and each subsequent adapter/mux/bridge/ACK milestone in the
60 MHz domain. A 16-cell green/red bar overlays the first active video lines
after scanout, so it remains visible even when firmware freezes. Its bit order
and the expected full-word-classic pattern are documented in
[`SDRAM_CPU_WINDOW_DIAGNOSTIC.md`](../SDRAM_CPU_WINDOW_DIAGNOSTIC.md). The
probe has an isolated RTL test; Pocket and Quartus evidence remain pending.

**Quartus/package update:** The dedicated A-059 probe RBF completed with 0
errors. It is hash-pinned and packaged as the side-by-side **TAU CPU SDRAM
Probe** (platform `tau_sdram_probe`), not as a replacement for the prior
diagnostic. The raw RBF SHA-256 is
`921d6f941b8d40dd0f662857b6fe1b34e09a22bade0f372a950c9ed8c88df97c`; the
bit-reversed Pocket RBF SHA-256 is
`fd4d59e5d054f6f4c5e6c552fcb84bcba691bd28a847432aa5c6dccfd8578720`.
Timing/resource fit evidence is recorded in A-059. Pocket evidence is still
pending; the package has not been copied to an SD card automatically.

**Probe result / corrected boundary:** The A-059 Pocket photo shows every
first-request handoff milestone through Wishbone ACK, classic CTI `000`, and
full-word SEL `1111`. The first mapped request therefore completes; the older
"first transaction stall" wording is superseded. The first probe bar repeated
because its 8-bit horizontal counter wrapped at pixel 256; that is a
diagnostic-display defect only, not duplicated SDRAM traffic. Source review
then found the adapter's `S_RELEASE` state waited indefinitely for `!CYC ||
!STB`. VexRiscv can legally retain CYC/STB between adjacent classic beats, so
the completed first beat prevents admission of the next one. A fixed one-clock
turnaround plus a back-to-back regression test is now implemented; it needs a
fresh Quartus/Pocket gate before this issue can close.

**A-060 build/package update:** The corrected standalone build completed with
0 errors, positive timing (tightest reported hold slack 0.116 ns), and no
additional RAM blocks. It is pinned and packaged separately as **TAU CPU SDRAM
Probe A060**, raw RBF SHA-256
`9ea5e38c20145125627b8d23c2bf4ab02b7c5b3998778cea80598bcd72b4c5ee`,
Pocket bit-reversed SHA-256
`4b68f7b3b6681697ed7af80718535a2e5542e347db918ee1ab9e41f4ccbb78cd`.
The prior A-059 package is retained for comparison. Pocket validation is
pending; no card write occurred during package creation.

**A-061 firmware-only discriminator:** A-060 no longer stalls but 182/183
readbacks fail; the one passing zero pattern is consistent with every mapped
read returning zero. The CPU-labelled launch screen confirms the intended
ROM; the later generic result title was a `draw_result()` labelling defect and
has been corrected. To distinguish a mapped-read/return-path issue from a
mapped-write issue without another Quartus build, the new A-061 ROM first
passes the established mailbox write/read at physical 2 MiB, then CPU-reads
the same physical word through `0xA0200000` before running the matrix. Its
separate package `TAU CPU SDRAM Readback A060` uses the verified A-060 RBF and
a distinct ROM SHA-256 `f8a7f999cb0a503c9bef0536cead0c8f2ea046382efb2626bfdc8af60c16338`.
Host firmware/package/UI checks pass; Pocket result/install are pending.

**A-061 Pocket result:** The readback ROM proceeded past its new checkpoint
and then produced the same matrix result (181 failures on first load, 182 on
re-run). Therefore the CPU successfully read the exact non-zero mailbox word
at physical 2 MiB before the matrix began. This rules out the CPU mapped-read
return path as the primary cause and narrows the fault to mapped CPU stores or
their write payload/direction. The one-count variation is retained as Pocket
evidence of non-deterministic/partial behaviour, not averaged away. The next
probe revision records the first two mapped request WE values; with the
readback ROM they correspond to the known CPU read then the first CPU store.

**A-062 build/package update:** The 19-cell direction probe completed with 0
Quartus errors, positive timing (tightest reported hold 0.115 ns), and is
packaged separately with the A-061 ROM as **TAU CPU SDRAM Probe A062**. Raw
RBF SHA-256 is
`d7f60eb7e52705a5f622d449312b266040399acf2940a352acebc0b77dcde0a4`; its
bit-reversed Pocket RBF SHA-256 is
`99437bac629b27bdba89c7e2a377645e7aa831bc51e56bd8e40c425a54fb2984`.
Pocket installation/result are pending explicit mounted-card confirmation.

**A-062 Pocket result / second reversal:** The installed A-062 bar read green
×8, red ×4, green ×4, red ×1, green ×2, then the matrix completed with
182/183 failures and zero actual values. With the A-061 ROM, this is exactly
the expected first mapped mailbox read (`CTI=000`, `SEL=1111`, `WE=0`) followed
by an observed first matrix-store request with `WE=1`. The store instruction
does therefore reach the VexRiscv Wishbone boundary with the correct direction.
The remaining fault is downstream of that boundary: adapter payload retention,
owner-mux forwarding, CDC bridge capture, or SDRAM-domain write sequencing.
The next diagnostic must observe those stages, not change functional write
logic speculatively.

**A-063 next diagnostic:** The next macro-enabled RBF expands the persistent
bar to 35 cells. It proves the known first-store payload (`FFFFFFFF`) and byte
enables (`1111`) at CPU ingress, adapter issue, owner-mux start, SDRAM-domain
capture, and both accepted 16-bit controller writes. A dedicated mux-origin
pulse prevents the preceding diagnostic-MMIO preflight write from contaminating
the CPU-store record. The first red cell, if any, becomes the only authorised
functional-fix target.

**A-063 Quartus/package result:** The clean retry snapshot completed with 0
errors, 343 warnings, and positive timing (tightest reported hold slack
0.115 ns). It is separately packaged as **TAU CPU SDRAM Probe A063**, raw RBF
SHA-256 `acce05b2145b34780da31a8a315557ac64ae2416546f104c5a242f2241cbfaaa`,
Pocket bit-reversed SHA-256
`8f2d4693060443a83c8d06620b8d85b4f7cbd283f69e971dec5b523dae3d348e`.
The package is host-verified only; card installation and Pocket evidence are
pending explicit confirmation that the card is mounted.

**A-063 Pocket display defect:** The first A-063 run completed with the known
181-failure variant and showed only the old 19-cell bar. Review found the
probe-pixel range had been widened to 280 pixels while the enclosing red/green
bar guard remained at 152 pixels. The hidden cells 19–34 have no Pocket
evidence. Correct the guard to 280, add a host check that both scanout ranges
match, then rebuild a new diagnostic artifact before making any data-path
claim.

**A-064 Quartus/package result:** The full-width scanout RBF completed with 0
errors, 343 warnings, and positive timing (tightest reported hold slack
0.118 ns). It is separately packaged as **TAU CPU SDRAM Probe A064**, raw RBF
SHA-256 `60abb545fef5e6c725c84717af21b6a53f4f994d9219cd0245b0dbd1577c32dc`,
Pocket bit-reversed SHA-256
`da6ccf4a8d0200147d458238b96c058d58dd67146ec8890f38b78e24f36a11fe`.
The package is host-verified only; card installation and Pocket evidence await
fresh explicit mounted-card confirmation.
