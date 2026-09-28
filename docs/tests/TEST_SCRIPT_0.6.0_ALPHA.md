# User test: 0.6.0-alpha work so far (`TAU_0_6_0_A_3` and `TAU_0_6_0_A_6`)

Five 0.6.0 RTL changes exist this cycle: pipelined alpha blend, the 192 KB RAM shrink, `clk_sys`
66.667 MHz, Helios H2 double buffering, and the persist register widening (16→32 words). Only two of
them are on a card, and in two different combinations — this script covers exactly those two builds
and explains why the other two installed alphas don't need their own pass.

## Which build tests what (read this first)

| Core on the card | Bitstream | Firmware | What's new here |
|---|---|---|---|
| `TAU_0_6_0_A_6` | blend only (`b327` seed 1) | current (this week's) | Pipelined alpha blend (Scope trail), Settings fade-in, hardware rounded-rect (`OP_RRECT`), **FLAC/MP3 decode-stage profiler splits**, **persist widened 16→32 words** |
| `TAU_0_6_0_A_3` | blend **+ RAM shrink to 192 KB** (`ram192-blend-b333`) | slightly older (same trail/fade/rrect fix, predates the profiler splits and persist widening) | Everything alpha.6 has for blend/trail/fade/rrect, **plus the real 192 KB on-chip RAM cut** — physically 68 fewer M10K blocks than every build before it |
| `TAU_0_6_0_A_1` | old (gamma, pre-blend) | 192 KB-linked firmware, running on a bitstream that still has the full 256 KB | **Skip.** This was a preliminary link-only sanity check before the real 192 KB bitstream existed. It proves nothing alpha.3 doesn't prove better — alpha.3 is the real test of the shrink. |
| `TAU_0_6_0_A_2` | blend only (`b327` seed 1) | trail/fade/rrect fix, no profiler splits, no persist widening | **Skip.** Same bitstream as alpha.6, and alpha.6's firmware is a strict superset (same source line, built later the same day). Testing alpha.6 covers everything alpha.2 would show. |

**Not on any card yet — nothing to test:** `clk_sys` 66.667 MHz (timing-closed on its own, never
combined or installed) and Helios H2 double buffering (Quartus fit just finished both seeds
Successful — timing/slack not yet confirmed, not packaged).

**Recommended order:** alpha.6 first (safer, most complete firmware, ~45 min). Then alpha.3
(~35 min, the one build carrying real hardware risk since it physically removes RAM the older builds
had). Skip alpha.1 and alpha.2 unless you specifically want to compare against them.

Both are Diagnostic Builds. Take a screenshot (the Pocket screenshot combo) of every item marked
**[shot]** — screenshots are the evidence. Quit the core normally before pulling the card.

---

## Part 1 — `TAU_0_6_0_A_6` (primary test, ~45 min)

Start with a **cold boot** (power off, then on, then open `TAU 0.6.0-alpha.6`).

### A. Boot and audio basics (5 min)
1. Loading bar, then player or empty state. **Pass:** no blank screen, no reset, no hang.
2. Play an MP3, then a FLAC. **Pass:** both start within a couple seconds, no crackle, cover shows.
3. Pause/resume/next/previous/seek/volume. **Pass:** all respond, no stutter after seek.

### B. Alpha blend — Winamp Scope trail (10 min)
This is the first hardware run of the pipelined blend RTL (B-327) and its firmware use (B-334).
4. Settings > Appearance > Meter > Winamp Scope > Configure. **Pass:** the scope trace shows a
   fading trail behind it (not a hard erase each frame). **[shot]**
5. Watch it for 30 s continuous. **Pass:** no visible tearing, no stuck/ghost trail, no colour
   corruption at the trail edges.
6. Switch away to Winamp Bars and back to Scope a few times. **Pass:** trail resets cleanly each
   time, no leftover pixels from the previous session.
7. Play music at the same time (this is the actual risk — blend logic competing with audio-critical
   SDRAM traffic). **Pass:** 0 audible glitches while the trail is animating.

### C. Settings fade-in (5 min)
8. Open any Settings page (Appearance, Playback, Diagnostics). **Pass:** the page fades in over
   about 100 ms rather than popping in instantly. **[shot]** one example.
9. Navigate quickly between several pages in a row. **Pass:** no visible flicker or double-fade,
   fade never gets "stuck" mid-fade.

### D. Hardware rounded-rect (`OP_RRECT`) (5 min)
This silently replaced the software-drawn selection highlight in every list (`fb_round_rect_on`,
B-240) — you're testing it by using any list, not a dedicated screen.
10. Open the library and scroll through a list with the selection highlight moving. **Pass:** the
    highlighted row's rounded corners look identical to before (same radius, no jagged edges, no
    missing/extra pixels at the corners). **[shot]** one list with the highlight visible, zoomed if
    your capture allows.
11. Settings > Diagnostics > Info: find the row this build's `RRECT_READY()` probe would report
    (whatever Info row currently surfaces it, if any — if there is none, note that and move on).
    **[shot]**

### E. Decode-stage profiler — the FLAC/MP3 splits (10 min)
No hardware reading of either split exists yet — this is the first one. **Music must be playing**
during Check for these fields to populate.
12. Play an MP3 track continuously for at least 20 s, then Settings > Diagnostics > Check >
    USER CHECK. **[shot]** the QR page.
13. Play a FLAC track continuously for at least 20 s, then run USER CHECK again. **[shot]** the QR
    page.
14. Note which track/album/bitrate you used for each — the decode cost depends on it (I'll decode
    both QR codes and read back `SR_T_DECPROF`'s H/I/S/D/A/X — MP3 — and R — FLAC — fields).

### F. Persist widening 16→32 words (5 min)
15. Set a non-default theme, a non-default accent colour, and a non-default meter preset (Winamp
    Bars or Chladni — whichever has presets loaded). **[shot]** the settings as configured.
16. Quit the core fully (not just the app — actually quit so `interact_persist.json` is written),
    then reopen. **Pass:** the theme/colour/preset you set are restored, not reset to default.
17. Repeat once more after a full power cycle (Pocket off, then on). **Pass:** same result — this
    confirms the widened persist file survives a cold boot, not just a warm re-open.

### G. General regression (5 min)
18. Settings > Diagnostics > Check > STANDARD (music playing). **[shot]** results and QR. **Pass:**
    all PASS except `Track changes` (known pre-existing failure, not new).
19. Open library, settings, playlist overlay; Start closes each from any depth; Left/Right are
    Back/Forward in menus. **Pass:** all as before, nothing regressed.

---

## Part 2 — `TAU_0_6_0_A_3` (RAM-shrink hardware test, ~35 min)

This is the one build with genuine new hardware risk this cycle: it physically removes 64 KB of
on-chip block RAM (68 fewer M10K blocks used) compared with every build before it. The RTL fit
closed timing cleanly on both seeds with real margin, but that only proves the *logic* is correct —
it says nothing about whether firmware actually behaves correctly with less RAM under real use. This
firmware snapshot predates the profiler splits and persist widening, so don't expect those — this
run is purely about whether the shrink itself is safe.

Cold boot into `TAU 0.6.0-alpha.3`.

### A. Does it even work (5 min)
20. Boot, open the library, play an MP3, play a FLAC. **Pass:** same as alpha.6's step 1-2 — no
    blank screen, no reset, no hang, no crackle.
21. Settings > Diagnostics > Info — read every row once. **[shot]** **Pass:** nothing shows garbage
    values, no unexpected error codes. This is the cheapest global "did the RAM cut corrupt
    something" check.

### B. Memory-pressure paths (15 min — the actual point of this test)
These exercise the largest/most RAM-hungry code paths on purpose, since a RAM-inference or
capacity bug would show up as corruption, a hang, or a crash specifically under load, not at idle.
22. Open the largest playlist/library view you have and scroll end to end rapidly (holding
    Up/Down). **Pass:** no visual corruption, no freeze, no reset.
23. Play a FLAC track (heavier decode buffers than MP3) to completion and let it auto-advance to
    the next track, FLAC to FLAC and FLAC to MP3. **Pass:** no `LOAD FAILED`, no glitch at the
    transition.
24. Open a large-cover album (biggest cover art file you have) while music is playing, and switch
    covers rapidly across a few tracks. **Pass:** covers load and swap cleanly, no stale/corrupted
    image, no audio glitch from the art decode competing for RAM/SDRAM.
25. Repeat the blend/trail check from Part 1 step 4-7 here too (Winamp Scope trail + music playing).
    **Pass:** identical behaviour to alpha.6 — confirms blend RTL is unaffected by the RAM shrink
    sharing the same fit.
26. Settings > Diagnostics > Stress (if present in this build) at the highest level for 2-3 minutes
    with music playing. **Pass:** 0 late underruns, no freeze. If Stress isn't in this build, skip
    and note it.

### C. Cold-boot repeatability (10 min)
A RAM-capacity bug is more likely to show up inconsistently (a stack/heap high-water mark that's
sometimes fine, sometimes not) than to fail every single time — repeat the boot a few times.
27. Full power cycle, cold boot, play a track. Repeat 3 times. **Pass:** identical behaviour every
    time — no intermittent hang, no occasional corrupted screen.
28. On the last boot, Settings > Diagnostics > Check > USER CHECK. **[shot]** **Pass:** all PASS
    except the known `Track changes` failure.

### D. What to send back for Part 2
Any deviation here is higher-priority than anything in Part 1 — it's the only build testing a change
that physically alters the hardware's memory. Note the exact step number, what you saw, and whether
a fresh cold boot reproduces it.

---

## What to send back overall
Every **[shot]** from both parts. For any failure: step number, what you saw, whether it repeats
after a cold boot. Highest-value items: E (both QR codes — first-ever profiler reading), F (persist
survives power cycle), and all of Part 2 section B (RAM-shrink safety under load).

## Not in this build (either core)
`clk_sys` 66.667 MHz — timing-closed alone, never combined with anything else, not packaged, not
installed. Helios H2 double buffering — Quartus fit (`dbuf-b340`) just finished Fitter Successful on
both seeds (299/308 RAM), timing/slack not yet checked, not packaged. Neither is exercised by this
test; both need their own future test script once they're on a card.
