# Meter settings save file: the specification (W2, S0 design and S1 spike)

Status 2026-11-18: **design, with the S1 spike prepared and proven in simulation; nothing of it has run in Quartus or on a Pocket.** Owner decision: `docs/DECISIONS.md` D-M25. Plan: `docs/features/meters/RELEASE_075_PLAN.md` section 3 (W2). Audit: B-1090 to B-1093. This file replaces the "no settings file" paragraph of `EXTERNAL_METERS_SPEC.md` section 5 (that paragraph is the D-M23 decision D-M25 amends).

Evidence labels used below: **docs-verified** (Analogue's text, quoted), **source-verified** (read in this tree or in the community source the knowledge base cites), **community-reported** (one third-party claim), **host-proven** (a test in this repo, `make test-rtl-save-ram`, `make test-rtl-save-soc`, `sim/test_p2_save_probe.py`), **to measure on a Pocket** (the S1 spike measures it).

## 0. Summary

* **Verdict on the bridge read path: feasible with one new case in `core_top.v`'s read mux** (section 2). The comment in `core_game.vh` ("the datatable is the ONLY way to hand APF a memory region it can read") is stale: `core_top.v` already serves the interact persist words from a core-owned register file at `0x2xxxxxxx`, hardware-validated (B-845).
* The native route is: a **`nonvolatile` data slot with an `address`**. APF writes the file into the core at boot (bridge writes), the core reads and writes it through MMIO, and at Quit APF **reads the same addresses back** over the bridge and writes the result over the file. Everything the core must provide is a small dual-clock RAM (`src/fpga/core/tau_save_ram.sv`, 8 KB = 8 M10K blocks) behind a new bridge region `0x3xxxxxxx` and three MMIO registers.
* **The first file must exist** (KB-074): the installer, Omega and the release zip create a zeroed 8 KB seed; the firmware copes with a missing, zero, all-0xFF or damaged file by starting from defaults and never refuses to boot.
* **File `tau-meter-settings.bin` v1:** 8,192 bytes = **two 4 KB generations** (A and B), each `header | sparse records keyed by id_hash`, CRC-32, newest valid generation wins. Two generations so that a Quit that lands in the middle of a rewrite (or a sleep, which may flush without a reset) can never leave the file without a complete copy.
* **Hot RAM does not grow.** The backend is cold code and works by streaming through the MMIO window; its few words of state live in the cold image (the same technique as the S1 self-test, which keeps the hot image byte-identical: section 5).
* **Exit rule:** if S1 fails on a Pocket, stop; the fallback is the two-tuning pool as built, and the owner decides.

## 1. The native route, end to end

### 1.1 The data.json slot

Analogue's field list (docs-verified, `core-definition-files__data-json.txt`):

* `nonvolatile: boolean`: "If true, slot will be both loaded and unloaded on core exit."
* `deferload: boolean`: "If true, slot will not be loaded, but its size and ID will still be communicated to the core, and the core may read/write it with Target commands." **Must be false for this slot**: the whole point is that APF loads it by itself.
* `address`: "Load address. 32-bit unsigned." The bridge writes the file at `address + offset`.
* `size_exact`: "the filesize will be checked and only loaded if the size matches": a file of another size is **refused by the Pocket with an error screen** (CORRECTED 2026-11-23, B-1149: the first version of this line said "not loaded, reads as no file"; the Pocket shows `Error in framework file id [11] size bad` instead, owner-observed on the release core; whether the core starts after it is to confirm) (we use it as a guard: a truncated or foreign file reads as "no file", never as data).
* `size_maximum`: "the file will not load unless it is equal or smaller" (and the fill size of bit 5).

The slot (as `tools/tau_data_slots.py add_save_probe_slot` writes it for the spike; the release slot differs only in name and file):

```json
{ "name": "Meter save", "id": 11, "required": false, "nonvolatile": true, "parameters": "0x2",
  "filename": "tau-save-probe.bin", "size_exact": 8192, "size_maximum": 8192, "address": "0x30000000" }
```

Slot id **11**: the ids in use are 1, 2, 4 to 10 (checked in `tools/tau_data_slots.py`; id 3 is unused and is left alone). The name is at most 15 characters (docs-verified: "Maximum length of 15 characters"; the first draft "Meter save probe" had 16).

Parameters (docs-verified bitmap, `core-definition-files__data-json.txt`):

| Bit | Meaning | Our choice |
|---|---|---|
| 0 | user-reloadable | **0**: the user must never reload this slot at run time (it would write into the RAM under the firmware) |
| 1 | core-specific file | **1**: "/Saves/<platform>/AuthorName.CoreName/examplefile.bin" (docs-verified). Each core keeps its own file: a TEST core can never overwrite the release's settings. The cost: a DEV core starts from defaults. |
| 2 | nonvolatile filename: cloned from slot 0 + this slot's extension | **0**: the file name is the one in the slot. **This is the bit that hurt in September**: a nonvolatile slot named after another slot wrote its contents over the user's music (`docs/A088_UPSTREAM_CHECKS.md` C5: "Upstream's corruption came from a write applied to whatever file occupied the audio slot"). Never set it. |
| 3 | read-only | **0**: it must be written back |
| 5 | initialise: if the file is missing, fill the memory with 0xFF up to `size_maximum` | **0 for the seed route** (S1 probe 1); **1 in S1 probe 2** to measure whether a missing file can be created by the Pocket itself (section 1.4). One bit at a time (the skill's rule 4). |
| 6 to 8 | reset / restart / reload around a user reload | 0: the slot is never reloaded by the user |
| 9 | persist the browsed filename | 0 |
| 25:24 | platform index | 0: `platform_ids[0]`, i.e. `tau` for the release, `tau_dev` for a TAU DEV core (**the exact folder is to measure**: expected `Saves/tau_dev/alfatreze.TAU DEV 227/tau-save-probe.bin`; the plan's `Saves/tau/<core>/` is right for the release only) |

### 1.2 The size table

* KB-001 (source-verified, high): the size APF writes back at shutdown "comes from the core's data-slot size table, indexed by slot position", entry `position*2+1`; "if the entry is 0 (no file was loaded), the flush writes 0 bytes: no file and no error". Measured on Tau 2026-08-05 (`core_game.vh`, comment at `datatable_addr`): the table in `mf_datatable` **is APF's own** `{slot_id, size}` list (word `2i` = id, `2i+1` = size).
* KB-002 (community-reported, medium): writing 0 into an entry while APF delivers slots made APF stop after one sector.
* **Our rule: the core never writes the size table for this slot.** APF fills the entry when it loads the file (a seed file always exists, so the entry is 8,192); the firmware only **reads** it (`SV TABLE` row of the self-test) to prove what the Quit will write. The NGPC core writes the entry itself because its file may not exist at boot; ours always does. (`EXTERNAL_METERS_SPEC.md` section 5 listed "a size-table entry" as a cost of the native route: it is not needed.)

### 1.3 The load path and the flush path

* **Load (boot).** Boot order (docs-verified, `core-boot-process.txt`): ... up to 32 bridge writes of JSON data, then `0082` Data slot request write and the data for each slot in order, `008F` all complete, ... `0011` Reset Exit. Source-verified in `core_top.v`: `dataslot_requestwrite_ack = dataslot_requestwrite_ok = 1`, so every slot is accepted. The slot's words arrive as **bridge writes at `0x3xxxxxxx`** (`bridge_endian_little = 0`: the first file byte is bits `[31:24]`, source-verified in `io_bridge_peripheral.v`). `tau_save_ram` takes them straight into port A of the RAM on `clk_74a`. **There is no clock-domain crossing on the load path**, so the fence problem of KB-076 (a "done" overtaking writes still in a FIFO) does not exist: the CPU leaves reset only after `dataslot_allcomplete` (`core_game.vh` section 3: `cpu_reset_n` needs `fw_loaded`), by which time every write is in the RAM.
* **Flush (Quit).** Docs-verified (`core-boot-process.txt`, shutdown): "0010 Reset Enter ... save interact persist ... Pocket sends [0080 Data slot request read] to read out any nonvolatile data, taking into account any changes the core may have written to the Dataslot ID/Size Table". KB-004 (source-verified from three cores, never run on Tau): "When APF flushes a nonvolatile slot it reads the slot back through ordinary bridge reads at the slot's `address` range". `core_top.v` answers `0080` with ack and ok today. **To measure on a Pocket: that this really happens for a slot whose `address` lies in the `0x3` region** (S1).
* **Order at Quit** (docs-verified): `0010` first. `cpu_reset_n` includes `reset_n_sys`, so **the CPU is held in reset before the first read** (source-verified): the RAM is quiescent, the snapshot is coherent without any handshake. Quit by "Pocket turned off" or "slept" (docs-verified: the flush "happens whenever the core is shutdown - ... Quit, Pocket is turned off, or Pocket is slept") may or may not send `0010` first: **to measure**. The file format is built to survive both (section 4.5): the CPU never modifies the generation that is the only valid one.
* **Quit in the middle of a CPU write:** a single word is written atomically; a record update is not, which is what the two generations and the CRC are for (section 4).

### 1.4 Who creates the first file, and what the core does without one

KB-074 (community-reported, medium, one source): "The Pocket only writes back a slot it *loaded*, so the exit-time flush can never create the first file." So:

1. **The seed is created by the installer** (`tools/install_dev_core.py`: created when missing, **never overwritten**, verified; host-tested in `sim/test_install_dev_core.py`), **by Tau Omega and Pocket Sync-style tools** (create a zeroed 8,192-byte file when it is missing, never touch an existing one), and **by the release zip** (`tau-compat.json` lists it as a `user` file with `"seed": true`: an installer creates it when missing). A user who deletes it gets defaults and **no write-back** (nothing is loaded, nothing is written: KB-074), which the firmware reports (below) and the plugin's Check repairs.
2. **S1 probe 2 tests the alternative** (parameters `0x22`, no seed on the card): if the Pocket fills the memory with 0xFF **and** writes the file back at Quit, a missing file heals itself and the seed becomes a nicety. Not assumed. It is a data.json-only change (no refit).
3. **What the core does on a bad file** (decided here; the same code for every case):

| File state at boot | How the firmware sees it | What it does |
|---|---|---|
| missing, slot not loaded | `SAVE_ST` bits `[15:0]` (words the bridge wrote) = 0 | start from defaults; row "SETTINGS FILE: MISSING" (S2: About page); settings are kept in RAM and the old two-tuning pool for the session; **nothing is written back by the Pocket** |
| wrong size | **the Pocket refuses it at load: `Error in framework file id [11] size bad`** (B-1149; NOT "same as missing" as first written): the core never sees it | the user must delete the file (the Pocket recreates it); Omega must never leave a file of another size |
| all zero (the seed) | no valid generation (magic is zero) | defaults, silently; the first edit writes generation A with seq 1 |
| all 0xFF (bit 5 fill) | no valid generation | same |
| one generation damaged (CRC) | the other is valid | use the other; the first write repairs |
| both damaged | none valid | defaults; never refuse to boot; keep the bytes until the first edit overwrites them |
| right magic, newer version | version > 1 | defaults; **do not overwrite** until the owner decides (a downgrade must not destroy a newer file): the firmware keeps the RAM image untouched and edits stay session-only, row says why |

## 2. The bridge read path: VERDICT

**Feasible, with this change in `core_top.v` (three lines under `ifdef TAU_SAVE`):** a `32'h3xxxxxxx` case in the `bridge_rd_data` mux that returns the save RAM's word. Evidence (source-verified):

* `core_top.v` lines 309 to 316 are **the core's own mux**, not the shell's: `32'hF8xxxxxx` to `core_bridge_cmd`, `32'h2xxxxxxx` to `set_bridge_rd_data`, default 0. The `0x2` case is the interact persist register file (`core_game.vh` section 8), read back by APF every frame and saved to `Settings/.../interact_persist.json` at Quit: **hardware-validated** (B-845, KB-035). The shell (`src/fpga/apf/io_bridge_peripheral.v`) does no decoding: it forwards `pmp_addr` and samples `pmp_rd_data`. So an address-decoded read of a core-owned memory through this mux is proven on a Pocket today.
* **Timing and protocol, from the shell's source and the docs (CORRECTED 2026-11-22, B-1123 to B-1126: the first version of this paragraph had it backwards and the DEV 227 hardware showed it).** `io_bridge_peripheral.v` states in its header that **reads are buffered by one word**: "reads should always return the current bus value, and kickstart into the next read immediately", and the docs (Bus Communication) say the core "has up until the next read strobe to drive `bridge_rd_data`". In one read transaction the shell latches the address, waits `read_cnt == 3` (four `clk_74a` edges), captures `bridge_rd_data` for the host (`spis_word_tx <= pmp_rd_data_e`), then pulses `pmp_rd` with the address still on the bus: **the strobe is the request for that address, and the word it asks for is what the NEXT transaction returns**. The Pocket therefore reads a file with one extra, discarded transaction (file word j = the return of transaction j+1). agg23's `data_unloader` is built that way (it latches `bridge_addr` at the `bridge_rd` strobe). A core that answers for the LIVE address (the first version of `tau_save_ram`) delivers file word j = RAM word j+1 and a zero last word, exactly as measured. `tau_save_ram` now latches the address at the strobe (own region only) and returns that word; `sim/tb_tau_save_ram.v` and `sim/tb_save_soc.v` model the buffered protocol, and the old live-address answer is mutant 1 (killed).
* The persist words use the same mux with a register file; M10K needs one register stage. No `data_unloader`, no `read_avail` handshake (KB-026) is needed because the memory has fixed one-cycle latency.
* The datatable (`F8xx2xxx`) is **not** the only way to hand APF a memory; it is only the way `core_bridge_cmd` offers. The stale comment should be corrected when the macro goes in the default build (S2).
* **Open, to measure on a Pocket (S1):** that APF reads a `nonvolatile` slot's range with ordinary bridge reads at all (KB-004 is from three other cores' source, never run on Tau); that nothing hangs at Quit.

## 3. The RTL design

`src/fpga/core/tau_save_ram.sv`, built only with **`TAU_SAVE`** (`core_game.vh` maps it to `TAU_SAVE_EN` and to the SoC parameter `SAVE_ENABLE`; `core_top.v` has the `0x3` case under `ifdef`).

* **Memory:** `2**AW` words of 32 bits, `AW = 11` (8 KB), a true dual-port RAM with two clocks and `ramstyle "M10K, no_rw_check"`. Port A on `clk_74a` (the bridge), port B on `clk_sys` (the CPU). Estimated **8 M10K blocks** (true dual port is at most 20 bits wide per block: 4 deep by 2 wide); the release fit has 274 of 308, so 282 expected (to read in the fit report). `AW = 10` (4 KB, 4 blocks) is one parameter away if the owner prefers.
* **Bridge side:** `bridge_hit` = `bridge_addr[31:28] == 3` and the word is inside the RAM (above it: reads return 0, writes are dropped, `BUG 5` of the bench). A write is one cycle on port A; a read is one registered stage.
* **CPU side, three MMIO registers** (offsets free in `docs/MMIO_ALLOCATION.md`; defined in `fw2/diagnostics/save_probe.c` and not in `fw2/platform/mmio.h`, because the committed WebAssembly meter modules hash that header):

| Offset | Name | |
|---|---|---|
| `0x1EC` | `SAVE_ST` | read: `[31]` built in, `[30]` the bridge has written the RAM, `[28:24]` AW, `[15:0]` words the bridge wrote (a Gray-coded counter, `tau_cdc_gray_ctr`) |
| `0x1F0` | `SAVE_IDX` | word index, R/W |
| `0x1F4` | `SAVE_DATA` | the word at the index, R/W; **every access steps the index** (auto-increment) |

  The CPU sees the file in **little-endian file order** (the RTL swaps the bridge's big-endian words), so `memcpy` semantics hold. The read data register lags the index by one cycle, which is exactly the slack back-to-back MMIO accesses leave; **the real-CPU bench proves back-to-back reads work** (`sim/fw_save/main.c` MODE 0), and the firmware accessor still puts one `SAVE_ST` read between an index write and the first data read.
* **Write-through:** the CPU writes each change into the RAM at once (a record rewrite is about 1,000 words, ~100 microseconds). Quit can come at any moment; nothing is "saved at exit" by the core, because **the core cannot run at Quit** (it is in reset). The snapshot is the RAM.
* **Clock-domain crossing:** the memory itself is the crossing (true dual port, two clocks, **different addresses at the same time are safe**; the same address at the same instant is undefined and is excluded by protocol: the CPU is in reset while APF writes at boot, and in reset while APF reads at Quit; for a Quit without reset the CRC and the generations cover it, section 4.5). The only other crossing is the write counter (Gray code, as `tau_rtc_cdc` and the SDRAM statistics). `core_constraints.sdc` already declares `clk_74a` and the PLL outputs asynchronous to each other (read, source-verified), so no new constraint is needed (to confirm in the fit report: no new unconstrained path).
* **Macro off:** nothing exists. **Argument, not a Pocket test:** (a) `core_top.v`'s mux case and wire are under `ifdef`; (b) in `tau_soc.v` the three registers read 0 as before (the default of the read mux is `xm_range ? xm_rdata : 0` and `xm_range` is false at `0x1EC` to `0x1F4`, source-verified), the write cases are empty, `SAVE_ENABLE = 0` folds the rest; (c) **yosys + abc on `tau_soc` before and after (flattened, `SAVE_ENABLE = 0`): identical flip-flop and memory counts, 8,283 LUTs after against 8,288 before** (generic LUT6 mapping, not Quartus; the difference is abc noise, not logic); (d) `make test-rtl-save-soc` has the mutant "block left out of the SoC", which is the macro-off SoC, and the real firmware sees `SAVE_ST` read 0 and the self-test report "not present". The definitive proof is the release fit of the default tree, which is the fit that produces the shipped bitstream anyway.
* **Tests (host-proven):** `make test-rtl-save-ram` (`sim/tb_tau_save_ram.v`: the bridge's LOAD and FLUSH cycles modelled on `io_bridge_peripheral.v`, the CPU's MMIO cycles, concurrent access on different addresses, junk writes to other regions and past the end, status; **four clock ratios; six mutants killed**: late read, no region check, no byte swap, read does not step, no range check, write does not step) and `make test-rtl-save-soc` (`sim/tb_save_soc.v`: the same RAM behind the real `tau_soc` with the real VexRiscv running `sim/fw_save/main.c`; MODE 0 back-to-back MMIO and the flush against the model; MODE 1 the self-test across **three boots**: a zero file, the file the Quit wrote, a file whose tail was lost; five more mutants killed).

## 4. The file: `tau-meter-settings.bin` v1

All numbers little-endian. **8,192 bytes = generation A (bytes 0 to 4095) + generation B (4096 to 8191).**

### 4.1 Generation

| Offset | Size | Field |
|---|---|---|
| 0x00 | u32 | magic `54 4D 53 46` ("TMSF") |
| 0x04 | u16 | version = 1 |
| 0x06 | u16 | count of records |
| 0x08 | u32 | seq (generation counter; the larger wins by serial arithmetic) |
| 0x0C | u16 | used = bytes of records |
| 0x0E | u16 | reserved = 0 |
| 0x10 | u32 | CRC-32 (zlib) of bytes `0x00..0x0F` followed by the `used` bytes of records |
| 0x14 | ... | records, `used` bytes; the rest of the 4 KB is zero |

### 4.2 Record

`id_hash u32 | params_rev u16 | preset u8 | n u8 | n x ( index u8, value u16 )` = **8 + 3n bytes**.

* `id_hash`: the meter's 32-bit id hash (the TOC key, `EXTERNAL_METERS_SPEC.md` section 2). Never an index: installing, removing or reordering meters changes nothing.
* `params_rev`: the meter's parameter revision from its manifest (u16; `tools/persist_registry.json` carries the shipped meters' values). A record whose `params_rev` differs from the meter's current one is **ignored on read and dropped at the next write**: the meter starts from its defaults (the same rule as today's tuning key).
* `preset`: the position of the preset the settings started from (0 to 254; 255 = none).
* The `n` pairs are **only the parameters that differ from the default, or from the preset when one is named**, in index order, index < 64, value u16 (the parameter's stored integer; the descriptor's `max` is at most 16 bits, `tune_core.h`).
* **Order in the file = recency, oldest first.** A write removes the meter's old record and appends the new one at the end. This makes the bytes of a generation a pure function of the history, which is what the golden vectors need.

### 4.3 Capacity and eviction

Worst case 64 meters with 16 changed parameters each: 64 x (8 + 48) = 3,584 bytes; the generation holds 4,076. A typical meter (4 changes) is 20 bytes. **If a generation would overflow**, the oldest records are dropped until it fits (the least recently changed meter goes back to its defaults). The 8 KB cap of the plan is met with room: it is two copies of a 4 KB file, not one 8 KB file.

### 4.4 Reading, writing, migration

* **Read at boot:** read both headers; a generation is valid when magic, version, `used <= 4076`, `count` records that exactly fill `used`, and CRC all hold; take the valid one with the larger seq. Both invalid: defaults, `seq = 0`.
* **Write (every committed edit):** build the new generation **in the other bank**, records first (streaming, CRC accumulated on the way), header last, `seq = newest + 1`. The valid generation is never modified, so at any instant at least one complete generation exists.
* **CRC failure:** start from defaults (or from the other generation), **never refuse to boot**, report in the support rows.
* **Migration from today's two tunings** (S2): on the first boot where the file has no valid generation and the persist words hold a sound tuning record (`tune_core.h`: `key = id_hash[15:0] | params_rev[3:0] << 16`, preset, parameters in index order, 124 bits each), the firmware maps each key to the full `id_hash` through the registry (the keys of the seven shipped meters are unique, `shipped_meter_ids.h`), writes two records, and from then on the file wins; the persist words are left as they are (`persist_layout` unchanged; S2 may move to v3 to free them). A tuning whose meter is unknown is dropped.
* **One-writer rule:** while the core runs, **only the firmware writes the RAM, and APF writes the file at Quit**. Mac-side tools (Omega, `tools/meter_settings.py`) may read the file and may create the zeroed seed when it is missing; they must **never rewrite a file that holds a valid generation** while a Pocket may hold a different state (the card is mounted, so the core is not running, but a stale edit made from an old copy would silently revert the user's later choices). An editor, if ever needed, writes a **new generation with `seq + 1`** into the older bank (the same append-only discipline), never in place.
* **Python reference and golden vectors for Omega:** `tools/meter_settings.py` (parse, validate, build, apply a change with eviction) and `docs/schemas/fixtures/tau-meter-settings-*.bin` with a `.json` describing each (a fresh file, three records, a full file with eviction, one damaged generation, both damaged, a newer version, a v1 seed of zeros). **A message to Omega comes first** (its repository is not touched from here): the proposed text is in section 6.5. Not built in S1; the S2 plan is to write the C parser and the Python reference against each other, as `sim/test_p2_tune.py` does for the tuning record.

### 4.5 A Quit that does not stop the CPU (sleep, power-off): why the file is still sound

If the Pocket flushes without sending `0010` first, the CPU may be writing while APF reads. The CPU only ever writes the **inactive** generation. APF reads A then B (about one millisecond in all). Whichever way the instants fall, the file holds at least one generation that was complete when it was read: a half-written one fails its CRC, and the active one is intact. The worst outcome is losing the **latest** edit, never the file. (Concurrent access to the **same address** in the two clock domains returns an undefined word to the reader; the CRC catches it.)

## 5. Firmware

* **The second backend behind W1's interface.** W1 (`fw2/ui`, not merged when this was written) defines `meter_settings_get/put(id_hash, ...)` with today's two-tuning pool behind it. This spec **assumes** records keyed by `id_hash` and `params_rev` (the plan says so) and proposes, to be matched to W1's real signature when it is merged:

```c
typedef struct { uint32_t id_hash; uint16_t params_rev; uint8_t preset; uint8_t n; struct { uint8_t index; uint16_t value; } p[MSET_MAXP]; } mset_rec_t;   /* MSET_MAXP = 24, as TUNE_MAX */
int  meter_settings_get(uint32_t id_hash, uint16_t params_rev, mset_rec_t *out);   /* 1: found and params_rev matches */
void meter_settings_put(const mset_rec_t *rec);                                   /* write-through: returns when the other generation is complete */
```

  The backend is chosen **at run time by `SAVE_ST` bit 31** (the RAM is built in) and by a sound file; otherwise the two-tuning pool stays in use: **no `CORE_VERSION` bump and no interlock** (the project's own rule: a runtime probe when a mismatch is safely testable; reading `0x1EC` on a bitstream without the RAM returns 0). The release bitstream is the one with `TAU_SAVE`; the release firmware needs nothing else from it.
* **Cold code, hot RAM budget.** The backend is `P2_COLD_TEXT` code (~1.5 KB estimated) and **works as a stream**: lookups scan the active generation word by word through the MMIO window; a put copies the other records into the other bank while computing the CRC, appends the new record, writes the header last. No generation buffer in hot RAM (4 KB would not fit: the free heap is about 1 KB above the floor). Its few words of state (active bank, seq, last result) live in the cold image (`.cold_data`, writable), as the S1 self-test's do.
* **S1 measurement of the claim "free hot RAM does not shrink"** (`fw2/build.sh PACKS=1`, `tau.rom` and the ELF sections):

| Build | Before (main) | After (this branch) |
|---|---|---|
| `DIAG=0` (normal) | 110,164 B ROM; text 336,496, data 1,244, bss 43,132; cold image id `0x1E0729E1`, **byte-identical** | **identical** |
| `DIAG=1` (diagnostic) | 112,420 B ROM; hot `.rodata` 0x40F0, `.data` 0x4E4, `.bss` 0xA98C; cold image 248,256 B | 112,420 B ROM, **same hot sections**; cold image 249,756 B (+1,500 B cold) |

  (Two mistakes were found and fixed on the way by exactly this measurement: out-of-line helper functions in hot text, and a `const` function-pointer table, 12 bytes of hot `.rodata`.)
* **S1 self-test:** `fw2/diagnostics/save_probe.c` and the pure core `fw2/cores/save_probe_core.h`, a **registered diagnostic feature** (`fw2/diag_features.json`: the source, the ten `SV ...` counter rows, the symbol `save_probe_run`; `sim/test_p2_diag_split.py` checks the register against both builds). It runs once per boot from `counters_init()` (cold code), reads the 8 KB probe file, verifies it (CRC-32 and a pattern), and writes a new counter, value, seed and pattern (the header last). It does not depend on W1's work or on any `fw2/ui` file.

## 6. Packaging and tools

1. **`tools/tau_data_slots.py`:** `add_save_probe_slot` (done, S1). S2 adds `add_save_slot` for the release (`"Meter settings"`, file `tau-meter-settings.bin`, the same id 11, parameters `0x2` or `0x22` as S1 decides).
2. **`tools/package_dev_build.py --save-probe [--save-probe-params 0x22]`** (done): declares the slot, writes the zeroed seed under `Saves/<platform>/<core>/`, and **refuses** a package that is not a Diagnostic Build or whose bitstream manifest does not list `TAU_SAVE` (the Report would show `SV PRESENT 0`).
3. **`tools/tau_compat.py`** (done): a `Saves/...` file in a zip is accepted only as the seed of a nonvolatile, core-specific, non-deferload slot named in `SAVE_FILES`; it becomes a `user` entry with `"seed": true` (create when missing, never overwrite); a save slot without a shipped seed is still listed (`"seed": false`) so the plugin's Check knows the path. Any other file under `Saves/` stops the build. The release bitstream's `CORE_VERSION` is **unchanged** by this feature (section 5), so the manifest's pairing fields do not move.
4. **`tools/install_dev_core.py`** (done, rule 4): the dry run prints `save: <path>: missing, will be created (zeroed seed)` or `exists on the card, kept as it is`; the install creates the file with SHA-256 verification; a `--replace` never overwrites it; a restore (failed copy) removes a seed it had just created. `sim/test_install_dev_core.py` covers each. It never touches `Saves/` otherwise (removing a core leaves its saves).
5. **Omega and Pocket Sync (community-reported for the latter; ask before assuming):** an update that replaces `Assets/<platform>/<core>/` and `Cores/<core>/` must leave `Saves/<platform>/<core>/tau-meter-settings.bin` alone (it is a `user` entry in `tau-compat.json`, the same class as `tau-assets.bin` and `tau-meters.bin`); Pocket Sync is understood to back up the `Saves/` tree as ordinary save files, which is the behaviour wanted here, but it was not checked. **Draft of the message to Omega** (to be sent by the owner or the parent session; nothing is written to the Omega repository from here): "Tau 0.7.5 adds a save file `Saves/<platform>/<core>/tau-meter-settings.bin`, 8,192 bytes, written by the Pocket at Quit. tau-compat.json lists it as role `user`, `seed: true`: create a zeroed file of that size when it is missing, never overwrite an existing one, never write it while a core could be running, keep it across updates and on a core remove/reinstall of the same core folder. Read-side format and golden vectors will follow (`docs/features/meters/METER_SAVE_FILE_SPEC.md` section 4)."
6. **The plugin's Check** (S3): the file exists, is 8,192 bytes, has at least one valid generation (the Python reference), and the data slot in `data.json` matches. A missing file is a repairable warning with a button that creates the seed.

## 7. Risks, the Pocket test ladder, the exit rule

### 7.1 Risks (ranked)

1. **APF does not flush a slot by bridge reads at `address`, or not for a `0x3` address** (KB-004 is from other cores' source). Kills the route. S1 step 5 answers it independently of everything else (the file on the Mac).
2. **A hang at Quit or boot.** Upstream's nonvolatile slot hung both (`docs/A088_UPSTREAM_CHECKS.md` C4); the recorded cause there was the table clobber, which this design does not do; still the first thing watched.
3. **Where the Pocket looks for the file** (platform folder for a dev core, core folder name with spaces): to measure; S1 probe 2 (or the Pocket's own error) shows it.
4. **A file that is loaded but whose write-back is not at the loaded size** (KB-001 says it is; to measure: the file size after Quit).
5. **Timing closure of the fit** (the release family has fast-0C hold margin of only about +0.08 ns; the RAM adds 8 blocks and a few dozen ALMs).
6. **A seed that does not exist** (user deletes it, a tool never created it): defaults and no write-back, reported.
7. **A new bitstream for 0.7.5** (release fit, the ROM pairing, Omega): already in the plan.

### 7.2 The S1 ladder (details and photographs: `docs/features/meters/METER_SAVE_FILE_TEST.md`)

| Step | Where | Passes when |
|---|---|---|
| 0 | host | **done**: `make test-rtl-save-ram`, `make test-rtl-save-soc`, `sim/test_p2_save_probe.py`, `sim/test_install_dev_core.py`, `sim/test_p2_diag_split.py` |
| 1 | VM (owner approves, rule 3) | `tools/vm_fit.py launch ...` for the TAU_SAVE bundle: Successful, all corners positive, **RAM blocks 282**, the RAM mapped as a dual-clock true-dual-port M10K |
| 2 | Mac | package `TAU DEV 227`, dry-run the installer, owner approves the card write |
| 3 | Pocket | boot: `SV PRESENT 1`, `SV LOAD WORDS 2048`, `SV TABLE 8192`, `SV BOOTS 1`, `SV CRC 0` |
| 4 | Pocket | **root-menu Quit**; no hang |
| 5 | **Mac** | `tools/decode_save_probe.py` on the file: `status ok`, `boots 1`, CRC and pattern good. **This alone proves the flush route.** |
| 6 | Pocket | boot again: `SV BOOTS 2`, `SV CRC 1`, `SV PAT BAD 0`, `SV LAST` = the previous `SV WRITTEN`: the load route is proven |
| 7 | Pocket | repeat three times; then Quit one second after boot; Quit during playback; **power off**; **sleep** (the plan says sleep may flush: record what the file shows) |
| 8 | Pocket | probe 2 (`0x22`, no seed): does the Pocket create the file (who creates the first file) |

**Exit rule:** if step 5 fails (the file is not written back, or is written with another size or content), **stop**: no format, no firmware backend, no S2; the fallback is the two-tuning pool, and the owner decides. A step 6 failure with step 5 passing is a load-path problem and is debugged on the same bitstream (the data.json variants need no refit).

### 7.3 The final test (end of W2, S3)

On a release-style build with the real backend: change settings of at least eight meters (including one that is not the set meter, W1's Configure), Quit, power off, sleep, restart, verify every value; damage each generation in turn on the Mac (CRC) and boot; delete the file and boot; fill the file with 0xFF; install over it with `--replace`; Omega's update flow; the golden vectors against Omega's reader. The owner's gate in the plan: "W2's S1 spike and the final save-file test on a Pocket".

## 8. Where the plan was ambiguous or wrong (quoted)

* "two seeds (about 1h45 each, say the finish time when launched)": **two seeds launched together take about twice as long, not the same time**: `wdog-w3` (two seeds, launched 07:15) had its fitter finished at 09:41 and its reports at about 10:40 (B-940: "about 3h25, twice a single fit"); a single seed on an idle VM takes 1h21 to 1h32 (`rc-b936`, `def-b931`). The fit recipe and the durations are in `METER_SAVE_FILE_TEST.md` section 1.
* "(the) installer or Omega pre-creates a zeroed file" is right and is implemented; the file must be **in `Saves/`, not `Assets/`**, and for a DEV core in `Saves/tau_dev/<core folder>/`, not `Saves/tau/`.
* `EXTERNAL_METERS_SPEC.md` section 5 says the native route "needs ... a size-table entry": **it does not** (APF owns the entry; the file exists, so the entry is the loaded size). It also says "a bridge-readable BRAM": that is the new 8 KB RAM and the new mux case, as expected.
* `core_game.vh` ("the ONLY way to hand APF a memory region it can read") is **stale** (section 2).
* "`persist_layout` unchanged or v3 (decide then)": left for S2; nothing here forces v3.
* "the core must cope with a missing or zero or damaged file": the table in 1.4 is the decision; the one thing the core cannot do is **create** the file (KB-074) unless probe 2 shows otherwise.
* W1's `meter_settings_get/put` signature was not available (W1 not merged): section 5 states the assumption.
