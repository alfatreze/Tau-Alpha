# RAM shrink to 192 KB — what it takes and what it buys

Status: **PLAN, saved for review (2026-09-25, B-262). Nothing here is started.** The RTL side is done and fit-proven
(B-223..B-235, `TAU_RAM_192K`); this document is the firmware side and the honest value assessment.
Numbers are measured from the release ELF at commit `7b65c8c` unless marked [EST].

## 1. The gap

| | Bytes |
|---|---|
| `release` heap gap today, 256 KB RAM | 62,256 |
| The same image with RAM capped at 192 KB (64 KB less) | **-3,280** (the linker refuses: "firmware image collide") |
| Needed just to link | +3,280 |
| Needed to keep the release's 6 KiB heap floor (`HEAP_MIN=6144`) | **+9,424** |

Layout (low to high): image (116,720 B: `.text` 96,104 hot code, `.rodata` 19,792, `.data` 824, `.bss` 38,106, of which the
24,576 B arena holds Helix's 23,816 B decoder instance), heap, MP3 ring 24 KB (DMA target), stack 16 KB.
Hot code is 95.9 KB over 151 functions; about 36 KB is the MP3/FLAC decoder and must stay hot.

## 2. Where the bytes can come from

| # | Change | Expected gain | Effort / risk |
|---|---|---|---|
| 1 | Stack 16 KB -> 8 KB (or 6 KB). Worst measured peak 1,672 B (B-230, heavy R3 stress with MP3+FLAC+covers). | **+8.2 KB** (+10.2 KB at 6 KB) | One line in `fw/link.ld`. Low; the Check's stack-peak test keeps guarding it. |
| 2 | Remove 64-bit divisions (`__udivdi3` 1,128 B). Callers: `load_track`, `main`, `pcm_rate_apply`, `ui_draw_dynamic_cold`. | +1.1 KB | Small: 32-bit math. |
| 3 | Compile non-audio code with `-Os` (per-function attribute), decoder stays `-O2`. | +3 to 5 KB hot; cold image ~10% smaller (64.7 KB -> ~57 KB, boot ~20-30 ms faster) | Low. |
| 4 | `poll_input` (4.8 KB): tiny hot "did a key change" stub, dispatcher cold. | +4.3 KB | Small-medium; runs only on key events. |
| 5 | `read_track_head` (4.9 KB) + `load_track` (2.7 KB) cold. | +7 KB | Medium; about 0.6 ms of cold fetch per track load. |
| 6 | UI screens cold: `ui_draw_chrome`, idle screen, splash, boot tick, failure message. | +5 KB | Medium. Allowed by the owner decision (2026-09-25): no cold file, nothing works, so no fallback UI is needed in hot code. |
| 7 | Split the 13.9 KB `main`: audio loop hot; idle, paused, overlay, boot, reload paths cold. | +6 to 9 KB | Highest risk (audio path); same work as Phase 4 of `FIRMWARE_MODULARIZATION_PLAN.md`. |

Not worth it: the decoder (about 36 KB) and its Huffman tables; the 24.6 KB arena (it is Helix's instance); shrinking the 24 KB
ring (it is the SD-stall tolerance, about 0.6 s at 320 kbps).

## 3. Suggested order

1. **Link at 192 KB in about a day:** items 1 + 2 + 3 + 4 = about +10 to 13 KB, i.e. 1 to 4 KB above the 6 KiB floor.
   Verify with `RAM_192K=1 bash fw/build.sh release`, then the ENDURANCE Check and the stack-peak reading on hardware.
2. **Durable margin:** items 5 + 6 + 7 add about 18 to 21 KB (about a 20 KB cushion at 192 KB). Do these together with
   the modularization work rather than as one-off hacks.
3. **Pairing rule (B-245):** the 192 KB bitstream physically has 64 KB less RAM. A 256 KB-linked image on it corrupts
   silently. Ship the 192 KB bitstream only with a firmware that links under `RAM_192K=1`, and make the boot check refuse a
   mismatch.

## 4. What the shrink actually buys (the honest part)

Measured (real fits): main RAM 256 -> 192 KB frees **64 M10K blocks**: 299/308 used (9 free) -> **235/308 (73 free)**,
timing clean on both seeds together with B11 (B-235).

But the planned features in the ledger (`docs/PHASE_F_SPEC.md` section 4) do **not** need most of that:

| Planned consumer | Blocks |
|---|---|
| Blit engine follow-ons | about 2 |
| FLAC bit-reader accelerator (only if a profile justifies it) | about 2 |
| Hardware spectrum filter bank | about 0 |
| **Total planned** | **about 4** (of 9 already free) |

So **today no planned feature is blocked without the shrink.** It is headroom, not a requirement. What the 73 free blocks would
newly allow, none of it planned or decided: tile-based 2.5D for visualisers (about 20-25 blocks; a full-screen Z-buffer is
about 281 and stays dropped), a wider row buffer for `OP_COPY` (its 128-entry limit), larger line/blit buffers and caches, and room
for features not yet imagined.

**Cost of doing it now:** the firmware work in section 2, a new bitstream that must be paired with a matching firmware forever
after, and a permanent 192 KB ceiling for firmware growth.

**Recommendation:** treat the shrink as *deferred until a feature actually needs the blocks*. Do items 1 to 3 of section 2
anyway if firmware RAM gets tight (they are cheap and independent). Items 5 to 7 are worth doing on their own merit (RAM
headroom, faster boot from a smaller cold image) as part of the modularization, whether or not the 192 KB bitstream ships.

## 5. Decisions for the owner

1. Ship the 192 KB bitstream now, or defer until a feature needs the 64 blocks (recommended: defer)?
2. Stack: 8 KB or 6 KB (worst measured peak 1.7 KB)?
3. Are items 5 to 7 done as their own effort or folded into the modularization plan (recommended: folded in)?
