# A-088 next-run checks from HarpMudd upstream evidence

**Scope:** only the log-write path (APF data slot 5, `0184`). Nothing here
concerns the SDRAM CPU window; the SDRAM boundary is unchanged and still open.

**Source:** `harpmudd/HarpMudd.mp3player` (`ROADMAP.md`; commits `58e243e`,
`1258491`, `9b0af96`, `45b27a6`, `d6cf8b7`, `2da67db`, `4ca9711`). Upstream
statements are hypotheses for Tau until a Pocket result confirms them.

> **Correction (2026-09-19):** upstream's `ROADMAP.md` records `0184` as the
> write that destroyed three libraries (permanently off), its `nonvolatile` slot
> as a Pocket hang, and `interact.json` as the only working persistence. The
> "settings.bin rewritten via 0184" bullet below was an earlier, later-reverted
> attempt, not a working route. See AUDIT_TRAIL A-090.

## What upstream supports

- A `0184` write from the datatable worked upstream: `settings.bin` was
  rewritten in place at 32 bytes, sourced from word 192 (`0xF8002300`).
  `0xF8002000 + word*4` is the same addressing as Tau's `DT_BRIDGE_BASE`, so
  the A-088 base correction is consistent with upstream.
- Datatable layout: words 0–63 APF slot ID/size table; 64/128/192 command
  structs; 224–226 APF date/time. Tau's words 200–216 are clear of all of it.
- Upstream's `seq` completion counter in `tgt_cmd.v` is inherited, not new.

## Predictions to write down BEFORE the run

Upstream's rule: record the expected result first; a miss refutes, it does not
puzzle. Fill in before pressing anything.

| # | Prediction | Written before run? |
|---|---|---|
| P1 | Slot 5 table entry reads size 64 | ☐ |
| P2 | `WRITE D ERR 0` and file becomes non-zero (starts `TLOG`) | ☐ |
| P3 | `READ D` returns `544C4F47` (`TLOG`), not `00000000` | ☐ |
| P4 | Other slots' table entries unchanged after the write | ☐ |

## Checks

### C1 — Slot 5 size in the APF table (upstream: `0184` takes its size from it)
Upstream found APF sizes a `0184` write from its own ID/size table, not from
the command length alone. Before the write, open the Select+A table view
(`dt_snapshot`, `fw/player.c`) or read words 0–63 and locate the `{5, size}`
pair. Scan for the id; do not hard-code a word (upstream: observed order, not
specified).

- Size 64: consistent with `size_exact: 64`; proceed.
- Size 0 or other: the "file stays zero" symptom would follow from this alone.
  Record it; do not change SDRAM logic.

### C2 — Table integrity across the write
Snapshot words 0–63 before and after the write. Expect every slot id and size
unchanged (`TABLE INTACT`). `TABLE CLOBBERED` means something wrote into the
table region and invalidates the result.

### C3 — Source payload and readback (already in A-088)
Keep the A-087 source-word display (words 200–203 before `0184`). Compare with
`READ` data and with the saved file. Decision table:

| Source words | READ | File | Reading |
|---|---|---|---|
| valid `TLOG…` | `TLOG` | `TLOG…` | Path works; the base fix was the cause |
| valid | zero | zero | Still failing; go to C1/C2 before blaming the base again |
| valid | `TLOG` | zero | Write reached the bridge buffer; flush/APF persistence is suspect |

### C4 — Quit and sleep with the `nonvolatile` slot
Upstream's `nonvolatile` slot hung the Pocket on Quit (`d6cf8b7`) and on boot
(`4ca9711`), and was never retried after upstream found the table-clobber cause.
Tau's slot 5 differs (no address, `size_exact: 64`), and it boots, but no
Quit/sleep result is recorded. After the write, in order:

1. Quit the core to the menu.
2. Relaunch and confirm it boots.
3. Sleep and wake the Pocket.
4. Power off cleanly, then re-read the card file.

Note any hang, and whether the file changed across each step.

### C5 — Slot guard
`0184` must only ever target `LOG_SLOT_ID` (5). Upstream's corruption came from
a write applied to whatever file occupied the audio slot. Confirm the ROM issues
no `TGT_WRITE` with another id (disassembly or a runtime assert), and confirm
music, playlist, artwork and settings files are byte-identical before and after.

## Fallback channel (not a test; only if C1–C4 do not resolve it)
`interact.json` persist is a proven APF-written channel with no `0184`. Tau uses
12 of 16 register slots (4 free, about 124 bits): too small for the 64-byte
record but enough for a compact pass/fail code and first-fail code. Limits: it
needs a visible menu entry, and APF may not flush it on a hard power-off.

## Recording the result
Add the outcome to `docs/DIAGNOSTIC_RESULT_LOG.md` and `docs/AUDIT_TRAIL.md` with
screenshots and card hashes. Do not mark the log path passing without matching
screenshot and card evidence.
