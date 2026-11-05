# Pack watchdog: plan (2026-11-05)

Status: **PLANNED, not built.** Owner decision 2026-11-05: "I want the watchdog, let's plan it in" (D-T15). Follows D-M20 (packs are protected by integrity and strikes). Needs an RTL change and a fit; nothing here is host-proven or fitted yet.

## 1. The problem
A pack is code from the card that the player calls once per picture (`pack_tick(&in)`, `fw2/ui/meters.c` around line 441). The host judges the tick only **afterwards** (`took = cycles() - tp`, `ps_judge`: three overruns disable the pack). A pack that never returns (an endless loop) hangs the whole player: no audio refill, no UI, nothing recovers except a power cycle. D-M20 records this as the known limit and names the cure: a hardware watchdog.

**Goal:** an endless or far-too-long pack tick is cut off, counted as a strike, and the player keeps running (audio included). **Not a goal:** defending against a deliberately hostile pack (it runs with full access; signing was assessed and not chosen, D-M20).

## 2. Facts from the tree (read 2026-11-05)
* The CPU is VexRiscv (`src/fpga/rtl/VexRiscv_Full.v`). It has `timerInterrupt`, `softwareInterrupt` and `externalInterruptArray[31:0]` inputs; `tau_soc.v` ties all three to 0 (lines 390-392).
* The firmware uses **no interrupts and no traps**: `fw2/platform/start.S` sets the stack, paints the sentinel, clears `.bss`, calls `main`; there is no `mtvec`, `mstatus` or CSR access anywhere in `fw2/`.
* The reset is global: `cpu_reset` (`core_game.vh` line 107) = reset counter, `fw_loaded`, host reset; it resets the CPU and every peripheral together. The firmware image is loaded into RAM once at boot and `.data` is initialised **in place** (no pristine copy). A CPU reset would therefore restart `main` with **dirty `.data`**: a reset is not a clean recovery.
* The existing check is soft and after the fact (`ps_judge`, `pack_strike_core.h`, host-tested); the per-pack limit `p2.max_cycles` is in the manifest (`cyc_limit_of`).

## 3. Options
| | Mechanism | For | Against |
|---|---|---|---|
| **A. Interrupt + recovery (recommended)** | A cycle counter in RTL raises a CPU interrupt when the armed limit passes; a trap handler abandons the tick and returns into the scheduler | Audio keeps running; no reboot; reuses the strike logic; small RTL | First interrupt use in this firmware (trap entry, context recovery); a pack that disables interrupts is not stopped |
| B. CPU reset on expiry | Counter resets the CPU | Stops anything | Restarts with dirty `.data`; the song stops; needs a reset-surviving "why" flag; no RAM budget for a pristine copy |
| C. A with B as a last-resort backstop | A, plus a reset if the interrupt is not acknowledged within N cycles | Catches a pack that masks interrupts | Needs the dirty-`.data` problem solved first; larger; decide after A is on a Pocket |

**Recommendation: A for the first version; C only if the owner wants protection against interrupt-masking code after A has run on a Pocket.**

## 4. Design of A
**RTL (`tau_wdog.sv`, about 50 ALMs, no RAM, no DSP):** a free-running 32-bit down-counter on `clk_sys` and four MMIO registers in the next free block of `tau_soc.v`'s map:
* `WDOG_ARM` (write): the limit in cycles; loads the counter and starts it. Writing 0 disarms. Not reset by the CPU's own writes, only by `cpu_reset`.
* `WDOG_STATUS` (read): bit 31 present, armed, expired (sticky until acknowledged), the expiry count.
* `WDOG_ACK` (write): clears `expired` and drops the interrupt.
* the interrupt line goes to `timerInterrupt` (level; stays up until acknowledged). Present bit lets old firmware and new bitstreams coexist (the usual `CORE_VERSION` / probe rule applies: use the runtime probe).

**Firmware:**
* `start.S`: a trap vector (`mtvec`) that saves the minimum, checks `mcause`, and for the watchdog interrupt jumps to a recovery routine. Anything else keeps today's behaviour (hang visible in the Report, not silent).
* The pack call site arms the counter just before `pack_tick` and disarms right after; `mstatus.MIE` is set only inside that window, so **no other firmware code ever sees an interrupt**.
* Recovery (the part with real risk): the call site saves `sp`, `ra`, `s0..s11` (a minimal `setjmp`); the handler acknowledges the watchdog, restores them and returns into the call site with "aborted". The call site then treats it as a strike, sets `force_next`, and **cleans the engine**: drain or flush the gfx command queue and fence state the pack may have left half-written (needs a check of what a safe flush is in `tau_gfx`/`tau_talos`; this is the main open technical question).
* An abort should count as **more than one strike** (suggestion: the pack is disabled for the session at once, since an endless loop is not marginal); the owner decides.
* The hardware limit is a multiple of the soft limit, with a floor and a ceiling (suggestion: 4 x `p2.max_cycles`, floor 2 M cycles, ceiling 200 M); packs with limit 0 get the ceiling.
* Diagnostic Build only: a registered `P2_DIAG` feature (`fw2/diag_features.json`, `fw2/build.sh DIAG=1`) with a built-in "hang" test pack and Report rows `WDOG ARMS / EXPIRES / LAST LIMIT`. Counters in the normal build are cheap and stay.

## 5. Work, in order (each phase ends with a result)
* **W0 Facts (host, small):** confirm VexRiscv_Full was generated with the interrupt and CSR plugins on (the ports exist; check `mtvec`/`mie` are really present); decide the safe engine flush; read `tau_gfx` for what a mid-command abort leaves behind. Output: this plan's open questions answered.
* **W1 RTL + host test:** `tau_wdog.sv` + an iverilog testbench (arm, expire, ack, disarm, limit 0, re-arm during expiry, reset) with mutants, per the repo's red-then-green rule.
* **W2 Firmware + host test:** trap entry, the recovery routine, the call-site change, a pure core for the limit arithmetic (like `pack_strike_core.h`), a host test of the strike logic; a CPU-level simulation of the trap path if the repo's firmware-in-RTL harness can run it (check in W0), otherwise the Diagnostic Build hang test is the first proof.
* **W3 Fit:** one RTL change = one fit, about 1 h 40 per seed, two seeds for any timing claim (about 3 h 30). The player fit is at hold +0.096 ns and has only one seed (`rc-b936-s2` is queued): do not add to the RTL until that second seed is read.
* **W4 Pocket:** a Diagnostic Build with the hang pack: the tick is cut off, the song keeps playing, the pack is struck out, the Report shows the counters; then a normal pack still runs with a clean count. Evidence labels as usual; nothing claimed before that run.
* **W5 Docs:** D-M20 follow-up, `pack_strike_core.h` comment (no longer "no watchdog"), the pack author guide (a pack that never returns is cut off), audit entries.

## 6. Where it fits
`V0_7_0_REVIEW.md` says not to add a pack-safety hardware watchdog to v0.7.0 (it needs a new bitstream), and the v0.7 RTL (`rc-b936`, DEV 202) is already fitted and tested. So this goes in the **next RTL fit after v0.7.0**, bundled with whatever else needs RTL by then. W0 to W2 are host work and can start any time without touching the fitted bitstream.

## 7. Owner decisions needed (my suggestion first)
1. Option A now, C later only if wanted (suggested).
2. An abort disables the pack at once, versus counts as one strike of three (suggested: at once).
3. Hardware limit = 4 x the manifest limit, floor 2 M, ceiling 200 M cycles (suggested; the manifest limits are per pack).
4. Start W0 to W2 now (host only, no fit), W3 after the second `rc-b936` seed and the v0.7.0 release.

## 8. Risks
* Recovering from an interrupt in the middle of an engine command or an MMIO burst may leave the engine in a state the host does not expect; this is why W0 and the W4 hang test exist.
* A pack that writes `mstatus` or `mtvec` defeats A (accepted limit; the same pack can already do anything).
* First use of interrupts: the trap path must be proven by a test that actually takes the interrupt, not by reading the code.
