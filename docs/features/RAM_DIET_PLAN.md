# RAM diet plan (branch `ram-diet`, worktree `../tau-alpha-ram-diet`)

Measured 2026-10-07 on the shipped link (192 KB, CLK66, SDRAM_BUSY) from commit 31f03bf; `tools/check_heap_gap.py` now tracks exactly these:

| Build | Free (heap gap) | Floor |
|---|---|---|
| release | 15,504 B | 6,144 |
| release + tempo (ships in release too) | 8,752 B | 6,144 |
| Diagnostic Build (tempo on) | 1,328 B | 1,024 (lowered from 6,144 -> 2,048 -> 1,024) |
| Diagnostic profile | does not link, 3,376 B over | |

Rule: tempo is a live feature in both builds (only the 1.75x+ click fix is parked). Floors are not lowered again: a build that falls short sheds hot bytes instead.

## Candidates (hot bytes saved, status)
1. DONE: the guard measures the shipped link (this branch's first commit).
2. DROPPED: art working set shares `pcm`. Already in PSRAM since B-567 (see the update note in RAM_BSS_AUDIT.md).
3. Tempo state (`tempo_st`, 6,216 B): `out_l/out_r` (2 KB) and `ws.pend` (2 KB) to PSRAM, about 4 KB. Open question: they are int16 arrays and the PSRAM window is only proven for 32-bit accesses (the staging ring is word-wide for that reason), so they need word-wide access or a hardware proof first; CPU cost to be measured against the current 26-31%.
4. MP3 ring 24 KB -> 20 KB (4 KB): first turn the eight `RING_SIZE` uses into explicit byte constants (RAM_BSS_AUDIT.md 6.1).
5. `PolyphaseStereo` (5,004 B) to cold code (about 5 KB): only the self-check slots and the hardware-failure fallback call it; needs the COLD_READY() gating audit first.
6. Meter scratch overlay (about 2 KB), last.
