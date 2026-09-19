# Phase 2 uncached CPU-window SDRAM diagnostic

**Status:** the original CPU-window test no longer stalls after the adapter
turnaround correction, but it returns mostly-zero data after a mapped CPU
store. A-064 is now visible on Pocket and renders all 35 probe cells. Its
payload cells correctly observed the first test store (`0x00000000`), exposing
that the probe documentation had incorrectly described that store as
`0xFFFFFFFF`. A-065 instead targets the fourth CPU request—the first
`0xFFFFFFFF` store—which is the transaction behind the first observed failure.
Its focused simulation, Quartus build, and first Pocket run agree that the
request reaches the bridge. A-066 now adds controller-latch, WRITE-pin, and
first-read provenance. A-067's delayed bridge capture passed focused simulation
and a fresh Quartus fit, but its first Pocket run regressed the established
MMIO preflight (`5DB54350` instead of `43505550`) before the CPU window began.
That functional change is rejected. A-074 restores the known-good controller
client timing and adds bridge-response provenance. Its isolated Quartus seed-2
fit is complete and packaged. The first A-074 Pocket run completed 183 checks
with 181 failures; cells 46–48 decoded `G-R-G`, proving the bridge assembled
`0xFFFFFFFF` while the CPU still read zero.
No CPU-window pass or SDRAM migration is authorised. This is a developer-only
gate, not a player feature.

**A-065 Pocket result:** the target all-ones write reached the SDRAM-domain
bridge and both 16-bit controller requests were accepted, yet the later CPU
readback remained zero (182 failures). The active boundary is now downstream
of the bridge; see [issue 018](issues/018-phase2-post-bridge-write-readback.md).

## Purpose and scope

`TAU CPU SDRAM Diagnostic` verifies the *mapped CPU data path* which Phase 1
could not exercise. It performs ordinary VexRiscv loads and stores through the
uncached alias `0xA010_0000–0xA3FF_FFFF`, not the Phase 1 MMIO mailbox.

The first smoke test writes only `0xA020_0000–0xA02F_FFFC`: physical SDRAM
2–3 MiB. That deliberately preserves the framebuffer/guard below 1 MiB and
the Phase 1 mailbox diagnostic's 1–2 MiB region. It is destructive within its
own 1 MiB region; it must not run alongside a product feature that owns it.

| Check | What it establishes | Does not establish |
|---|---|---|
| 48 anchor-pattern readbacks | CPU address decode and data round trip | Cached alias behavior |
| 64 walking-one/zero readbacks | 32-bit CPU word data path | Sustained throughput |
| 64 sparse address-as-data readbacks | basic address-line separation in 2–3 MiB | Full 63 MiB coverage |
| 7 lane-sequence readbacks (one word seed, four bytes, two halfwords) | Wishbone `SEL` preservation into SDRAM halfwords | Burst/cache-line semantics |

The expected total is **183 readback checks**. A PASS is **Pocket** evidence
only for this limited uncached data path. It does not authorize cached mapping,
linker placement, cold workspace migration, or execution from SDRAM.

## Hardware progress probe (A-074 Quartus complete; Pocket gate pending)

The existing running screen is CPU-rendered, so it cannot say which hardware
handoff occurred after a CPU-side stall. The next *separate macro-enabled
diagnostic RBF* adds a 368×8-pixel top-edge bar that is driven independently of
the CPU. It latches evidence from the first mapped request and holds it until
reset. It is deliberately not part of normal Tau.

Read the 49 cells from left to right. **Green** means observed; **red** means
not observed. Cells 0–8 are `CPU request`, `adapter request`, `mux accept`,
`mux start`, `bridge busy`, `bridge done`, `adapter done`, `Wishbone ACK`, and
`unsupported CTI`. Cells 9–11 show the first request's CTI bits, low to high;
cells 12–15 show its SEL byte-enable bits, low to high. Cell 16 is the first
request's write-enable. A-065 retains that first-request metadata, then records
the **fourth** CPU request in cells 17–20: the preflight read, zero store, and
zero read occur first, so request four is the first matrix store of
`FFFFFFFF` with byte enables `1111`. Cells 21–24 record the adapter request
and its write/payload/byte-enable values; 25–28 do the same at the owner-mux
output; 29–32 record the first all-ones mapped-write capture in the SDRAM clock
domain and its write/payload/byte-enable values; 33–34 require the lower and
upper 16-bit write commands to have been accepted. Cells 35–37 show the target
all-ones write as latched by `sdram_fb` (target seen, data `FFFF`, byte enables
`11`). Cells 38–41 show the actual controller WRITE command, DQ output enable,
driven `FFFF`, and unmasked DQM. Cells 42–45 show the following CPU-owned read
request, a sampled `READ_OUTPUT` halfword, whether that halfword was zero, and
whether it was all ones. Cells 46–48 are the bridge's assembled 32-bit response
for that same following read: response seen, response is zero, response is all
ones. For the expected A-065 failure, cells 17–43 and 45 should be green while
44 is red; cells 46–48 distinguish a bridge-side all-ones response from a
later mux/Wishbone loss. This is diagnostic evidence, not transaction proof.
In successor A-076 return-path mode, cells 46–48 are reused as CPU-facing
ACK/data evidence: response seen, CPU-facing data zero, and CPU-facing data
all ones. A-076 therefore has a distinct core identity.

Photograph the initial screen and the frozen screen if it freezes. Record the
49-cell green/red sequence verbatim. This identifies the first missing
boundary but is not itself proof of a correct transaction.

## Build and package provenance

1. Build the ROM: `make firmware-sdram-cpu-diag`.
2. Stage only the known macro-enabled RBF from a fresh Quartus build:
   `work/diagnostics/sdram-cpu/fpga/ap_core.rbf`.
3. Verify its SHA-256 against A-056 before packaging. The accepted build hash
   is `0d01f61409f42aec6f372166f8e81f17aa1c3b0482361a03ccba3465c942217e`.
4. Package: `python3 tools/package_sdram_cpu_diagnostic.py`.
5. Copy the generated `Cores`, `Assets`, and `Platforms` folders from
   `work/diagnostics/sdram-cpu/pocket` to the Pocket SD root.

The current packager hard-rejects any RBF whose SHA-256 differs from its named
profile. The original diagnostic remains pinned to A-056. The current hardware
probe uses `python3 tools/package_sdram_cpu_diagnostic.py --probe-a074`, a
separate **TAU CPU SDRAM Probe A074** package and `tau_sdram_prb74` platform.
The signed-off A-074 seed-2 raw RBF SHA-256 is
`d4b6295d168351704dc185abf358bb230be5cc2b77460a3adbaeea48c95b6c98`; its
Pocket bit-reversed SHA-256 is
`794d5c9b1a9b646959a687dbaa2325beb821a006d671941896cf039f6b95d3db`. The
A-061 readback ROM SHA-256 remains
`f8a7f999cb0a503c9bef0536cead0c8f2ea046382efb2626bfdc8af60c16338a`.
Its `SHA256SUMS.txt` records both values. The signed-off A-076 raw RBF SHA-256
is `9ef62ebc4002abf4f5d29c84c59c08d997c18369d55e7c134beac5b97c832ef1`.
It defines both `TAU_PHASE2_WINDOW` and `TAU_PHASE2_RETURN_PROBE` and must use
the distinct **TAU CPU SDRAM Probe A076** / `tau_sdram_prb76` package profile.
Its generated bit-reversed Pocket RBF SHA-256 is
`212e1761d2b4107e6d933e11bada800dfdaf78c8fc83ad9c1f36788e65fc747a`.
Do not substitute the ordinary
macro-off RBF: that map does not provide the CPU window. The legacy version
register cannot distinguish the two bitstreams, so this provenance check is
mandatory.

The signed-off A-077 raw RBF SHA-256 is
`53b11ee8fbfd2ff401a8a84255c88c4edd994333210933dfb1825d8b6bc6806f`.
It defines `TAU_PHASE2_WINDOW` and `TAU_PHASE2_ADAPTER_PROBE` and must use the
distinct **TAU CPU SDRAM Probe A077** / `tau_sdram_prb77` package profile. Its
generated bit-reversed Pocket RBF SHA-256 is
`1dd410d81ad9fa646a498bdcbead7d52cc5a7330fb0502d15dc489b40dd71918`.

The signed-off A-079 raw RBF SHA-256 is
`246202a00fab50c6b96a38e8dd1acc4d835e5041b9d1784f325d2223421531e8`.
It defines `TAU_PHASE2_WINDOW` and `TAU_PHASE2_MUX_PROBE` and must use the
distinct **TAU CPU SDRAM Probe A079** / `tau_sdram_prb79` package profile. Its
generated bit-reversed Pocket RBF SHA-256 is
`6ce93f8713ea2e3d2dc74ae98ed215cfb7d84006b393ecd92619665ab47a93dd`.

## Pocket procedure and evidence

1. Cold boot the Pocket, then open **Media Players → TAU CPU SDRAM Probe A079**.
   It includes the A-061 preflight-readback ROM. **TAU CPU SDRAM Diagnostic**
   remains available only as the uninstrumented A-056 comparison baseline.
2. Photograph the initial screen. It must state **PHASE 2 UNCACHED WINDOW**
   and **SAFE REGION 2-3 MIB** before memory traffic begins.
3. Wait for complete FAIL or PASS. Record device temperature, cold/warm boot,
   check count, failure count, and photos of both the 49-cell bar and result.
   Press A to repeat only if the bar is not stable or legible.
4. Decode A-076 cells 46–48 as CPU-facing **ACK seen**, **CPU data zero**, and
   **CPU data all ones** for the fifth target read. A-077 reuses the same
   cells for the adapter's ACK/data pair before `mp3_soc`'s selector: `G-R-G`
   means all ones reached the adapter boundary; `G-G-R` means zero reached it.
   A-079 reuses those cells for owner-mux **done seen**, **mux data zero**, and
   **mux data all ones**. `G-R-G` means the mux presented all ones before the
   adapter; `G-G-R` means the mux itself presented zero.
   One clean cold-boot
   observation is the immediate gate; do not spend the five-cold/five-warm
   matrix until this return-path boundary is classified. A later passing
   implementation must still complete that matrix and one post-player-session
   pass, without claiming concurrent playback.

Before copying a newer CPU-window diagnostic, remove its superseded CPU-window
packages from the card. Keep normal Tau and only independent regression
baselines that are still useful; do not accumulate obsolete probes.

**Current card state:** A-077 is installed with verified RBF/ROM hashes;
completed A-076 was removed. A-076's Pocket result remains the recorded 183
checks / 181 failures with final cells `G-G-R`: CPU ACK observed, CPU data
zero, CPU data not all ones. A-077 has now produced the same 183 / 181 result
and final cells `G-G-R`: adapter ACK observed, adapter return data zero, and
adapter return data not all ones. Keep normal Tau and the independent SDRAM
Diagnostic/Stress baselines; A-079 is installed with verified RBF/ROM hashes,
and only superseded A-077 was removed. A-079 probes owner-mux
`wb_done`/`wb_rdata` before adapter capture. Its `G-R-G` result means mux all
ones; `G-G-R` means mux zero. Its five regenerable Pocket catalog indexes were
backed up under
`work/diagnostics/sdram-cpu-probe-a079/pocket-cache-backup-2026-09-19/System/`
and cleared after their
platform/category mappings omitted A-077 despite a valid core-list entry.
Eject/remount the card and cold boot Pocket before evaluating list visibility.

On a black screen before the initial UI, capture Pocket diagnostics: the CPU
may be stalled by a malformed mapped transaction. On FAIL, capture the full
screen including `FIRST BYTE ADDR`, expected and actual values. Do not retry
by installing a normal TAU RBF; rebuild/package the matching enabled artifact.

## Evidence boundary and next gate

Record every run in `AUDIT_TRAIL.md` with **Pocket** evidence, including
failures and reversals. After the 10-pass smoke matrix, add a separate
concurrent playback/CRC contention gate before migrating any cold data. The
cached `0x4010_0000` alias deliberately remains a bus error until its own
bounded cache-line adapter and proof exist.
