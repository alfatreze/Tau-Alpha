# Meter save file, S1 spike: the fit recipe and the Pocket test (TAU DEV 227)

Status 2026-11-18: **prepared, host-proven, nothing launched.** No fit was started, nothing was written to a card, no Pocket was used. Design and evidence: `docs/features/meters/METER_SAVE_FILE_SPEC.md`. Audit: B-1090 to B-1093.

What the spike answers, in one sentence: **does the Pocket read a `nonvolatile` slot's file into the core at boot and read it back from the core at Quit, through the bridge, for a slot whose `address` is in the new `0x3xxxxxxx` region?** Everything else (the format, the firmware backend) is built only if it does.

## 1. The fit (the owner approves it; rule 3 of `docs/PROJECT_RULES.md`)

**Bundle:** `tools/save_probe_qsf_append.txt` = the shipped stats bundle (the one of `rc-b936` and `wdog-w3`) copied unchanged, plus one line, `TAU_SAVE=1`. The pack watchdog is on by default now (D-M20), so the bundle is the release netlist plus the RAM; the only RTL difference to the release is `TAU_SAVE`. `python3 tools/fit_manifest.py check-bundles tools/save_probe_qsf_append.txt` passes (no dead macro).

**Command (run from the worktree that holds the committed RTL; the VM stages the working tree of `src/fpga`, so commit or stash nothing that is not meant to be in it):**

```bash
python3 tools/vm_fit.py launch save-s1-b1090 --append tools/save_probe_qsf_append.txt --seed 1 --seed 2
```

One seed only (what I recommend for a spike that needs a fit that closes, not two): `... --seed 1`. Add `--wait-free` if the VM may be busy (it then polls every 30 minutes and launches when it is idle; `launch` refuses to start otherwise).

**How long, and when it finishes (from this bundle's own earlier fits; correct me when status is asked):**

| Launch | Fitter done | Reports / timing done | Source |
|---|---|---|---|
| one seed, idle VM | **about 1 h 21** | about 1 h 32 | `rc-b936`: launched 21:11, fitter 22:32, timing about 22:43; `def-b931` 1 h 24 |
| **two seeds together** | **about 2 h 26** | **about 3 h 25** | `wdog-w3`: launched 07:15, fitter 09:41, "about 3h25, twice a single fit" (B-940) |

So launched at 09:00: one seed finishes about **10:35**; two seeds about **11:30** (fitter) and **12:25** (reports). **The plan's "two seeds (about 1h45 each)" does not hold on this VM: two seeds in parallel take about twice as long as one** (they share the CPUs). Add about ten minutes for `vm_fit.py collect`.

**After the launch:** `launch` prints `quartus_sh processes now running on the VM: N (expected N)`; if that is not the number of seeds, STOP (the `wdog-w3` entry records a run launched without its bundle: 302 RAM blocks, 7 DSP, not a valid result). Check `work/diagnostics/save-s1-b1090/launch.json` lists `TAU_SAVE` among the macros. Status: `python3 tools/vm_fit.py status save-s1-b1090`; bitstream: `python3 tools/vm_fit.py collect save-s1-b1090 --seed 1` (prints the SHA-256 and writes `work/diagnostics/save-s1-b1090/ap_core_s1.rbf` and its manifest `.json`).

**What to check in the fit reports** (`ap_core.fit.summary`, `ap_core.fit.rpt`, `ap_core.sta.summary`):

1. **Fitter Successful, all four corners positive.** The reference (`wdog-w3`): slow 85C setup +0.655 / hold +0.286, slow 0C +0.699 / +0.190, fast 85C +2.472 / +0.129, fast 0C +2.483 / **+0.078**. The fast-0C hold is the thin one; a result below about +0.03 ns is a reason for the second seed, not a failure by itself.
2. **Total RAM blocks: 282 expected (274 + 8) of 308.** 302 would mean the bundle was not applied; 274 would mean the RAM was optimised away (it was not read: check `u_save_ram`). DSP unchanged at 23.
3. **The RAM is inferred as one true-dual-port, two-clock M10K memory:** in the "RAM Summary" of `ap_core.fit.rpt` a row for `u_save_ram|mem` with type "True Dual Port", 2048 words by 32 bits, and clocks `clk_74a` and the `clk_sys` PLL output; **no** "Cannot convert all sets of registers into RAM megafunctions" message (the failure a different restructuring once caused, `tools/ram_shrink_qsf_append.txt`), no "read during write" critical warning.
4. **ALMs about +30 to +80 over 15,507**; registers about +60 (the Gray counter, the index, the synchronisers).
5. **No new unconstrained or unknown-clock path** in the timing report involving `u_save_ram` or `u_bwr` (the SDC puts `clk_74a` and every PLL output in separate asynchronous groups).
6. `core_constraints.sdc: read-capture multicycle applied to 16 registers` (the SDRAM constraint still matches: the line the `rc-b936` read-back checked).

## 2. The package (after the fit; the bitstream is not built yet)

```bash
python3 tools/package_dev_build.py --number 227 --variant player2 --build-flags DIAG=1,PACKS=1 --save-probe \
  --rbf work/diagnostics/save-s1-b1090/ap_core_s1.rbf --rbf-sha256 <the SHA-256 collect printed>
```

TAU DEV numbers: **225 is installed, 226 is W1's, this one is 227** (the sequence continues, memory rule `feedback-dev-numbering-continuous`). The packager refuses a non-Diagnostic firmware and a bitstream whose manifest lacks `TAU_SAVE`. It declares data slot 11 (`tau-save-probe.bin`, `nonvolatile`, `address 0x30000000`, `size_exact 8192`, parameters `0x2`) and ships a zeroed 8,192-byte seed in `Saves/tau_dev/alfatreze.TAU DEV 227/`. Probe 2 (step 10): `--save-probe-params 0x22 --save-probe-no-seed` as TAU DEV 228 or later (same bitstream: no refit).

**Install (the owner approves the card write; never by hand, rule 4):**

```bash
python3 tools/install_dev_core.py work/diagnostics/tau-dev-227/pocket --card /Volumes/Pock
```

Read the dry run: it must print `save: Saves/tau_dev/alfatreze.TAU DEV 227/tau-save-probe.bin: missing, will be created (zeroed seed)`. Then `--yes` after the owner's word. The installer verifies the file's SHA-256 and leaves an existing one alone.

## 3. The Pocket test

**Setup:** TAU DEV 227 on the card with its seed; a Mac to read the card afterwards. The Diagnostic Build's rows are on **Settings > INFO** (the raw table; the rows start with `SV`, after the `PP` rows) and in the Report (Select + Start, decoded with `tools/decode_tau_suite.py --grid` when it fits; the Info page is photographed by hand when it does not, as for DEV 187).

| # | Do | Look at / photograph | Good | Bad, and what it means |
|---|---|---|---|---|
| 1 | Start TAU DEV 227. Do not play anything yet. Open Settings > INFO and photograph the SV rows (and BOOT EPOCH, BOOT TIME). | `SV PRESENT`, `SV LOAD WORDS`, `SV TABLE`, `SV BOOTS`, `SV CRC`, `SV PAT BAD`, `SV LAST`, `SV WRITTEN`, `SV PREV EPOCH`, `SV MS` | PRESENT **1**; LOAD WORDS **2048**; TABLE **8192**; BOOTS **1**; CRC **0** (first boot: nothing to verify); PAT BAD **0**; LAST **00000000**; WRITTEN a non-zero hex; PREV EPOCH **0**; MS a few tens of ms | PRESENT 0: the bitstream has no RAM (wrong rbf). LOAD WORDS 0: the Pocket loaded nothing: the file is not where it looks (path, size, name) or the slot is not declared; TABLE tells whether APF knows the slot. LOAD WORDS 2048 but TABLE 0: APF's table does not list the slot (the Quit would write 0 bytes). |
| 2 | Quit with the **root-menu Quit** (not power off). Note any hang (the screen freezing on Quit, the Pocket not returning to the menu). | | returns to the menu within a few seconds | **Any hang** is a result: record it, photograph, do not repeat; this is the old failure of the upstream `nonvolatile` slot |
| 3 | Eject nothing; **power the Pocket off, take the card to the Mac** (or mount it as the dock allows). Run `python3 tools/decode_save_probe.py "/Volumes/Pock/Saves/tau_dev/alfatreze.TAU DEV 227/tau-save-probe.bin"`. If the file is not there: `find /Volumes/Pock/Saves -name 'tau-save-probe.bin'`. | the decoder's lines | `status ok`, `boots 1`, `crc_stored` = `crc_computed`, `pattern_bad_words 0`, `epoch` = the boot time; the file is **8,192 bytes** | `status zero`: the Quit wrote nothing back (the flush route did not read our RAM, or wrote zeros). `status bad`: written but damaged (a read-timing or byte-order fault: keep the file for the bench). A file of another size (`ls -l`): KB-001's size-table claim is wrong. A file in another folder: the platform/core-folder rule is wrong; **record the path** |
| 4 | **This step alone proves the flush route.** If 3 is good, put the card back and start TAU DEV 227 again. Photograph the SV rows. | same rows | **BOOTS 2**, **CRC 1**, **PAT BAD 0**, **SV LAST = the first boot's SV WRITTEN**, **PREV EPOCH = the first boot's time** | BOOTS 1 again with a good file in step 3: the load path failed (LOAD WORDS tells: 0 = not loaded). CRC 2 or PAT BAD > 0 with a good file: bytes arrive damaged (byte order, address offset, a lost word: LOAD WORDS and the first bad word) |
| 5 | Quit, restart, **three more times**. | BOOTS and LAST each time | BOOTS 3, 4, 5, every LAST equal to the previous WRITTEN, CRC 1, PAT BAD 0 | any drop: record which boot |
| 6 | Boot, wait **one second**, Quit immediately. Restart. | | BOOTS +1, CRC 1 | CRC 2: the write-through was not complete when the flush read (a torn update: expected rarely; the format's generations exist for this) |
| 7 | Boot, **play a track for a minute**, Quit during playback. Restart. | | BOOTS +1, CRC 1, no audio fault at Quit | a hang or CRC 2 under load |
| 8 | **Power off by the power button** (not the menu Quit). Mount the card, run the decoder, restart. | the decoder, then the rows | the file shows the boot that just ran (the flush also happens at power-off, docs-verified) | the file unchanged: power-off does not flush (record it: it limits what "restart" means for the feature) |
| 9 | **Sleep** (power button short press), wake, Quit, restart. | | BOOTS +1 | if the sleep flush ran without reset the file may show the previous boot: record |
| 10 | **Probe 2** (a later package, no refit): parameters `0x22` and **no seed** on the card (`--save-probe-params 0x22 --save-probe-no-seed`). Boot (LOAD WORDS expected 0 or 2048 of 0xFF: record), Quit, look on the Mac. | whether `tau-save-probe.bin` appears | decides **who may create the first file** (spec section 1.4) | no file appears: the seed route is the only one (the plan's assumption holds) |

**What to photograph:** the INFO page(s) with the SV rows at steps 1, 4, 6, 7; BOOT EPOCH and BOOT TIME on each (to tie a boot to the file's `epoch`); any hang or odd screen. **On the Mac:** the decoder output (copy the text) and `ls -l` of the Saves folder at steps 3 and 8. Remember that **a photograph while audio plays costs a dropout** (memory rule `feedback-screenshot-dropouts`); the SV rows are static, so photograph before playback starts.

**Reading the file by hand:** `xxd "<file>" | head -3`: the first 32 bytes are `54 53 41 56` ("TSAV"), `01 00 00 00`, the boot count (4 bytes), the last value, the seed, the CRC, the epoch, zero; the rest is the pattern. `python3 tools/decode_save_probe.py FILE --json` for a machine-readable line (exit 0 sound, 1 zero, 2 damaged). `python3 tools/decode_save_probe.py --make FILE` writes the zeroed seed.

**Exit rule:** if step 3 or 4 shows the flush route does not work (`status zero`, no file, a hang at Quit), **stop**: nothing else is built, the fallback is the two-tuning pool, and the owner decides. Record every outcome in the audit trail with an evidence label (`POCKET-OBSERVED` only for what the owner ran).

## 4. What was and was not proven before this test (host only)

Run on the Mac, all green: `make test-rtl-save-ram` (the RAM against the shell's bridge timing, four clock ratios, six mutants killed), `make test-rtl-save-soc` (the real VexRiscv against the RAM, the self-test across three boots including a torn file, five mutants killed), `python3 sim/test_p2_save_probe.py` (the firmware's self-test logic and the Mac decoder agree, two mutants killed), `python3 sim/test_install_dev_core.py` (the seed is created, never overwritten, removed by a restore), `python3 sim/test_p2_diag_split.py` (the register lists the new rows and source), both firmware builds (`DIAG=0` is byte-identical to main; `DIAG=1` has the same hot RAM). **Not run: Quartus, the VM, any Pocket.** The macro-off equivalence is an argument (spec section 3), not a Pocket test.
