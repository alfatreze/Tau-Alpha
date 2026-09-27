# RAM shrink to 192 KB — what it takes and what it buys

## Status 2026-09-27 (B-333): PREPARED. Firmware links at 192 KB; the paired bitstream is being fitted; nothing shipped

| Piece | State |
|---|---|
| RTL: 192 KB main RAM (`TAU_RAM_192K`, `tau_main_ram.sv`) | Fit-proven with B11 (B-235). The current 0.6 candidate (poly + pipelined blend + 192 KB) is fitting on the VM (`ram192-blend-b333`, two seeds). |
| Pairing rule (B-245) | **Enforced.** The 192 KB bitstream reports `CORE_VERSION` rev 24 (`4D503318`); every other bitstream rev 23. A firmware linked for 256 KB refuses rev 24 at boot (the existing interlock), a 192 KB firmware accepts both, so it can be tested on today's bitstream first. |
| Firmware links at 192 KB | **Yes**, release, Diagnostic Build and the profile build (numbers below). `RAM_192K=1 bash fw/build.sh <target>`; output goes to `work/ram192k/` (release) so `dist/` is never touched. If a link misses, `build.sh` now prints by how much. |
| Normal 256 KB builds | Byte-identical to v0.5.0 (all savings are behind `TAU_RAM_192K_FW`, i.e. `RAM_192K=1`). Checked by `make test-ram192k`. |
| Test build on hardware | `alfatreze.TAU_0_5_0_A_36` (192 KB firmware on the existing 256 KB gamma bitstream, packaged, not installed). Runs on today's bitstream; the point is to exercise the cold-code moves before the bitstream changes. |

### What was done (measured, heap gap = free RAM under the linker's DMA buffers)

| Change | Saved | Notes |
|---|---|---|
| Stack 16 KB -> 8 KB on the 192 KB link | 8,192 B | Worst measured peak 1,672 B (B-230). |
| `player.c` compiled `-Os`, audio-critical functions kept `-O2` (`HOT_O2`: `meters_feed`, `refill_pump`, `refill_drain`, `flac_emit`) | about 8.4 KB | Decoders (Helix, `flac.c`) are separate translation units and stay `-O2`. `main` and `poll_input` are `-Os` too. |
| `__udivdi3` removed (`udiv64`, host-tested against native division) | about 1.1 KB | 14 call sites, none per audio frame. |
| `load_track` and `read_track_head` cold | about 7.6 KB | Once per track, in the silent gap; needs the cold image. |
| `ui_draw_chrome`, `ui_idle_screen`, `flac_seek_locate`, `set_input` cold | about 5.7 KB | Hot callers guarded (`SR_READY()`). |

Resulting free RAM at 192 KB (floors: release 6,144 B, Diagnostic 4,096 B):

| Target | 256 KB (today) | 192 KB link | Margin over the floor |
|---|---|---|---|
| `release` | 49,712 B | **12,272 B** | +6.1 KB |
| `player-library-diagnostic` (the shipped Diagnostic Build) | about 54.6 KB | **6,528 B** | +2.4 KB |
| `player-library-diagnostic-profile` (dev) | about 53.2 KB | **5,200 B** | +1.1 KB |

The Diagnostic Build's margin is thin. Durable margin (about +15 KB) is still the list below: split `main`, a hot `poll_input` stub, UI screens that are not needed before the cold image loads.

### Ship procedure (in order, each step verified before the next)

1. Fit result (`ram192-blend-b333`): pick the seed with the best margins, `tools/vm_fit.py collect`.
2. Test alpha.36 on the current bitstream (below). If the 192 KB firmware misbehaves, fix it here, where nothing depends on the new bitstream.
3. Package the 192 KB firmware with the new bitstream, test on the Pocket: same script, plus Check ENDURANCE and the stack-peak reading (the 8 KB stack claim).
4. Release: `make_release.py` (release and Diagnostic both need the 192 KB firmware, since they share one bitstream), `make test-ram192k`.

### What to test on alpha.36 (192 KB firmware, current bitstream)

Track loads (MP3 and FLAC, the first and later ones, an album change), skipping, seeking in MP3 and FLAC (`flac_seek_locate` is cold now), Start opening and closing every menu (set_input is cold), the idle/empty screen, the player screen after menus, and the Info page (FIRMWARE, FREE RAM, LOAD MS: compare the load time with alpha.35, the cold fetch should add only milliseconds). Then a USER CHECK and a STANDARD check and a stack-peak reading. What must not happen: any hang at a track change, a blank player screen, a menu that does not open.

### Known risks

* Cold code runs from PSRAM at about 32 cycles per instruction word; `load_track` and the chrome are once-per-track, but confirm LOAD MS on hardware.
* Without the cold image the player now shows nothing (no menus, no chrome, no track loads). That is the owner's decision (2026-09-25) but it is stricter than before; `tau-cold.bin` is always shipped beside the ROM.
* `-Os` slows non-audio code a little; the audio-critical loops keep `-O2`. CPU load is the number to compare (Meter Sweep).

---

*The rest of this document is the original plan (2026-09-25, B-262), kept for the reasoning; its numbers are from that date.*


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
