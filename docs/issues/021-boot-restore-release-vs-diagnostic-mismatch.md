# `TAU` vs `TAU_DIAGNOSTIC` boot-restore mismatch, no known mechanism

**Status:** Confirmed on hardware (owner report, first v0.4.0 boot, B-080, 2026-09-22). Parked as
"UX friction, not blocking" (B-082, 2026-09-22). **Escalated (B-112, 2026-09-23, full audit):** the
firmware read found no mechanism in the code that would explain the divergence, so "cosmetic" was
never actually established — it's an unexplained behavioural difference, not a known-harmless one.
**Re-parked (owner, 2026-09-23, B-115):** left parked deliberately rather than root-caused now — the
owner intends a full UI/UX redesign from the ground up, and this specific boot-behaviour question
may not survive that redesign in its current form (a rethought boot/idle flow could change or
remove the code path this bug lives in entirely). Investigating it now risks work that gets thrown
away. **Revisit this file only after the redesign's boot/idle flow is settled** — if the redesign
keeps something recognizable as "restore last played on boot," re-check whether the mismatch still
reproduces in the new code before re-investigating; if not reproducible, close as superseded rather
than fixed.

## Observed

On the first boot of each: `TAU` (release build) shows the idle/"select a track" card instead of
restoring the last-played album. `TAU_DIAGNOSTIC` restores correctly (resumes what was last
playing). Both builds were installed from the same firmware source at the same time (B-079).

## Why this isn't understood yet

`lib_boot_restore()` — the function responsible for resuming playback history — is called through
the exact same path in both builds, gated only by `TAU_LIBRARY`, which is `1` in both `release` and
`player-library-diagnostic` (`fw/build.sh`). A source diff between the two targets' macro sets
(B-080) found nothing that touches this path. `lib_boot_restore()` has a fallback (album 0, track 0)
that should succeed on every boot against a non-empty library, so `TAU` returning failure here is
itself unexplained, not just "expected to sometimes not restore."

**B-112's firmware audit independently confirmed this analysis is still accurate as of the current
code** — no firmware change since B-080 touches `lib_boot_restore()` or its call sites, and the two
targets still compile an identical source list differing only by `-D` flags (the safe, tooled
pattern this project otherwise relies on, `docs/FULL_AUDIT_2026-09-23.md` section 2). Whatever is
causing the divergence is not a build-macro asymmetry — the more likely candidates are
persisted-state differences (e.g. leftover/stale interact.json state from earlier test cores
installed on the same card slot) or a timing-dependent boot-sequence effect specific to one core's
install history, neither of which has been investigated yet.

## Evidence already in place

Info's LIBRARY row ends `R1` (boot restore opened something) or `R0` (it did not) on every build
with `TAU_LIBRARY` — added specifically so the next boot of either core states directly whether
this is still reproducing, without needing to guess from playback behaviour alone.

## Suggested next step, if picked back up

Do NOT guess further from source reading alone — B-073's small-cover investigation already showed
that pattern costs a session before B-075 found the real cause by testing directly. Instead: boot
both cores from a clean, freshly-copied card state (rule out interact.json/persist-file carryover
from earlier test installs), read the Info R0/R1 row on each, and if the mismatch still reproduces
under controlled conditions, add targeted instrumentation to `lib_boot_restore()` itself (e.g. which
branch it took, what state it read) rather than continuing to reason about it from the call site.

## Relation to other items

Distinct from the missing loading-message-on-album-pick item (also parked under B-082) — that one
was separately not reproduced from source and needs its own investigation.
